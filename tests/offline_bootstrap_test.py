import importlib.util, pathlib, sys
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bootstrap',root/'tools/offline-bootstrap.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
fixture=pathlib.Path(sys.argv[1]);game=fixture/'steamapps/common/ETS2';home=fixture/'home'
game.mkdir(parents=True,exist_ok=True);(home/'mod').mkdir(parents=True,exist_ok=True)
for name in ('active','disabled','second'):(home/'mod'/(name+'.scs')).write_text('data',encoding='utf8')
log=home/'game.log.txt'
log.write_text(f'''[fs] device {home/'mod/disabled.scs'} mounted to mod pool.
[mods] Active 2 mods (local: 2, workshop: 0)
[mods] Active local mod active (name: A, version: 1)
[mods] Active local mod second (name: B, version: 1)
[fs] device {home/'mod/active.scs'} mounted to mod pool.
[fs] device {home/'mod/second.scs'} mounted to mod pool.
''',encoding='utf8')
mods=module.active_mods(log,game,home)
assert [p.stem for p in mods]==['active','second'],mods
before=module.fingerprint(game,mods,root/'tools')
(home/'mod/disabled.scs').write_text('disabled changed',encoding='utf8')
assert module.fingerprint(game,mods,root/'tools')==before
(home/'mod/active.scs').write_text('active changed',encoding='utf8')
assert module.fingerprint(game,mods,root/'tools')!=before
with log.open('a',encoding='utf8') as f:f.write('[mods] Active 0 mods (local: 0, workshop: 0)\n')
assert module.active_mods(log,game,home)==[]

def rejected(content, message):
    if content is None:
        log.unlink(missing_ok=True)
    else:
        log.write_text(content, encoding='utf8')
    try:
        module.active_mods(log, game, home)
    except (OSError, RuntimeError):
        return
    raise AssertionError(message)

rejected(None, 'A missing log must not mean an unmodified profile')
rejected('[mods] Preparing profile\n', 'An unfinished log must not mean zero mods')
rejected('[mods] Active 2 mods\n[mods] Active local mod active (name: A)\n',
         'Do not publish a partially written active-mod list')
rejected('[mods] Active 1 mods\n[mods] Active local mod missing (name: Missing)\n',
         'Every active archive must resolve before preparing the cache')

outside=fixture/'outside'/'external.scs'; outside.parent.mkdir(exist_ok=True)
outside.write_text('external',encoding='utf8')
workshop=game.parent.parent/'workshop/content/227300/1234'
workshop.mkdir(parents=True,exist_ok=True)
for name in ('a.scs','b.zip'):(workshop/name).write_text(name,encoding='utf8')
log.write_text(f'''[mods] Active 1 mods
[mods] Active local mod disabled (name: Old profile)
[mods] Active 3 mods
[mods] Active workshop mod ID 1234 (name: Workshop)
[mods] Active local mod external (name: External)
[mods] Active local mod active (name: A)
[fs] device {outside} mounted to mod pool.
''',encoding='utf8')
assert module.active_mods(log,game,home)==[
    (workshop/'a.scs').resolve(),(workshop/'b.zip').resolve(),outside.resolve(),
    (home/'mod/active.scs').resolve()]
with log.open('a',encoding='utf8') as stream:
    stream.write(f'[fs] device {outside} mounted to mod pool.\n')
assert module.active_mods(log,game,home).count(outside.resolve())==1
rejected('[mods] Active 1 mods\n[mods] Active workshop mod ID 9999 (name: Missing)\n',
         'A missing Workshop package must not be silently omitted')
print('Active profile selection, complete discovery, mod order and cache invalidation passed')
