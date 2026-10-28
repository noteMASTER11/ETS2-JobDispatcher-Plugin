"""Keep existing fixture assertions, but resolve the mutated project from cwd.
Mutator links tests/; using the linked test's resolved __file__ would import
unmutated tools from the original snapshot instead of the worker overlay.
"""
import pathlib,sys
root=pathlib.Path.cwd()
test=root/'tests/offline_bootstrap_test.py'
sys.argv=[str(test),str(root/'target/mutation-fixture')]
namespace={'__name__':'__main__','__file__':str(root/'_virtual-tests/offline_bootstrap_test.py')}
exec(compile(test.read_text(encoding='utf8'),str(test),'exec'),namespace)
assert pathlib.Path(namespace['module'].__file__).resolve().is_relative_to(root.resolve()),'Fixture imported the unmutated tree'
