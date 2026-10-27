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
print('Active profile selection, mod order and cache invalidation passed')
