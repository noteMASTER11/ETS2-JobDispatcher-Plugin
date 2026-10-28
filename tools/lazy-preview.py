"""Prepare exactly one selected preview. Never convert the complete catalogue."""
import hashlib, importlib.util, json, pathlib, re, shutil, subprocess, time

ROOT = pathlib.Path(__file__).parent
spec = importlib.util.spec_from_file_location('textured', ROOT / 'prepare-textured-previews.py')
builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
spec = importlib.util.spec_from_file_location('scene', ROOT / 'preview-scene.py')
scene = importlib.util.module_from_spec(spec); spec.loader.exec_module(scene)
MAX_BYTES = 128 * 1024 * 1024
MAX_ENTRIES = 8
MANAGED = re.compile(r'(?:[0-9a-f]{24}\.(?:jdm|part)|[0-9a-f]{32}\.dds)\Z')


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=True), encoding='utf8')
    temporary.replace(path)


def read_entries(output):
    try: return json.loads((output / 'entries.json').read_text(encoding='utf8'))
    except (OSError, ValueError): return {}


def read_components(output):
    try:return json.loads((output/'components.json').read_text(encoding='utf8'))
    except (OSError,ValueError):return {}


def publish(output, entries, selected=None):
    """LRU disk budget; only hashes owned by this preview cache are removed."""
    def files_for(records):
        return {name for entry in records.values() for name in entry['files'] if MANAGED.fullmatch(name)}
    components=read_components(output)
    def total(records):
        return sum((output / name).stat().st_size for name in files_for(records)|files_for(components) if (output / name).is_file())
    for component in sorted(components,key=lambda c:components[c]['used']):
        if len(components)<=24 and total(entries)<=MAX_BYTES:break
        del components[component]
    for token in sorted(entries, key=lambda t: max(entries[t]['used'], (output / entries[t]['mesh']).stat().st_mtime_ns if (output / entries[t]['mesh']).is_file() else 0)):
        if len(entries) <= MAX_ENTRIES and total(entries) <= MAX_BYTES: break
        if token != selected: del entries[token]
    if total(entries) > MAX_BYTES: raise ValueError('Selected preview exceeds cache budget')
    atomic_json(output/'components.json',components)
    atomic_json(output / 'entries.json', entries)
    temporary = output / 'index.tmp'
    temporary.write_text(''.join(token + '\t' + entry['mesh'] + '\n' for token, entry in sorted(entries.items())), encoding='utf8')
    temporary.replace(output / 'index.tsv')
    keep = files_for(entries)|files_for(components)
    for path in output.iterdir():
        if MANAGED.fullmatch(path.name) and path.name not in keep and not path.is_symlink(): path.unlink()


def reset(output, identity):
    output.mkdir(parents=True, exist_ok=True)
    try: old = (output / 'identity.txt').read_text(encoding='ascii')
    except OSError: old = ''
    if old != identity:
        atomic_json(output/'components.json',{})
        publish(output, {})
        (output / 'identity.txt').write_text(identity, encoding='ascii')


def retire_legacy(data):
    """Discard only the old generated mesh/texture cache after metadata is ready."""
    legacy = data / 'previews'
    if not legacy.is_dir() or legacy.is_symlink() or legacy.resolve().parent != data.resolve(): return
    for path in legacy.iterdir():
        if (MANAGED.fullmatch(path.name) or path.name == 'index.tsv') and path.is_file() and not path.is_symlink():
            path.unlink()
    if not any(legacy.iterdir()): legacy.rmdir()


