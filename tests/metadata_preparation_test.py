"""Real metadata pipeline with a fixture adapter for the external converter."""
import contextlib, importlib, importlib.util, io, json, os, pathlib, runpy, shutil, subprocess, sys, tempfile
from types import SimpleNamespace
from unittest.mock import patch

root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bootstrap',root/'tools/offline-bootstrap.py')
bootstrap=importlib.util.module_from_spec(spec);spec.loader.exec_module(bootstrap)
fixture=pathlib.Path(sys.argv[1]);game=fixture/'game';home=fixture/'home';data=fixture/'data'
for path in (game,home/'mod',data):path.mkdir(parents=True,exist_ok=True)
converter=fixture/'converter.exe';shutil.copyfile(root/'runtime/converter_pix.exe',converter)
mod=home/'mod/map.scs';mod.write_text('map',encoding='utf8')
(home/'game.log.txt').write_text('[mods] Active 1 mods\n[mods] Active local mod map (name: Map)\n',encoding='utf8')
(data/'prepared-assets.json').unlink(missing_ok=True)
calls=[]
conversion_error=False

def run_script(command):
    with patch.object(sys,'argv',command[1:]):
        try:return runpy.run_path(command[1],run_name='__main__')
        except SystemExit as error:
            if error.code:raise

def external(command,**options):
    if command[0]!=str(converter):
        if pathlib.Path(command[1]).name=='prepare-game-assets.py':
            assert int(command[command.index('--workers')+1])>=1
        try:run_script(command)
        except RuntimeError:
            if options.get('check'):raise subprocess.CalledProcessError(1,command)
            return SimpleNamespace(returncode=1)
        return SimpleNamespace(returncode=0)
    calls.append(command)
    if conversion_error:return SimpleNamespace(returncode=1,stdout='fixture extraction failed',stderr='')
    output=pathlib.Path(command[command.index('-e')+1])
    directory=command[command.index('--extract-directory')+1]
    if directory=='/def/country':
        target=output/'def/country';target.mkdir(parents=True,exist_ok=True)
        (target/'event.sii').write_text('country_data : country.event { included_in_discovery_percentage: false }',encoding='utf8')
        (target/'normal.sii').write_text('country_data : country.normal { included_in_discovery_percentage: true }',encoding='utf8')
    elif output.name=='defs':
        target=output/'locale/en_gb';target.mkdir(parents=True,exist_ok=True)
        (target/'local.sii').write_text('@include "shared.sui"\nkey[]: "cargo"\nval[]: "Base"\n',encoding='utf8')
        (target/'shared.sui').write_text('key[]: "city"\nval[]: "City\\nName"',encoding='utf8')
        (target/'local.override.sii').write_text('key[]: "cargo"\nval[]: "Override"',encoding='utf8')
    else:
        target=output/'locale/en_gb';target.mkdir(parents=True,exist_ok=True)
        (target/'mod.sui').write_text('key[]: "cargo"\nval[]: "Mod cargo"',encoding='utf8')
    return SimpleNamespace(returncode=0,stdout='',stderr='')

