"""Prepare local labels and career-area metadata. Models stay in the game VFS."""
import argparse, os, pathlib, re, subprocess, sys


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=pathlib.Path, required=True)
    parser.add_argument('--converter', type=pathlib.Path, required=True)
    parser.add_argument('--work', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--workers', type=int, default=os.cpu_count() or 1)
    parser.add_argument('--mod', type=pathlib.Path, action='append', default=[])
    return parser.parse_args(argv)


def archive_mounts(args):
    archives = [args.game / name for name in ('base.scs', 'base_share.scs', 'base_vehicle.scs', 'def.scs', 'locale.scs')]
    archives += sorted(args.game.glob('dlc_*.scs')) + args.mod
    return [part for archive in archives if archive.exists() for part in ('-b', str(archive))]


def extract(converter, mounts, directory, target, optional=False):
    result = subprocess.run([str(converter), *mounts, '--extract-directory', directory, '-e', str(target)],
                            capture_output=True, text=True, encoding='utf8', errors='replace', timeout=300)
    if result.returncode and (not optional or (target / 'locale').exists()):
        raise RuntimeError(result.stdout[-1000:] + result.stderr[-1000:])


def extract_mod_locales(args):
    # Distinct keys from lower-priority mods must survive VFS overlay shadowing.
    for index, archive in enumerate(args.mod):
        extract(args.converter, ['-b', str(archive)], '/locale', args.work / 'mod-files' / f'{index:04d}', optional=True)


def excluded_countries(directory):
    excluded = set()
    for file in (directory.iterdir() if directory.is_dir() else []):
        if not file.is_file() or file.suffix not in ('.sii', '.sui'): continue
        text = file.read_text(encoding='utf8', errors='replace')
        if re.search(r'\bincluded_in_discovery_percentage\s*:\s*false\b', text):
            match = re.search(r'\bcountry_data\s*:\s*([\w.]+)', text)
            excluded.add(match[1].split('.')[-1] if match else file.stem)
    return excluded


def main(argv=None):
    args = parse_args(argv)
    args.work.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    extracted = args.work / 'defs'
    mounts = archive_mounts(args)
    for directory in ('/def/country', '/locale'):
        print('Extracting', directory, flush=True)
        extract(args.converter, mounts, directory, extracted)
    extract_mod_locales(args)
    scripts = pathlib.Path(__file__).parent
    subprocess.run([sys.executable, str(scripts / 'prepare-locales.py'), '--source', str(extracted),
                    '--output', str(args.output)], check=True)
    excluded = excluded_countries(extracted / 'def/country')
    (args.output / 'excluded-countries.tsv').write_text('# Non-career areas\n' + '\n'.join(sorted(excluded)) + '\n', encoding='utf8')
    print('Prepared translations and career areas; native previews need no model index.', flush=True)


if __name__ == '__main__':
    main()
