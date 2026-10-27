"""Extract real SCS geometry into a bounded, texture-free preview cache.

The parser helpers are also used by prepare-textured-previews.py. ConverterPIX
is a separate executable; the preparation does not launch the game.
"""
import argparse, hashlib, json, math, pathlib, re, struct, subprocess, sys, os, shutil
from concurrent.futures import ThreadPoolExecutor, ProcessPoolExecutor, as_completed

def blocks(text, tag):
    pattern = re.compile(r'\b' + re.escape(tag) + r'\s*\{')
    for match in pattern.finditer(text):
        level, position, quoted = 1, match.end(), False
        start = position
        while position < len(text) and level:
            ch = text[position]
            if ch == '"' and (position == 0 or text[position - 1] != '\\'): quoted = not quoted
            if not quoted:
                if ch == '{': level += 1
                elif ch == '}': level -= 1
            position += 1
        if not level: yield text[start:position - 1]

def field(text, name, fallback=''):
    match = re.search(r'^\s*' + re.escape(name) + r'\s*:\s*(?:"([^"]*)"|([^\s{}]+))', text, re.M)
    return (match.group(1) if match.group(1) is not None else match.group(2)) if match else fallback

def number(value):
    return struct.unpack('<f', bytes.fromhex(value[1:])[::-1])[0] if value.startswith('&') else float(value)

def rows(text):
    for match in re.finditer(r'^\s*\d+\s*\(\s*([^)]*)\)', text, re.M):
        yield match.group(1).split()

def read_sii(path):
    if not path.is_file(): return ''
    return re.sub(r'#.*', '', path.read_text(encoding='utf-8-sig', errors='replace'))

def model_def(path):
    text = read_sii(path)
    model = field(text, 'model') or field(text, 'detail_model')
    return (model, field(text, 'variant', 'default')) if model else None

def geometry(pim, variant):
    if pim.stat().st_size > 64 * 1024 * 1024: raise ValueError('preview model exceeds 64 MiB source limit')
    text = pim.read_text(encoding='utf-8')
    pit = pim.with_suffix('.pit').read_text(encoding='utf-8')
    visible = None
    for item in blocks(pit, 'Variant'):
        if field(item, 'Name') == variant:
            visible = {field(part, 'Name') for part in blocks(item, 'Part') if re.search(r'Value:\s*\(\s*1\s*\)', part)}
            break
    allowed = None
    if visible is not None:
        allowed = set()
        for part in blocks(text, 'Part'):
            if field(part, 'Name') in visible:
                match = re.search(r'Pieces:\s*([\d\s]+)', part)
                if match: allowed.update(map(int, match.group(1).split()))
    materials = []
    for material in blocks(text, 'Material'):
        materials.append((field(material, 'Alias').lower(), field(material, 'Effect')))
    vertices = []
    for piece in blocks(text, 'Piece'):
        if allowed is not None and int(field(piece, 'Index', '-1')) not in allowed: continue
        material = int(field(piece, 'Material', '0'))
        alias, effect = materials[material] if material < len(materials) else ('', '')
        if 'shadowonly' in effect: continue
        positions = []
        for stream in blocks(piece, 'Stream'):
            if field(stream, 'Tag') == '_POSITION': positions = [tuple(map(number, row)) for row in rows(stream)]
        triangle_block = next(blocks(piece, 'Triangles'), '')
        if not positions: continue
        color = (.64, .7, .76)
        if any(key in alias for key in ('tire', 'tyre', 'rubber', 'plastic')): color = (.12, .14, .17)
        elif 'glass' in alias: color = (.23, .42, .51)
        elif any(key in alias for key in ('body', 'paint', 'reefer', 'curtain')): color = (.72, .78, .84)
        for row in rows(triangle_block):
            ids = list(map(int, row))
            if len(ids) != 3 or max(ids) >= len(positions): continue
            points = [positions[index] for index in ids]
            a = [points[1][i] - points[0][i] for i in range(3)]
            b = [points[2][i] - points[0][i] for i in range(3)]
            n = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
            length = math.sqrt(sum(x*x for x in n))
            if length < 1e-10: continue
            normal = tuple(x/length for x in n)
            for point in points: vertices.append((*point, *normal, *color))
    if not vertices: raise ValueError('model has no visible triangles')
    if len(vertices) > 240000: raise ValueError('preview exceeds 80000 triangles')
    return vertices

