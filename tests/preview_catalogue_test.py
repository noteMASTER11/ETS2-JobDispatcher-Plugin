"""Regression: several trailer units in one SUI retain separate geometry."""
import importlib.util, pathlib, sys

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('preview_builder', root / 'tools/prepare-textured-previews.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
fixture = pathlib.Path(sys.argv[1])
vehicle = fixture / 'def/vehicle'
for name in ('trailer', 'trailer_owned', 'cargo'):
    (vehicle / name).mkdir(parents=True, exist_ok=True)
(vehicle / 'trailer/a.sii').write_text('model: "/vehicle/a.pmd"\nvariant: "short"', encoding='utf8')
(vehicle / 'trailer/b.sii').write_text('model: "/vehicle/b.pmd"\nvariant: "long"', encoding='utf8')
(vehicle / 'trailer/multiple.sui').write_text('''
trailer : trailer.short {
  trailer_definition: trailer_def.short
  accessories[]: .short.chassis
}
vehicle_accessory : .short.chassis { data_path: "/def/vehicle/trailer/a.sii" }
trailer : trailer.long {
  trailer_definition: trailer_def.long
  accessories[]: .long.chassis
}
vehicle_accessory : .long.chassis { data_path: "/def/vehicle/trailer/b.sii" }
''', encoding='utf8')
result = module.collect(fixture)
assert result['trailer.short'] == [('/vehicle/a.pmd', 'short')], result
assert result['trailer.long'] == [('/vehicle/b.pmd', 'long')], result
assert result['trailer_def.short'] == result['trailer.short']
assert result['trailer_def.long'] == result['trailer.long']
print('Separate trailer units, accessory references and definition aliases passed')
