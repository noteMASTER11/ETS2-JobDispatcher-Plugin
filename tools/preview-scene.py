"""Compose a loaded trailer using SCS attachment locators and load definitions."""
import importlib.util, math, pathlib, re
spec = importlib.util.spec_from_file_location('sii', pathlib.Path(__file__).with_name('prepare-previews.py'))
sii = importlib.util.module_from_spec(spec); spec.loader.exec_module(sii)


def vector(text, field, default=(0., 0., 0.)):
    match = re.search(r'\b' + re.escape(field) + r'\s*:\s*\(([^)]+)\)', text)
    return tuple(sii.number(v) for v in re.split(r'[,\s]+', match[1].strip())) if match else default


def collect(defs, records):
    root = defs / 'def/vehicle'
    bodies = {}
    for path in (root / 'trailer_owned').rglob('*.sii'):
        text = sii.read_sii(path)
        if 'accessory_trailer_body_data' not in text: continue
        model = sii.model_def(path)
        if model:
            bodies[model[0]] = {'methods': re.findall(r'cargo_loading_methods\[\]\s*:\s*(\w+)', text),
                                'custom': sii.field(text, 'cargo_custom_loads_path')}
    cargo = {}
    for directory in sorted((root / 'cargo').iterdir()):
        if not directory.is_dir(): continue
        matches = []
        for path in sorted(directory.glob('*.sii')):
            text = sii.read_sii(path)
            if 'cargo_model_match' not in text: continue
            for data_path in re.findall(r'data_path\[\]\s*:\s*"([^"]+)"', text):
                accessory = defs / data_path.lstrip('/')
                body = sii.read_sii(accessory)
                model = sii.field(body, 'detail_model') or sii.field(body, 'model')
                if model:
                    matches.append({'component': [model, sii.field(body, 'variant', 'default'), sii.field(body, 'look', 'default')],
                                    'method': sii.field(text, 'loading_method'), 'dimensions': vector(text, 'dimensions')})
        if matches: cargo['cargo.' + directory.name] = matches
    custom = {}
    for path in sorted((root / 'cargo_custom_loads').rglob('*.sii')):
        for block in re.finditer(r'cargo_custom_load_data\s*:\s*[^\s{]+\s*\{([^}]+)\}', sii.read_sii(path)):
            token = sii.field(block[1], 'cargo_token')
            if token:
                key = '/' + path.parent.relative_to(defs).as_posix()
                custom.setdefault(key, {})['cargo.' + token] = {'offset': vector(block[1], 'offset'),
                                                              'rotation': vector(block[1], 'rotation')}
    trailers = {token: {model: bodies[model] for model, _ in components if model in bodies}
                for token, components in records.items() if token.startswith(('trailer.', 'trailer_def.'))}
    return {'bodies': trailers, 'cargo': cargo, 'custom': custom}


IDENTITY = ((0., 0., 0.), (1., 0., 0., 0.), (1., 1., 1.))


def multiply(a, b):
    w,x,y,z = a; W,X,Y,Z = b
    return (w*W-x*X-y*Y-z*Z, w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X, w*Z+x*Y-y*X+z*W)


def rotate(q, p):
    norm = sum(v*v for v in q)
    if norm < 1e-12: raise ValueError('Invalid attachment rotation')
    inverse = (q[0]/norm, -q[1]/norm, -q[2]/norm, -q[3]/norm)
    return multiply(multiply(q, (0., *p)), inverse)[1:]


def euler(degrees):
    result = (1., 0., 0., 0.)
    for axis, angle in enumerate(degrees):
        half = math.radians(angle)/2; q = [math.cos(half), 0., 0., 0.]; q[axis+1] = math.sin(half)
        result = multiply(result, q)
    return result


def transform(pose, point, normal=False):
    position, rotation, scale = pose
    if normal:
        result = rotate(rotation, tuple(point[i]/scale[i] for i in range(3)))
        length = math.sqrt(sum(v*v for v in result))
        return tuple(v/max(length, 1e-12) for v in result)
    result = rotate(rotation, tuple(point[i]*scale[i] for i in range(3)))
    return tuple(result[i]+position[i] for i in range(3))


def transform_mesh(data, pose):
    """One matrix per attachment; no quaternion allocations for each vertex."""
    position, q, scale = pose
    norm=sum(v*v for v in q)
    if norm<1e-12:raise ValueError('Invalid attachment rotation')
    w,x,y,z=(v/math.sqrt(norm) for v in q)
    a=1-2*(y*y+z*z);b=2*(x*y-z*w);c=2*(x*z+y*w)
    d=2*(x*y+z*w);e=1-2*(x*x+z*z);f=2*(y*z-x*w)
    g=2*(x*z-y*w);h=2*(y*z+x*w);j=1-2*(x*x+y*y)
    sx,sy,sz=scale;tx,ty,tz=position
    for i in range(0,len(data),11):
        X=data[i]*sx;Y=data[i+1]*sy;Z=data[i+2]*sz
        data[i]=a*X+b*Y+c*Z+tx;data[i+1]=d*X+e*Y+f*Z+ty;data[i+2]=g*X+h*Y+j*Z+tz
        X=data[i+3]/sx;Y=data[i+4]/sy;Z=data[i+5]/sz
        A=a*X+b*Y+c*Z;B=d*X+e*Y+f*Z;C=g*X+h*Y+j*Z
        length=max(1e-12,math.sqrt(A*A+B*B+C*C))
        data[i+3]=A/length;data[i+4]=B/length;data[i+5]=C/length


