import importlib,sys
import tree_sitter_language_pack as languages
languages.configure({'cache_dir':r'D:\ChatGPT\Cache\ub-mode\grammars','languages':['python']})
module=importlib.import_module(sys.argv[1]+'.cli')
module.main(sys.argv[2:])
