"""Build material/UV previews from already converted game assets, in parallel."""
import argparse, array, ctypes, hashlib, importlib.util, json, math, os, pathlib, re, struct, subprocess
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor, as_completed
from ctypes import wintypes

spec=importlib.util.spec_from_file_location('preview_source',pathlib.Path(__file__).with_name('prepare-previews.py'))
source=importlib.util.module_from_spec(spec);spec.loader.exec_module(source)

def native_pack(data):
    api=ctypes.WinDLL('cabinet',use_last_error=True);handle=ctypes.c_void_p()
    api.CreateCompressor.argtypes=[wintypes.DWORD,ctypes.c_void_p,ctypes.POINTER(ctypes.c_void_p)]
    api.Compress.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
    api.CloseCompressor.argtypes=[ctypes.c_void_p]
    if not api.CreateCompressor(4,None,ctypes.byref(handle)):raise ctypes.WinError(ctypes.get_last_error())
    try:
        needed=ctypes.c_size_t();api.Compress(handle,data,len(data),None,0,ctypes.byref(needed))
        output=ctypes.create_string_buffer(needed.value)
        if not api.Compress(handle,data,len(data),output,len(output),ctypes.byref(needed)):raise ctypes.WinError(ctypes.get_last_error())
        return b'JDC2'+struct.pack('<I',len(data))+output.raw[:needed.value]
    finally:api.CloseCompressor(handle)

