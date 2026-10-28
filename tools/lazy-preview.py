"""Prepare exactly one selected preview. Never convert the complete catalogue."""
import hashlib, importlib.util, json, pathlib, re, shutil, subprocess, time

ROOT = pathlib.Path(__file__).parent
spec = importlib.util.spec_from_file_location('textured', ROOT / 'prepare-textured-previews.py')
builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
MAX_BYTES = 128 * 1024 * 1024
MAX_ENTRIES = 8
MANAGED = re.compile(r'(?:[0-9a-f]{24}\.jdm|[0-9a-f]{32}\.dds)\Z')


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=True), encoding='utf8')
    temporary.replace(path)


def read_entries(output):
    try: return json.loads((output / 'entries.json').read_text(encoding='utf8'))
    except (OSError, ValueError): return {}


def publish(output, entries, selected=None):
    """LRU disk budget; only hashes owned by this preview cache are removed."""
    def files_for(records):
        return {name for entry in records.values() for name in entry['files'] if MANAGED.fullmatch(name)}
    def total(records):
        return sum((output / name).stat().st_size for name in files_for(records) if (output / name).is_file())
    for token in sorted(entries, key=lambda t: max(entries[t]['used'], (output / entries[t]['mesh']).stat().st_mtime_ns if (output / entries[t]['mesh']).is_file() else 0)):
        if len(entries) <= MAX_ENTRIES and total(entries) <= MAX_BYTES: break
        if token != selected: del entries[token]
    if total(entries) > MAX_BYTES: raise ValueError('Selected preview exceeds cache budget')
    atomic_json(output / 'entries.json', entries)
    temporary = output / 'index.tmp'
    temporary.write_text(''.join(token + '\t' + entry['mesh'] + '\n' for token, entry in sorted(entries.items())), encoding='utf8')
    temporary.replace(output / 'index.tsv')
    keep = files_for(entries)
    for path in output.iterdir():
        if MANAGED.fullmatch(path.name) and path.name not in keep and not path.is_symlink(): path.unlink()


def reset(output, identity):
    output.mkdir(parents=True, exist_ok=True)
    try: old = (output / 'identity.txt').read_text(encoding='ascii')
    except OSError: old = ''
    if old != identity:
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
    if token not in records: raise ValueError('No preview definition for selected item')
    entries = read_entries(output)
    if token in entries and all((output / f).is_file() for f in entries[token]['files']):
        entries[token]['used'] = time.time_ns(); publish(output, entries, token)
        print('Selected preview cache hit:', token, flush=True); return
    components = [tuple(c) for c in records[token]]
    wheel = ('/vehicle/wheel/trailer/single_385_55/alu.pmd', 'default')
    needed = set(components)
    if token.startswith(('trailer.', 'trailer_def.')): needed.add(wheel)
    # One request directory. The native worker owns this process and cancels its
    # entire descendant tree when selection/profile changes or the window closes.
    work = output / 'request'
    if work.is_symlink(): raise ValueError('Preview work directory is a symlink')
    if work.exists(): shutil.rmtree(work)
    work.mkdir()
    stage = work / 'result'; stage.mkdir()
    converted = {}; parts = {}
    archives = [game / name for name in ('base.scs', 'base_share.scs', 'base_vehicle.scs', 'def.scs')]
    archives += sorted(game.glob('dlc_*.scs')) + mods
    mounts = [part for archive in archives if archive.exists() for part in ('-b', str(archive))]
    try:
        for model in sorted({model for model, _ in needed}):
            stem = pathlib.PurePosixPath(model).with_suffix('').as_posix()
            destination = work / 'converted'
            pim = destination / (stem.lstrip('/') + '.pim')
            result = runner([str(converter), *mounts, '-m', stem, '-e', str(destination)],
                            capture_output=True, text=True, encoding='utf8', errors='replace', timeout=120)
            if result.returncode or not pim.is_file():
                if any(c[0] == model for c in components): raise ValueError('Selected model unavailable: ' + model)
                continue
            converted[model] = str(pim)
        for model, variant in sorted(needed):
            if model not in converted: continue
            part = work / (hashlib.sha256((model + '\n' + variant).encode()).hexdigest()[:24] + '.part')
            parts[(model, variant)] = builder.make_part((converted[model], variant, str(work / 'converted'), str(stage), str(part)))
        row = builder.assemble((token, components, parts, converted, str(stage), wheel))
        files = [p.name for p in stage.iterdir() if MANAGED.fullmatch(p.name)]
        if sum((stage / f).stat().st_size for f in files) > MAX_BYTES: raise ValueError('Selected preview exceeds cache budget')
        for name in files: (stage / name).replace(output / name)
        entries[token] = {'mesh': row[1], 'files': files, 'used': time.time_ns()}
        publish(output, entries, token)
        print('Prepared selected preview:', token, 'models:', len(converted), flush=True)
    finally:
        if work.exists() and not work.is_symlink(): shutil.rmtree(work)