def prepare(game, data, converter, mods, token, runner=subprocess.run):
    output = data / 'previews-lazy'
    records = json.loads((output / 'catalogue.json').read_text(encoding='utf8'))
    selection = token.split('|')
    trailer = selection[0]
    cargo = selection[1] if len(selection) > 1 else ''
    units = int(selection[2]) if len(selection) > 2 else 1
    if trailer not in records: raise ValueError('No preview definition for selected item')
    load = None
    if cargo:
        metadata = json.loads((output / 'loading.json').read_text(encoding='utf8'))
        load = scene.choose_load(metadata, trailer, cargo)
    # Closed vans/tanks have the same exterior for every concealed cargo.
    cache_key=trailer+'|@body' if cargo and load is None else token
    entries = read_entries(output)
    if cache_key in entries and all((output / f).is_file() for f in entries[cache_key]['files']):
        entries[cache_key]['used'] = time.time_ns(); publish(output, entries, cache_key)
        print('Selected preview cache hit:', token, flush=True); return
    components = [tuple(c) for c in records[trailer]]
    wheel = ('/vehicle/wheel/trailer/single_385_55/alu.pmd', 'default')
    needed = set(components)
    if trailer.startswith(('trailer.', 'trailer_def.')): needed.add(wheel)
    if load: needed.add(tuple(load[1]['component']))
    # One request directory. The native worker owns this process and cancels its
    # entire descendant tree when selection/profile changes or the window closes.
    work = output / 'request'
    if work.is_symlink(): raise ValueError('Preview work directory is a symlink')
    if work.exists(): shutil.rmtree(work)
    work.mkdir()
    stage = work / 'result'; stage.mkdir()
    converted = {}; parts = {}; cached_points={}
    component_cache=read_components(output)
    needed_textures=set()
    def component_key(component):return hashlib.sha256(json.dumps(component).encode()).hexdigest()[:24]
    missing=set()
    for component in needed:
        key=component_key(component);entry=component_cache.get(key)
        if entry and all((output/name).is_file() for name in entry['files']):
            parts[component]=str(output/entry['part']);cached_points[component]=entry['points']
            needed_textures.update(name for name in entry['files'] if name.endswith('.dds'))
            entry['used']=time.time_ns()
        else:missing.add(component)
    archives = [game / name for name in ('base.scs', 'base_share.scs', 'base_vehicle.scs', 'def.scs')]
    archives += sorted(game.glob('dlc_*.scs')) + mods
    mounts = [part for archive in archives if archive.exists() for part in ('-b', str(archive))]
    try:
        for model in sorted({c[0] for c in missing}):
            stem = pathlib.PurePosixPath(model).with_suffix('').as_posix()
            destination = work / 'converted'
            pim = destination / (stem.lstrip('/') + '.pim')
            result = runner([str(converter), *mounts, '-m', stem, '-e', str(destination)],
                            capture_output=True, text=True, encoding='utf8', errors='replace', timeout=120)
            if result.returncode or not pim.is_file():
                if any(c[0] == model for c in needed if c != wheel): raise ValueError('Selected model unavailable: ' + model)
                continue
            converted[model] = str(pim)
        for component in sorted(missing):
            model, variant = component[:2]
            look = component[2] if len(component) > 2 else 'default'
            if model not in converted: continue
            part = work / (hashlib.sha256((model + '\n' + variant + '\n' + look).encode()).hexdigest()[:24] + '.part')
            builder.make_part((converted[model], variant, str(work / 'converted'), str(stage), str(part), look))
            key=component_key(component);name=key+'.part'
            temporary=output/(name+'.tmp');shutil.copyfile(part,temporary);temporary.replace(output/name)
            parts[component]=str(output/name)
            cached_points[component]=scene.locators(pathlib.Path(converted[model]),variant)
            # Store only the material textures referenced by this part.
            textures,_,_=builder.read_part(output/name)
            textures=[n for n in textures if n]
            for texture in textures:
                if (stage/texture).is_file():shutil.copyfile(stage/texture,output/texture)
            component_cache[key]={'part':name,'points':cached_points[component],
                                  'files':[name,*textures],'used':time.time_ns()}
            needed_textures.update(textures)
            atomic_json(output/'components.json',component_cache)
        points, poses = scene.trailer_poses(components, converted, {c:cached_points[c] for c in components})
        instances = [(c, poses[c]) for c in components]
        if cargo:instances += scene.cargo_instances(load, components, points, poses, units)
        row = builder.assemble((token, components, parts, converted, str(stage), wheel, instances, poses,points))
        files = list({p.name for p in stage.iterdir() if MANAGED.fullmatch(p.name)}|needed_textures)
        if sum((stage/f if (stage/f).is_file() else output/f).stat().st_size for f in files)>MAX_BYTES:raise ValueError('Selected preview exceeds cache budget')
        for name in files:
            if (stage/name).is_file():(stage / name).replace(output / name)
        entries[cache_key] = {'mesh': row[1], 'files': files, 'used': time.time_ns()}
        publish(output, entries, cache_key)
        print('Prepared selected preview:', token, 'models:', len(converted), flush=True)
    finally:
        if work.exists() and not work.is_symlink(): shutil.rmtree(work)