def compose(parent, child):
    return (transform(parent, child[0]), multiply(parent[1], child[1]), tuple(parent[2][i]*child[2][i] for i in range(3)))


def locators(pim, variant):
    text = pim.read_text(encoding='utf8')
    pit = pim.with_suffix('.pit').read_text(encoding='utf8')
    visible = None
    for block in sii.blocks(pit, 'Variant'):
        if sii.field(block, 'Name') == variant:
            visible = {sii.field(part, 'Name') for part in sii.blocks(block, 'Part') if re.search(r'Value:\s*\(\s*1\s*\)', part)}
            break
    allowed = None
    if visible is not None:
        allowed = set()
        for part in sii.blocks(text, 'Part'):
            if sii.field(part, 'Name') in visible:
                match = re.search(r'Locators:\s*([\d\s]+)', part)
                if match: allowed.update(map(int, match[1].split()))
    result = {}
    for block in sii.blocks(text, 'Locator'):
        if allowed is not None and int(sii.field(block, 'Index', '-1')) not in allowed: continue
        name = sii.field(block, 'Name')
        result.setdefault(name, (vector(block, 'Position'), vector(block, 'Rotation', (1.,0.,0.,0.)), vector(block, 'Scale', (1.,1.,1.))))
    return result


def trailer_poses(components, converted, cached_points=None):
    points = cached_points if cached_points is not None else {c: locators(pathlib.Path(converted[c[0]]), c[1]) for c in components}
    # Bodies use the chassis' actual "body" attachment, not the chassis origin.
    anchor = next((points[c]['body'] for c in components if '/chassis/' in c[0] and 'body' in points[c]), None)
    poses = {c: anchor if anchor and '/body/' in c[0] else IDENTITY for c in components}
    return points, poses


def choose_load(metadata, trailer, cargo):
    for body, info in metadata['bodies'].get(trailer, {}).items():
        for match in metadata['cargo'].get(cargo, []):
            method = match['method']
            if method not in info['methods']: continue
            custom = metadata['custom'].get(info['custom'], {}).get(cargo)
            if method == 'custom' and custom is None: continue
            if method not in ('custom', 'area'): continue
            return body, match, custom
    return None


def cargo_instances(load, components, points, poses, units):
    if load is None: return [] # Closed tanks/vans or an already loaded legacy model.
    body_model, match, custom = load
    body = next(c for c in components if c[0] == body_model)
    body_pose = poses[body]; attachments = points[body]
    component = tuple(match['component'])
    if match['method'] == 'custom':
        if 'cargo' not in attachments: raise ValueError('Selected trailer has no cargo attachment')
        offset = (custom['offset'], euler(custom['rotation']), (1.,1.,1.))
        return [(component, compose(compose(body_pose, attachments['cargo']), offset))]
    width, height, length = match['dimensions']
    if min(width, length) <= 0: raise ValueError('Cargo footprint unavailable')
    instances = []; remaining = max(1, units)
    for name, start in sorted(attachments.items()):
        area = re.fullmatch(r'larea_s_(\d+)', name)
        if not area or 'larea_e_' + area[1] not in attachments: continue
        end = attachments['larea_e_' + area[1]][0]
        low = tuple(min(start[0][i], end[i]) for i in range(3))
        high = tuple(max(start[0][i], end[i]) for i in range(3))
        nx = int((high[0]-low[0]+.02)/width); nz = int((high[2]-low[2]+.02)/length)
        turn = False
        rx = int((high[0]-low[0]+.02)/length); rz = int((high[2]-low[2]+.02)/width)
        if rx*rz > nx*nz: nx,nz=rx,rz; width_step,length_step=length,width; turn=True
        else: width_step,length_step=width,length
        if nx*nz == 0: continue
        for z in range(nz):
            for x in range(nx):
                position = ((low[0]+high[0])/2+(x-(nx-1)/2)*width_step,
                            start[0][1], (low[2]+high[2])/2+(z-(nz-1)/2)*length_step)
                local = (position, euler((0.,90. if turn else 0.,0.)), (1.,1.,1.))
                instances.append((component, compose(body_pose, local))); remaining-=1
                if remaining <= 0: return instances
    if not instances: raise ValueError('No matching load area on selected trailer')
    return instances
