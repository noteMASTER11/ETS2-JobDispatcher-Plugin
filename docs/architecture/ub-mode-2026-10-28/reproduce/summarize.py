import json,pathlib,re,hashlib
root=pathlib.Path.cwd()
source=pathlib.Path(r'D:\ChatGPT\Projects\ETS2-JobDispatcher-Plugin')
crap=(root/'.metrics/crap.edn').read_text()
pattern=r':name "([^"]+)", :namespace "([^"]+)", :complexity (\d+), :coverage ([\d.]+), :crap ([\d.]+)'
rows=[dict(name=n,namespace=ns,cc=int(cc),coverage=float(cov),crap=float(score)) for n,ns,cc,cov,score in re.findall(pattern,crap)]
runtime=[x for x in rows if x['namespace'] in ('tools.offline-bootstrap','tools.prepare-game-assets','tools.prepare-locales')]
manifest={str(p.relative_to(source)).replace(chr(92),'/'):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('src','tools','tests') for p in sorted((source/folder).rglob('*')) if p.is_file() and p.suffix in ('.cpp','.hpp','.py','.ps1') and '__pycache__' not in p.parts}
(root/'source-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
summary={'python_functions':len(rows),'python_crap_at_least_10':sum(x['crap']>=10 for x in rows),'runtime_python_functions':runtime,'mutation':{'scope':'tools/offline-bootstrap.py only','killed':5,'survived':16,'uncovered':47,'sites':68,'executed':21,'score_percent':100*5/21},'cpp_supported_by_ub_tools':False}
(root/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