def make_part(job):
    pim, variant, output = job
    pim, output = pathlib.Path(pim), pathlib.Path(output)
    if output.is_file(): return str(output)
    vertices = geometry(pim, variant)
    with output.open('wb') as file:
        file.write(struct.pack('<4sI', b'JDM1', len(vertices)))
        for vertex in vertices: file.write(struct.pack('<9f', *vertex))
    return str(output)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', type=pathlib.Path, required=True)
    parser.add_argument('--converter', type=pathlib.Path, required=True)
    parser.add_argument('--work', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--limit', type=int, default=0)
    parser.add_argument('--workers', type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()
    for directory in (args.work, args.output):
        directory.mkdir(parents=True, exist_ok=True)
    archives = [args.game / name for name in ('base.scs', 'base_share.scs', 'base_vehicle.scs', 'def.scs')]
    archives += sorted(args.game.glob('dlc_*.scs'))
    mounts = [part for archive in archives if archive.is_file() for part in ('-b', str(archive))]
    def execute(*parameters):
        result = subprocess.run([str(args.converter), *mounts, *parameters], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        if result.returncode: raise ValueError(result.stdout[-500:] + result.stderr[-500:])
        return result.stdout
    defs = args.work / 'defs'
    for directory in ('trailer', 'trailer_defs', 'trailer_owned', 'cargo', 't_wheel'):
        execute('--extract-directory', '/def/vehicle/' + directory, '-e', str(defs))
    root = defs / 'def/vehicle'
    records = {}
    # Owned trailer body sets link exact live trailer_def identifiers to bodies.
    for path in sorted((root / 'trailer_owned').rglob('*.sii')):
        text = read_sii(path)
        token = field(text, 'trailer_definition')
        if not token or 'trailer_body_set' not in text: continue
        components = []
        for body in re.findall(r'body\[\]:\s*"([^"]+)"', text):
            model = model_def(defs / body.lstrip('/'))
            if model: components.append(model)
        config = path.parent.with_suffix('.sii')
        for chassis in re.findall(r'chassis\[\]:\s*"([^"]+)"', read_sii(config)):
            model = model_def(defs / chassis.lstrip('/'))
            if model: components.append(model)
        if components: records[token] = components
    # Legacy trailers generally contain a complete chassis/body model.
    for path in sorted((root / 'trailer').glob('*.sui')):
        text = read_sii(path)
        token = field(text, 'trailer_definition')
        if not token or token in records: continue
        components = []
        for accessory in re.findall(r'data_path:\s*"([^"]+)"', text):
            if '/t_wheel/' in accessory or '/paint_job/' in accessory: continue
            model = model_def(defs / accessory.lstrip('/'))
            if model and model not in components: components.append(model)
        if components: records[token] = components
    for directory in sorted((root / 'cargo').iterdir()):
        if not directory.is_dir(): continue
        for path in sorted(directory.glob('*.sii')):
            model = model_def(path)
            if model:
                records['cargo.' + directory.name] = [model]
                break
    selected = list(sorted(records.items()))
    if args.limit: selected = selected[:args.limit]
    pending = [(token, components) for token, components in selected
               if not (args.output / (hashlib.sha256(json.dumps(components).encode()).hexdigest()[:24] + '.jdm')).is_file()]
    stems = {pathlib.PurePosixPath(model).with_suffix('').as_posix() for _, components in pending for model, _ in components}
    converted, errors = {}, {}
    def convert(stem):
        pim = args.work / 'converted' / (stem.lstrip('/') + '.pim')
        if not pim.is_file(): execute('-m', stem, '-e', str(args.work / 'converted'))
        if not pim.is_file(): raise ValueError('source model is not available in mounted archives')
        return pim
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(convert, stem): stem for stem in stems}
        for done, future in enumerate(as_completed(futures), 1):
            stem = futures[future]
            try: converted[stem] = future.result()
            except Exception as error: errors[stem] = str(error)
            if done % 20 == 0: print(f'Converted {done}/{len(stems)} models', flush=True)
    parts_dir = args.work / 'parts'; parts_dir.mkdir(exist_ok=True)
    parts, part_errors = {}, {}
    components_needed = {component for _, components in pending for component in components}
    with ProcessPoolExecutor(max_workers=args.workers) as pool:
        futures = {}
        for model, variant in components_needed:
            stem = pathlib.PurePosixPath(model).with_suffix('').as_posix()
            if stem not in converted: part_errors[(model, variant)] = errors.get(stem, 'model unavailable'); continue
            output = parts_dir / (hashlib.sha256((model+'\n'+variant).encode()).hexdigest()[:24] + '.jdm')
            futures[pool.submit(make_part, (str(converted[stem]), variant, str(output)))] = (model, variant)
        for done, future in enumerate(as_completed(futures), 1):
            component = futures[future]
            try: parts[component] = pathlib.Path(future.result())
            except Exception as error: part_errors[component] = str(error)
            if done % 25 == 0: print(f'Prepared {done}/{len(futures)} geometry parts', flush=True)
    rows_out, failures = [], []
    for index, (token, components) in enumerate(selected):
        key = hashlib.sha256(json.dumps(components).encode()).hexdigest()[:24]
        mesh = args.output / (key + '.jdm')
        try:
            if not mesh.is_file():
                sources, count = [], 0
                for component in components:
                    if component not in parts: raise ValueError(part_errors.get(component, 'component unavailable'))
                    with parts[component].open('rb') as file: _, size = struct.unpack('<4sI', file.read(8))
                    count += size; sources.append(parts[component])
                if count > 300000: raise ValueError('assembled model exceeds 100000 triangles')
                with mesh.open('wb') as file:
                    file.write(struct.pack('<4sI', b'JDM1', count))
                    for source in sources:
                        with source.open('rb') as part: part.seek(8); shutil.copyfileobj(part, file, 65536)
            rows_out.append((token, mesh.name, '; '.join(x[0] for x in components)))
        except (ValueError, OSError, subprocess.TimeoutExpired) as error:
            failures.append({'token': token, 'error': str(error)[:200]})
        if index % 25 == 0: print(f'{index+1}/{len(records)}: {len(rows_out)} ready, {len(failures)} unavailable', flush=True)
    (args.output / 'index.tsv').write_text(''.join('\t'.join(row)+'\n' for row in rows_out), encoding='utf-8')
    (args.work / 'report.json').write_text(json.dumps({'ready': len(rows_out), 'failures': failures, 'archives': list(map(str, archives))}, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'Preview cache: {len(rows_out)} records, {len(failures)} unavailable', flush=True)
    return 0

if __name__ == '__main__': sys.exit(main())
