"""Validate every cached native mesh and texture; optionally archive obsolete files."""
import argparse, ctypes as c, hashlib, json, math, pathlib, re, struct
p=argparse.ArgumentParser();p.add_argument('--cache',type=pathlib.Path,required=True);p.add_argument('--report',type=pathlib.Path,required=True);p.add_argument('--archive-unused',type=pathlib.Path);a=p.parse_args()
root=a.cache.resolve()
api=c.WinDLL('cabinet',use_last_error=True)
api.CreateDecompressor.argtypes=[c.c_ulong,c.c_void_p,c.POINTER(c.c_void_p)]
api.Decompress.argtypes=[c.c_void_p,c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
api.CloseDecompressor.argtypes=[c.c_void_p]
required={'index.tsv'};meshes=set();textures=set();errors=[];records={}
for line in (root/'index.tsv').read_text(encoding='utf8').splitlines():
    token,name,*_=line.split('\t');assert re.fullmatch(r'[0-9a-f]{24}\.jdm',name)
    records[token]=name;meshes.add(name)
for name in sorted(meshes):
    try:
        data=(root/name).read_bytes();assert data[:4]==b'JDC2'
        size=struct.unpack_from('<I',data,4)[0];assert 16<=size<=64*1024*1024
        decoder=c.c_void_p();assert api.CreateDecompressor(4,None,c.byref(decoder))
        try:
            output=c.create_string_buffer(size);actual=c.c_size_t()
            assert api.Decompress(decoder,data[8:],len(data)-8,output,size,c.byref(actual)) and actual.value==size
        finally:api.CloseDecompressor(decoder)
        decoded=output.raw;magic,count,materials,batches=struct.unpack_from('<4sIII',decoded)
        assert magic==b'JDT1' and 0<count<=600000 and count%3==0 and materials<=512 and batches<=4096
        offset=16
        for _ in range(materials):
            length=struct.unpack_from('<H',decoded,offset)[0];offset+=2
            texture=decoded[offset:offset+length].decode();offset+=length
            assert not texture or re.fullmatch(r'[0-9a-f]{32}\.dds',texture)
            if texture:textures.add(texture)
        assert offset+batches*12+count*44==size
        for _ in range(batches):
            first,n,material=struct.unpack_from('<III',decoded,offset);offset+=12
            assert first+n<=count and material<materials
        for vertex in struct.iter_unpack('<11f',decoded[offset:]):
            assert all(math.isfinite(x) for x in vertex) and max(map(abs,vertex[:3]))<10000
    except Exception as error:errors.append({'mesh':name,'error':type(error).__name__+': '+str(error)})
for name in sorted(textures):
    try:
        data=(root/name).read_bytes();assert data[:4]==b'DDS ' and len(data)>=128
        height,width=struct.unpack_from('<II',data,12);assert 0<width<=8192 and 0<height<=8192
    except Exception as error:errors.append({'texture':name,'error':type(error).__name__+': '+str(error)})
required|=meshes|textures
unused=[path for path in root.iterdir() if path.is_file() and re.fullmatch(r'[0-9a-f]{24}\.jdm|[0-9a-f]{32}\.dds',path.name) and path.name not in required]
if a.archive_unused:
    archive=a.archive_unused.resolve();assert archive!=root and root not in archive.parents and archive not in root.parents
    archive.mkdir(parents=True,exist_ok=True)
    for path in unused:
        assert path.resolve().parent==root
        path.replace(archive/path.name)
report={'mappings':len(records),'meshes':len(meshes),'textures':len(textures),'referenced_bytes':sum((root/name).stat().st_size for name in required),'obsolete_files':len(unused),'errors':errors}
a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report))
raise SystemExit(bool(errors))
