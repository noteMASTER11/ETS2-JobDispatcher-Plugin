"""Keep existing fixture assertions, but resolve the mutated project from cwd.
Mutator links tests/; using the linked test's resolved __file__ would import
unmutated tools from the original snapshot instead of the worker overlay.
"""
import pathlib,sys
root=pathlib.Path.cwd()
for name in ('offline_bootstrap_test.py','metadata_preparation_test.py'):
    test=root/'tests'/name
    sys.argv=[str(test),str(root/'target'/('mutation-'+name))]
    namespace={'__name__':'__main__','__file__':str(root/'_virtual-tests'/name)}
    exec(compile(test.read_text(encoding='utf8'),str(test),'exec'),namespace)
    loaded=namespace.get('module',namespace.get('bootstrap'))
    assert pathlib.Path(loaded.__file__).resolve().is_relative_to(root.resolve()),'Fixture imported the unmutated tree'
