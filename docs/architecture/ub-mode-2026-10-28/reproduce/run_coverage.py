import pathlib,runpy,sys,coverage
root=pathlib.Path(__file__).resolve().parent
cov=coverage.Coverage(data_file=str(root/'.coverage'),source=[str(root/'tools')],branch=True)
cov.start()
for name in ('offline_bootstrap_test.py','preview_scene_test.py','preview_catalogue_test.py','lazy_preview_test.py'):
    sys.argv=[str(root/'tests'/name),str(root/'target'/'audit-fixtures'/name.removesuffix('.py'))]
    runpy.run_path(sys.argv[0],run_name='__main__')
cov.stop();cov.save()
output=root/'target/coverage/python';output.mkdir(parents=True,exist_ok=True)
cov.lcov_report(outfile=str(output/'lcov.info'))
cov.json_report(outfile=str(root/'coverage.json'))
cov.report(skip_covered=False)
