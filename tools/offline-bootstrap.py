"""Prepare the installed profile locally. No network access or package manager."""
import argparse, contextlib, hashlib, json, os, pathlib, re, shutil, subprocess, sys, traceback


def active_mods(log, game, home):
    text = log.read_text(encoding='utf8', errors='replace') if log.is_file() else ''
    active = []
    for line in text.splitlines():
        if re.search(r'\[mods\] Active \d+ mods', line): active = []
        match = re.search(r'\[mods\] Active local mod (.+?) \(name:', line)
        if match: active.append(('local', match[1]))
        match = re.search(r'\[mods\] Active workshop mod ID (\d+) ', line)
        if match: active.append(('workshop', match[1]))
    devices = []
    for match in re.finditer(r'\[fs\] device (.+?) mounted to mod pool\.', text):
        path = pathlib.Path(match[1])
        if path.exists(): devices.append(path.resolve())
    result = []
    for kind, identity in active:
        candidates = [p for p in devices if (p.stem == identity if kind == 'local' else identity in p.parts)]
        if not candidates and kind == 'local':
            candidates = [home / 'mod' / (identity + suffix) for suffix in ('.scs', '.zip', '')]
        if not candidates and kind == 'workshop':
            directory = game.parent.parent / 'workshop/content/227300' / identity
            if directory.is_dir(): candidates = sorted(directory.rglob('*.scs')) + sorted(directory.rglob('*.zip'))
        for path in candidates:
            if path.exists() and path.resolve() not in result: result.append(path.resolve())
    return result


def fingerprint(game, mods, scripts):
    files = [game / n for n in ('base.scs','base_share.scs','base_vehicle.scs','def.scs','locale.scs')]
    files += sorted(game.glob('dlc_*.scs')) + mods
    rows = []
    for path in files:
        if path.is_dir(): entries = sorted(p for p in path.rglob('*') if p.is_file())
        else: entries = [path]
        for item in entries:
            if item.exists():
                stat = item.stat(); rows.append((str(item.resolve()), stat.st_size, stat.st_mtime_ns))
    for path in sorted(scripts.glob('*.py')):
        rows.append((path.name, hashlib.sha256(path.read_bytes()).hexdigest()))
    return hashlib.sha256(json.dumps(['offline-assets-v1', rows], ensure_ascii=True).encode()).hexdigest()


def prepare(game, data, home, converter, workers, extra_mods=()):
    scripts = pathlib.Path(__file__).parent
    mods = active_mods(home / 'game.log.txt', game, home)
    mods += [p.resolve() for p in extra_mods if p.resolve() not in mods]
    identity = fingerprint(game, mods, scripts)
    marker = data / 'prepared-assets.json'
    try: previous = json.loads(marker.read_text(encoding='utf8'))
    except (OSError, ValueError): previous = {}
    if previous.get('fingerprint') == identity and (data / 'previews/index.tsv').is_file() and (data / 'game-labels.en_gb.tsv').is_file():
        print('Prepared cache is current; no extraction needed.', flush=True)
        return
    if hashlib.sha256(converter.read_bytes()).hexdigest() != '60310ee8e01563145b71a8fb62f63d7a8b03906334e84e1463918f615f192ed2':
        raise RuntimeError('Bundled converter checksum differs')
    work = data / 'preparation' / identity[:24]
    expected_parent = (data / 'preparation').resolve()
    if work.exists() and not work.is_symlink() and work.resolve().parent == expected_parent and work.name == identity[:24]:
        shutil.rmtree(work) # Discard an interrupted conversion, never reuse partial meshes.
    command = [sys.executable, str(scripts / 'prepare-game-assets.py'), '--game', str(game), '--converter', str(converter),
               '--work', str(work), '--output', str(data), '--workers', str(max(1, workers))]
    command += [value for mod in mods for value in ('--mod', str(mod))]
    print('Preparing installed game and', len(mods), 'active mod archives.', flush=True)
    subprocess.run(command, check=True, stdout=sys.stdout, stderr=sys.stderr)
    temporary = marker.with_suffix('.tmp')
    temporary.write_text(json.dumps({'fingerprint': identity, 'mods': list(map(str, mods))}, ensure_ascii=False, indent=2), encoding='utf8')
    temporary.replace(marker)
    # Only our generated work directory; retain the small finished runtime cache.
    if work.exists() and not work.is_symlink() and work.resolve().parent == expected_parent and work.name == identity[:24]:
        shutil.rmtree(work)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=pathlib.Path, required=True)
    parser.add_argument('--data', type=pathlib.Path, required=True)
    parser.add_argument('--game-home', type=pathlib.Path, required=True)
    parser.add_argument('--converter', type=pathlib.Path, required=True)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--mod', type=pathlib.Path, action='append', default=[])
    args = parser.parse_args()
    args.data.mkdir(parents=True, exist_ok=True)
    try:
        settings=json.loads((args.data/'offline-settings.json').read_text(encoding='utf-8-sig'))
        args.mod.extend(pathlib.Path(p) for p in settings.get('extra_mods',[]))
        args.workers=max(1,int(settings.get('workers',args.workers)))
    except (OSError,ValueError,TypeError): pass
    temp = args.data / 'temp'; temp.mkdir(exist_ok=True)
    os.environ.update(TEMP=str(temp), TMP=str(temp), PYTHONIOENCODING='utf-8', PYTHONDONTWRITEBYTECODE='1')
    sys.dont_write_bytecode = True
    with (args.data / 'preparation.log').open('a', encoding='utf8', buffering=1) as log:
        with contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
            try: prepare(args.game, args.data, args.game_home, args.converter, args.workers, args.mod)
            except Exception:
                traceback.print_exc(); return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