def texture_file(root,value,output):
    tobj=root/(value.lstrip('/')+'.tobj')
    if not tobj.is_file():return ''
    match=re.search(r'(/[^\s"\r\n]+\.dds)',tobj.read_text(encoding='utf-8',errors='replace'))
    if not match:return ''
    path=root/match.group(1).lstrip('/')
    if not path.is_file():return ''
    data=path.read_bytes()
    if data[:4]!=b'DDS ':return ''
    # Keep an existing <=512px mip, without decoding/re-encoding BC textures.
    width,height,mips=struct.unpack_from('<I',data,16)[0],struct.unpack_from('<I',data,12)[0],max(1,struct.unpack_from('<I',data,28)[0])
    fourcc=data[84:88];header=148 if fourcc==b'DX10' else 128
    fmt=struct.unpack_from('<I',data,128)[0] if header==148 else 0
    block=8 if fourcc==b'DXT1' or fmt in (71,72,80,81) else 16
    compressed=fourcc in (b'DXT1',b'DXT3',b'DXT5',b'ATI1',b'ATI2',b'BC4U',b'BC5U') or fmt in (71,72,74,75,77,78,80,81,83,84,95,96,98,99)
    offset=header;level=0
    if compressed:
        while max(width,height)>512 and level+1<mips:
            offset+=max(1,(width+3)//4)*max(1,(height+3)//4)*block
            width=max(1,width//2);height=max(1,height//2);level+=1
        if level:
            head=bytearray(data[:header]);struct.pack_into('<I',head,12,height);struct.pack_into('<I',head,16,width)
            struct.pack_into('<I',head,20,max(1,(width+3)//4)*max(1,(height+3)//4)*block)
            struct.pack_into('<I',head,28,mips-level);data=bytes(head)+data[offset:]
    name=hashlib.sha256(data).hexdigest()[:32]+'.dds';target=output/name
    if not target.is_file():
        temporary=target.with_suffix('.'+str(os.getpid())+'.tmp');temporary.write_bytes(data);temporary.replace(target)
    return name

def make_part(job):
    pim_path,variant,root_path,output_path,part_path=job
    pim,root,output,part=map(pathlib.Path,(pim_path,root_path,output_path,part_path))
    if part.is_file():return str(part)
    text=pim.read_text(encoding='utf-8');pit=pim.with_suffix('.pit').read_text(encoding='utf-8')
    visible=None
    for item in source.blocks(pit,'Variant'):
        if source.field(item,'Name')==variant:
            visible={source.field(p,'Name') for p in source.blocks(item,'Part') if re.search(r'Value:\s*\(\s*1\s*\)',p)};break
    allowed=None
    if visible is not None:
        allowed=set()
        for item in source.blocks(text,'Part'):
            if source.field(item,'Name') in visible:
                match=re.search(r'Pieces:\s*([\d\s]+)',item)
                if match:allowed.update(map(int,match.group(1).split()))
    look=next(source.blocks(pit,'Look'),'')
    definitions={source.field(m,'Alias'):m for m in source.blocks(look,'Material')}
    material_info=[];textures=[]
    for material in source.blocks(text,'Material'):
        alias,effect=source.field(material,'Alias'),source.field(material,'Effect')
        definition=definitions.get(alias,'');color=(1.,1.,1.);texture=''
        for attribute in source.blocks(definition,'Attribute'):
            if source.field(attribute,'Tag')=='diffuse':
                match=re.search(r'Value:\s*\(([^)]+)',attribute)
                if match:color=tuple(map(float,match.group(1).split()))[:3]
        for tex in source.blocks(definition,'Texture'):
            if source.field(tex,'Tag').endswith(':texture_base'):
                texture=texture_file(root,source.field(tex,'Value'),output);break
        material_info.append((color,'shadowonly' in effect or 'fakeshadow' in effect));textures.append(texture)
    floats=array.array('f');batches=[]
    for piece in source.blocks(text,'Piece'):
        if allowed is not None and int(source.field(piece,'Index','-1')) not in allowed:continue
        material=int(source.field(piece,'Material','0'))
        color,shadow=material_info[material] if material<len(material_info) else ((1.,1.,1.),False)
        if shadow:continue
        streams={source.field(s,'Tag'):[tuple(map(source.number,row)) for row in source.rows(s)] for s in source.blocks(piece,'Stream') if source.field(s,'Tag') in ('_POSITION','_NORMAL','_UV0')}
        positions=streams.get('_POSITION',[]);normals=streams.get('_NORMAL',[]);uv=streams.get('_UV0',[])
        first=len(floats)//11
        for row in source.rows(next(source.blocks(piece,'Triangles'),'')):
            ids=list(map(int,row))
            if len(ids)!=3 or max(ids)>=len(positions):continue
            for vertex in ids:
                normal=normals[vertex] if vertex<len(normals) else (0.,1.,0.)
                coords=uv[vertex] if vertex<len(uv) else (0.,0.)
                floats.extend((*positions[vertex],*normal,*color,*coords))
        count=len(floats)//11-first
        if count:batches.append((first,count,material))
    count=len(floats)//11
    if count>600000:raise ValueError('unsupported preview vertex count')
    with part.open('wb') as file:
        file.write(struct.pack('<4sIII',b'JDT1',count,len(textures),len(batches)))
        for name in textures:
            encoded=name.encode();file.write(struct.pack('<H',len(encoded))+encoded)
        for batch in batches:file.write(struct.pack('<III',*batch))
        file.write(floats.tobytes())
    return str(part)

def read_part(path):
    with path.open('rb') as file:
        magic,count,materials,batches=struct.unpack('<4sIII',file.read(16));assert magic==b'JDT1'
        textures=[file.read(struct.unpack('<H',file.read(2))[0]).decode() for _ in range(materials)]
        groups=[struct.unpack('<III',file.read(12)) for _ in range(batches)]
        data=array.array('f');data.frombytes(file.read());assert len(data)==count*11
    return textures,groups,data

def sii_units(text):
    for match in re.finditer(r'(?m)^\s*(\w+)\s*:\s*([\w.]+)\s*\{', text):
        level=1;position=match.end();start=position;quoted=False
        while position<len(text) and level:
            ch=text[position]
            if ch=='"' and text[position-1]!='\\':quoted=not quoted
            if not quoted:
                if ch=='{':level+=1
                elif ch=='}':level-=1
            position+=1
        if not level:yield match[1],match[2],text[start:position-1]

def collect(defs):
    root=defs/'def/vehicle';records={}
    for path in sorted((root/'trailer_owned').rglob('*.sii')):
        text=source.read_sii(path);token=source.field(text,'trailer_definition')
        if not token or 'trailer_body_set' not in text:continue
        components=[]
        configurations=[path.parent.with_suffix('.sii'),*sorted(path.parent.parent.glob(path.parent.name+'.*.sii'))]
        configuration_text='\n'.join(source.read_sii(config) for config in configurations)
        for item in re.findall(r'body\[\]:\s*"([^"]+)"',text)+re.findall(r'chassis\[\]:\s*"([^"]+)"',configuration_text):
            model=source.model_def(defs/item.lstrip('/'))
            if model and model not in components:components.append(model)
        if components:records[token]=components
    for path in sorted((root/'trailer').glob('*.sui')):
        units=list(sii_units(source.read_sii(path)))
        accessories={name:source.field(body,'data_path') for kind,name,body in units if kind.endswith('accessory')}
        for kind,legacy,body in units:
            if kind!='trailer':continue
            token=source.field(body,'trailer_definition');components=[]
            for name in re.findall(r'accessories\[\]:\s*([\w.]+)',body):
                item=accessories.get(name,'')
                if not item or '/t_wheel/' in item or '/paint_job/' in item:continue
                model=source.model_def(defs/item.lstrip('/'))
                if model and model not in components:components.append(model)
            if components:
                if token:records.setdefault(token,components)
                records[legacy]=components
    for directory in sorted((root/'cargo').iterdir()):
        if directory.is_dir():
            for path in sorted(directory.glob('*.sii')):
                model=source.model_def(path)
                if model:records['cargo.'+directory.name]=[model];break
    return records

def assemble(job):
    token,components,parts,converted,output_path,wheel_key=job
    output=pathlib.Path(output_path);textures=[];groups=[];vertices=array.array('f')
    def append(component,position=None,mirror=False):
        local_textures,batches,data=read_part(pathlib.Path(parts[component]))
        remap=[]
        for name in local_textures:
            if name not in textures:textures.append(name)
            remap.append(textures.index(name))
        first=len(vertices)//11
        if position is not None:
            sign=-1 if mirror else 1
            for i in range(0,len(data),11):
                data[i]=data[i]*sign+position[0];data[i+1]+=position[1];data[i+2]+=position[2];data[i+3]*=sign
        vertices.extend(data)
        groups.extend((first+a,b,remap[c]) for a,b,c in batches)
    for component in components:append(component)
    # Add native wheel geometry at the model's own wheel locators.
    if token.startswith(('trailer_def.','trailer.')) and wheel_key in parts:
        for component in components:
            pim=pathlib.Path(converted[component[0]])
            added=False
            for loc in source.blocks(pim.read_text(encoding='utf-8'),'Locator'):
                if not re.fullmatch(r'wheel_[rl]_\d+',source.field(loc,'Name')):continue
                match=re.search(r'Position:\s*\(([^)]+)',loc)
                if match:
                    position=tuple(map(source.number,match.group(1).split()));append(wheel_key,position,position[0]>0);added=True
            if added:break
    count=len(vertices)//11
    if not count:raise ValueError('model has no visible geometry')
    if count>600000:raise ValueError('assembled preview exceeds 200000 triangles')
    payload=bytearray(struct.pack('<4sIII',b'JDT1',count,len(textures),len(groups)))
    for texture in textures:
        encoded=texture.encode();payload.extend(struct.pack('<H',len(encoded))+encoded)
    for group in groups:payload.extend(struct.pack('<III',*group))
    payload.extend(vertices.tobytes())
    name=hashlib.sha256(('textured-v1'+json.dumps(components)).encode()).hexdigest()[:24]+'.jdm'
    (output/name).write_bytes(native_pack(bytes(payload)))
    return token,name,'; '.join(c[0] for c in components)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--work',type=pathlib.Path,required=True);parser.add_argument('--output',type=pathlib.Path,required=True);parser.add_argument('--workers',type=int,default=os.cpu_count() or 1)
    parser.add_argument('--game',type=pathlib.Path,required=True)
    parser.add_argument('--converter',type=pathlib.Path,required=True)
    parser.add_argument('--mod',type=pathlib.Path,action='append',default=[])
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);parts_dir=args.work/'textured-parts-v2';parts_dir.mkdir(exist_ok=True)
    records=collect(args.work/'defs');converted={};jobs={};failures=[]
    wheel=('/vehicle/wheel/trailer/single_385_55/alu.pmd','default')
    needed={c for cs in records.values() for c in cs};needed.add(wheel)
    archives=[args.game/name for name in ('base.scs','base_share.scs','base_vehicle.scs','def.scs')]
    archives+=sorted(args.game.glob('dlc_*.scs'))
    archives+=args.mod
    mounts=[part for archive in archives if archive.is_file() for part in ('-b',str(archive))]
    def convert(model):
        stem=pathlib.PurePosixPath(model).with_suffix('').as_posix()
        pim=args.work/'converted'/(stem.lstrip('/')+'.pim')
        if not pim.is_file():
            result=subprocess.run([str(args.converter),*mounts,'-m',stem,'-e',str(args.work/'converted')],capture_output=True,text=True,encoding='utf8',errors='replace',timeout=120)
            if result.returncode or not pim.is_file():return model
    with ThreadPoolExecutor(args.workers) as pool:
        missing=list(pool.map(convert,sorted({model for model,variant in needed})))
    failures.extend({'model':model,'error':'model unavailable'} for model in missing if model)
    for model,variant in needed:
        pim=args.work/'converted'/(pathlib.PurePosixPath(model).with_suffix('').as_posix().lstrip('/')+'.pim')
        if pim.is_file():
            converted[model]=str(pim);part=parts_dir/(hashlib.sha256((model+'\n'+variant).encode()).hexdigest()[:24]+'.part')
            jobs[(model,variant)]=(str(pim),variant,str(args.work/'converted'),str(args.output),str(part))
    parts={}
    with ProcessPoolExecutor(args.workers) as pool:
        futures={pool.submit(make_part,job):component for component,job in jobs.items()}
        for i,future in enumerate(as_completed(futures),1):
            try:parts[futures[future]]=future.result()
            except Exception as error:failures.append({'component':futures[future],'error':str(error)})
            if i%25==0:print(f'Textured parts {i}/{len(futures)}',flush=True)
        tasks={pool.submit(assemble,(token,components,parts,converted,str(args.output),wheel)):token for token,components in records.items() if all(c in parts for c in components)}
        rows=[]
        for i,future in enumerate(as_completed(tasks),1):
            try:rows.append(future.result())
            except Exception as error:failures.append({'token':tasks[future],'error':str(error)})
            if i%50==0:print(f'Assembled {i}/{len(tasks)}',flush=True)
    (args.output/'index.tsv').write_text(''.join('\t'.join(row)+'\n' for row in sorted(rows)),encoding='utf-8')
    (args.work/'textured-report.json').write_text(json.dumps({'ready':len(rows),'failures':failures},indent=2),encoding='utf-8')
    print(f'Textured preview cache: {len(rows)} mappings, {len(failures)} failed parts/configurations',flush=True)

if __name__=='__main__':main()
