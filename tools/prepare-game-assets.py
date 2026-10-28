"""Create local preview and label caches from the user's installed ETS2 files."""
import argparse, importlib.util, json, os, pathlib, re, subprocess, sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=pathlib.Path, required=True)
    parser.add_argument('--converter', type=pathlib.Path, required=True)
    parser.add_argument('--work', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--workers', type=int, default=os.cpu_count() or 1)
    parser.add_argument('--mod', type=pathlib.Path, action='append', default=[])
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    archives = [args.game / name for name in ('base.scs', 'base_share.scs', 'base_vehicle.scs', 'def.scs', 'locale.scs')]
    archives += sorted(args.game.glob('dlc_*.scs'))
    archives += args.mod
    mounts = [part for archive in archives if archive.exists() for part in ('-b', str(archive))]
    extracted = args.work / 'defs'
    for directory in ('/def/vehicle/trailer', '/def/vehicle/trailer_defs', '/def/vehicle/trailer_owned',
                      '/def/vehicle/cargo', '/def/vehicle/t_wheel', '/def/country', '/locale'):
        print('Extracting', directory, flush=True)
        result = subprocess.run([str(args.converter), *mounts, '--extract-directory', directory, '-e', str(extracted)],
                                capture_output=True, text=True, encoding='utf8', errors='replace', timeout=300)
        if result.returncode:
            raise RuntimeError(result.stdout[-1000:] + result.stderr[-1000:])
    scripts = pathlib.Path(__file__).parent
    # Localization databases are additive across active packages. Reading only
    # the final VFS overlay would lose lower-priority mods' distinct strings.
    for index, archive in enumerate(args.mod):
        target=args.work/'mod-files'/f'{index:04d}'
        result=subprocess.run([str(args.converter),'-b',str(archive),'--extract-directory','/locale','-e',str(target)],
                              capture_output=True,text=True,encoding='utf8',errors='replace',timeout=300)
        # Most definition/map packages contain no /locale tree.
        if result.returncode and (target/'locale').exists():
            raise RuntimeError(result.stdout[-1000:]+result.stderr[-1000:])
    subprocess.run([sys.executable, str(scripts / 'prepare-locales.py'), '--source', str(extracted),
                    '--output', str(args.output)], check=True)
    excluded = set()
    for file in (extracted / 'def/country').iterdir():
        if not file.is_file() or file.suffix not in ('.sii', '.sui'): continue
        text = file.read_text(encoding='utf8', errors='replace')
        if re.search(r'\bincluded_in_discovery_percentage\s*:\s*false\b', text):
            match = re.search(r'\bcountry_data\s*:\s*([\w.]+)', text)
            excluded.add(match[1].split('.')[-1] if match else file.stem)
    (args.output / 'excluded-countries.tsv').write_text('# Non-career areas\n' + '\n'.join(sorted(excluded)) + '\n', encoding='utf8')
    # Only definitions are indexed here. No geometry, materials or textures are
    # converted until the user actually selects a preview in the overlay.
    spec = importlib.util.spec_from_file_location('preview_builder', scripts / 'prepare-textured-previews.py')
    builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
    records = builder.collect(extracted)
    previews = args.output / 'previews-lazy'; previews.mkdir(exist_ok=True)
    (previews / 'catalogue.json').write_text(json.dumps(records), encoding='utf8')
    (previews / 'catalogue.tsv').write_text(''.join(token + '\n' for token in sorted(records)), encoding='utf8')
    print('Indexed', len(records), 'preview definitions; converted 0 models.', flush=True)


if __name__ == '__main__':
    main()
