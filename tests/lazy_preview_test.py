"""Selection isolation, cache hits and bounded eviction without game execution."""
import importlib.util, json, pathlib, subprocess, sys
root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('lazy', root / 'tools/lazy-preview.py')
lazy = importlib.util.module_from_spec(spec); spec.loader.exec_module(lazy)
fixture = pathlib.Path(sys.argv[1]); fixture.mkdir(parents=True, exist_ok=True)
data = fixture / 'data'; output = data / 'previews-lazy'; output.mkdir(parents=True, exist_ok=True)
game = fixture / 'game'; game.mkdir(exist_ok=True); (game / 'base.scs').write_text('fixture')
mod = fixture / 'active.scs'; mod.write_text('fixture')
records = {'cargo.chosen': [['/vehicle/chosen.pmd', 'default']],
           'cargo.untouched': [['/vehicle/untouched.pmd', 'default']],
           'trailer.chosen': [['/vehicle/chosen.pmd', 'default']]}
(output / 'catalogue.json').write_text(json.dumps(records))
lazy.reset(output, 'fixture-v1'); calls = []
def runner(command, **kwargs):
    model = command[command.index('-m') + 1]; calls.append(model)
    assert str(mod) in command
    pim = pathlib.Path(command[command.index('-e') + 1]) / (model.lstrip('/') + '.pim')
    pim.parent.mkdir(parents=True, exist_ok=True); pim.write_text('fixture')
    return subprocess.CompletedProcess(command, 0)
def part(job):
    path = pathlib.Path(job[-1]); path.write_bytes(b'part'); return str(path)
def assemble(job):
    token, components, parts, converted, stage, wheel = job
    assert all(c in parts for c in components)
    name = ('1' if token.startswith('cargo.') else '2') * 24 + '.jdm'
    (pathlib.Path(stage) / name).write_bytes(b'mesh')
    return token, name, ''
lazy.builder.make_part = part; lazy.builder.assemble = assemble
lazy.prepare(game, data, fixture / 'converter.exe', [mod], 'cargo.chosen', runner)
assert calls == ['/vehicle/chosen'], calls
assert 'cargo.untouched' not in lazy.read_entries(output)
lazy.prepare(game, data, fixture / 'converter.exe', [mod], 'cargo.chosen', runner)
assert len(calls) == 1, 'Cache hit invoked converter'
lazy.prepare(game, data, fixture / 'converter.exe', [mod], 'trailer.chosen', runner)
assert calls[1:] == ['/vehicle/chosen', '/vehicle/wheel/trailer/single_385_55/alu'], calls
assert not (output / 'request').exists()
# A failed conversion publishes no partial model or index row.
before = (output / 'index.tsv').read_bytes()
try: lazy.prepare(game, data, fixture / 'converter.exe', [], 'cargo.untouched', lambda *a, **k: subprocess.CompletedProcess(a, 1))
except ValueError: pass
else: raise AssertionError('Unavailable model accepted')
assert (output / 'index.tsv').read_bytes() == before
assert not (output / 'request').exists()
entries = lazy.read_entries(output)
for i in range(12):
    name = f'{i+10:024x}.jdm'; (output / name).write_bytes(b'x' * 10)
    entries[f'cargo.{i}'] = {'mesh': name, 'files': [name], 'used': i}
unrelated = output / 'user-note.txt'; unrelated.write_text('keep')
lazy.MAX_ENTRIES = 3; lazy.MAX_BYTES = 25
lazy.publish(output, entries, 'cargo.11')
assert len(entries) <= 3 and sum(p.stat().st_size for p in output.glob('*.jdm')) <= 25
assert 'cargo.11' in entries and unrelated.read_text() == 'keep'
lazy.reset(output, 'fixture-v2')
assert lazy.read_entries(output) == {} and not list(output.glob('*.jdm'))
legacy = data / 'previews'; legacy.mkdir(exist_ok=True)
(legacy / ('0' * 24 + '.jdm')).write_bytes(b'old')
(legacy / 'my-note.txt').write_text('keep')
lazy.retire_legacy(data)
assert not list(legacy.glob('*.jdm')) and (legacy / 'my-note.txt').read_text() == 'keep'
print('Selected-only conversion, wheel dependencies, cache hit, failure cleanup and bounded eviction passed')