with patch.object(subprocess,'run',side_effect=external):
    bootstrap.prepare(game,data,home,converter,2)
    assert len(calls)==3,calls
    assert 'cargo\tMod cargo\n' in (data/'game-labels.en_gb.tsv').read_text(encoding='utf8')
    assert 'city\tCity Name\n' in (data/'game-labels.en_gb.tsv').read_text(encoding='utf8')
    assert (data/'excluded-countries.tsv').read_text(encoding='utf8')=='# Non-career areas\nevent\n'
    marker=json.loads((data/'prepared-assets.json').read_text(encoding='utf8'))
    assert marker['mods']==[str(mod.resolve())] and marker['preview']=='native'
    assert not (data/'preparation'/marker['fingerprint'][:24]).exists()
    bootstrap.prepare(game,data,home,converter,2)
    assert len(calls)==3,'A current cache must not extract again'
    (data/'game-labels.en_gb.tsv').unlink()
    bootstrap.prepare(game,data,home,converter,2)
    assert len(calls)==6,'A cache with missing outputs must be rebuilt'
    (data/'prepared-assets.json').write_text('interrupted marker',encoding='utf8')
    bootstrap.prepare(game,data,home,converter,2)
    assert len(calls)==9
    (data/'offline-settings.json').write_text('{"workers": 2, "extra_mods": []}',encoding='utf8')
    arguments=['offline-bootstrap.py','--game',str(game),'--data',str(data),'--game-home',str(home),'--converter',str(converter)]
    with patch.object(sys,'argv',arguments),patch.dict(os.environ):
        assert bootstrap.main()==0
        contents=(home/'game.log.txt').read_text(encoding='utf8')
        (home/'game.log.txt').unlink()
        try:assert bootstrap.main()==1,'Missing profile log must fail the worker'
        finally:(home/'game.log.txt').write_text(contents,encoding='utf8')
    assert len(calls)==9
    # The entry point must handle a fresh nested directory and settings that
    # change workers and add archives, not merely call an already warm cache.
    with tempfile.TemporaryDirectory(prefix='bootstrap-cli-',dir=fixture) as fresh:
        fresh_data=pathlib.Path(fresh)/'nested/data'
        fresh_arguments=arguments.copy();fresh_arguments[fresh_arguments.index('--data')+1]=str(fresh_data)
        fresh_arguments+=['--workers','0']
        with patch.object(sys,'argv',fresh_arguments),patch.dict(os.environ):
            assert bootstrap.main()==0
            extra=fixture/'extra.scs';extra.write_text('extra',encoding='utf8')
            (fresh_data/'offline-settings.json').write_text(json.dumps({'workers':0,'extra_mods':[str(extra)]}),encoding='utf8')
            assert bootstrap.main()==0
            prepared=json.loads((fresh_data/'prepared-assets.json').read_text(encoding='utf8'))
            assert prepared['mods']==[str(mod.resolve()),str(extra.resolve())]
            assert os.environ['TEMP']==str(fresh_data/'temp') and os.environ['TMP']==os.environ['TEMP']
            (fresh_data/'no_cache_probe.py').write_text('value=42',encoding='utf8')
            with patch.object(sys,'path',[str(fresh_data),*sys.path]):
                assert importlib.import_module('no_cache_probe').value==42
            sys.modules.pop('no_cache_probe',None)
            assert not (fresh_data/'__pycache__').exists()
        for flag in ('--game','--data','--game-home','--converter'):
            incomplete=fresh_arguments.copy();index=incomplete.index(flag);del incomplete[index:index+2]
            with patch.object(sys,'argv',incomplete),contextlib.redirect_stderr(io.StringIO()):
                try:bootstrap.main()
                except SystemExit as error:assert error.code==2
                else:raise AssertionError('A missing required argument was accepted: '+flag)
    conversion_error=True
    (data/'prepared-assets.json').unlink()
    try:bootstrap.prepare(game,data,home,converter,0)
    except subprocess.CalledProcessError:pass
    else:raise AssertionError('A failed preparation subprocess was accepted')
    assert not (data/'prepared-assets.json').exists()
    conversion_error=False
    converter.write_bytes(b'corrupted')
    (data/'prepared-assets.json').unlink(missing_ok=True)
    try:bootstrap.prepare(game,data,home,converter,2)
    except RuntimeError as error:assert 'checksum' in str(error)
    else:raise AssertionError('A changed converter was accepted')
    assert not (data/'prepared-assets.json').exists()
print('Metadata extraction, locale includes/overrides, mod translations and cache recovery passed')

# Exercise the actual locale decoder's SCS escape syntax and include failures.
source=fixture/'locale-source';locale=source/'locale/en_gb'
locale.mkdir(parents=True,exist_ok=True)
(locale/'local.sii').write_text('key[]: "escape"\nval[]: "\\x41\\tB"\n',encoding='utf8')
result=run_script([sys.executable,str(root/'tools/prepare-locales.py'),'--source',str(source),'--output',str(data)])
assert (data/'game-labels.en_gb.tsv').read_text(encoding='utf8')=='escape\tA B\n'
(locale/'local.sii').write_text('@include "local.sii"',encoding='utf8')
try:run_script([sys.executable,str(root/'tools/prepare-locales.py'),'--source',str(source),'--output',str(data)])
except ValueError as error:assert 'cycle' in str(error)
else:raise AssertionError('A locale include cycle was accepted')
