"""Lists interface strings used in the sources and missing from the table (and unused entries)."""
import re, sys, glob
import os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from table import T
root = os.path.join(HERE, '..', '..') + os.sep
files = glob.glob(root + 'src/**/*.c', recursive=True)
pat = re.compile(r'\b(?:tr|N_)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')
lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
used = {}
for f in files:
    if f.endswith('i18n.c'): continue
    src = open(f, encoding='utf-8').read()
    for m in pat.finditer(src):
        s = ''.join(lit.findall(m.group(1)))
        used.setdefault(s, f.replace(root, ''))
# strings translated indirectly (arrays stored in English)
missing = [k for k in used if k not in T]
unused = [k for k in T if k not in used]
mode = sys.argv[1] if len(sys.argv) > 1 else 'missing'
if mode == 'missing':
    for k in missing: print(repr(k), '   #', used[k])
    print(len(missing), 'missing;', len(unused), 'unused;', len(used), 'used')
elif mode == 'unused':
    for k in unused: print(repr(k))
