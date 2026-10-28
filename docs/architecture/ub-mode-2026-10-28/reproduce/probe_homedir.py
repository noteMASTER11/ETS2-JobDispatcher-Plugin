import importlib.util,json,pathlib
root=pathlib.Path.cwd()
spec=importlib.util.spec_from_file_location('audited_bootstrap',root/'tools/offline-bootstrap.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
argument=pathlib.Path(r'D:\ChatGPT\Projects\2026-10-27\tr\work\preview-e2e-home')
actual=argument/'Euro Truck Simulator 2'
game=pathlib.Path(r'D:\ChatGPT\Projects\2026-10-27\tr\work\test-game')
result={'source':'src/offline_assets.cpp:GameHome and tools/offline-bootstrap.py:active_mods',
 'homedir_argument_is_parent':True,'log_at_returned_path_exists':(argument/'game.log.txt').exists(),
 'log_at_actual_home_exists':(actual/'game.log.txt').exists(),
 'mods_found_with_current_GameHome_contract':len(module.active_mods(argument/'game.log.txt',game,argument)),
 'mods_found_with_game_reported_home':len(module.active_mods(actual/'game.log.txt',game,actual))}
(root/'homedir-probe.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps(result,indent=2))
