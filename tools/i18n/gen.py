"""Regenerates the translation table of src/i18n.c from table.py (+ table2.py).

Only strings that the sources really pass to tr() or mark with N_() are
written, in a stable order; the code around the table is kept as it is."""
import re, sys, glob
import os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from table import T
root = os.path.join(HERE, '..', '..') + os.sep
pat = re.compile(r'\b(?:tr|N_)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')
lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
used = []
for f in sorted(glob.glob(root + 'src/**/*.c', recursive=True)):
    if f.endswith('i18n.c'):
        continue
    for m in pat.finditer(open(f, encoding='utf-8').read()):
        s = ''.join(lit.findall(m.group(1)))
        if s not in used:
            used.append(s)
missing = [s for s in used if s not in T]
if missing:
    print('MISSING TRANSLATIONS:')
    for s in missing:
        print('  ', repr(s))
    sys.exit(1)
path = root + 'src/i18n.c'
src = open(path, encoding='utf-8').read()
head, rest = src.split('static const tr_entry table[] = {\n', 1)
tail = rest[rest.index('\n};\n'):]
def c(s):
    return '"' + s + '"'
rows = ['  {%s,\n   %s,\n   %s},' % (c(en), c(T[en][0]), c(T[en][1])) for en in used]
open(path, 'w', encoding='utf-8').write(head + 'static const tr_entry table[] = {\n' + '\n'.join(rows) + tail)
print(len(used), 'entries written')
# sanity: printf directives must match across languages
spec = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXcsfgeEp])')
bad = 0
for en in used:
    for t in T[en]:
        if spec.findall(t) != spec.findall(en):
            print('FORMAT MISMATCH', repr(en), repr(t)); bad += 1
        if '"' in t.replace('\\"', ''):
            print('RAW QUOTE', repr(t)); bad += 1
sys.exit(1 if bad else 0)
