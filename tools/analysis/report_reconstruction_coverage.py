#!/usr/bin/env python3

import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Report reconstruction coverage.\npython tools/analysis/report_reconstruction_coverage.py [source-file.cpp]\nReads source annotations and docs/llm/groundtruth/funcmap.tsv; prints citation counts.')
    raise SystemExit(0)
#
# Citation coverage: per-file body coverage against the 677 Debug binary.
#
# NOT PART OF THE ORIGINAL.  Project tooling.  Check whether each 677 Debug function
# has a Debug or mapped Retail address cited in its assigned .cpp file.
# PRESENT means the citation exists; ABSENT means it was not found.
# Neither result establishes implementation completeness.
#
# Inputs, all generated from the binaries by the other scripts:
#   docs/llm/groundtruth/functions-dbg.tsv  every Debug function, col1 addr, col8 file
#   docs/llm/groundtruth/funcmap.tsv        Debug addr -> Retail addr
# and the reconstructed sources under src/a3dapi/.
#
# ICF and shared thunks fold several Debug functions onto one Retail address, so
# a function is present if EITHER its own dbg: or its rtl: is cited.

import os, re, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GT   = os.path.join(ROOT, 'docs', 'llm', 'groundtruth')
SRC  = os.path.join(ROOT, 'src', 'a3dapi')

def rows(path):
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		yield line.rstrip('\n').split('\t')

def norm(a):
	return a.strip().lower()

# Debug addr -> Retail addr (may be missing / unpaired).
dbg2rtl = {}
for r in rows(os.path.join(GT, 'funcmap.tsv')):
	if len(r) >= 2 and r[0].startswith('0x'):
		dbg2rtl[norm(r[0])] = norm(r[1])

# file -> list of (dbg, rtl) the binary assigns to it.
byfile = collections.defaultdict(list)
for r in rows(os.path.join(GT, 'functions-dbg.tsv')):
	if len(r) < 8 or not r[0].startswith('0x'):
		continue
	dbg  = norm(r[0])
	f    = r[7].strip()
	byfile[f].append((dbg, dbg2rtl.get(dbg)))

# Cited addresses per source file (any 0x... after dbg:/rtl:/dbgv:/678: tags,
# and any bare 0x... too, since some banners cite the address inline).
def cited_addrs(cpp):
	text = open(cpp, encoding='utf-8', errors='replace').read()
	return set(norm(a) for a in re.findall(r'0x[0-9A-Fa-f]{6,8}', text))

total_present = total_all = 0
report = []
for f in sorted(byfile):
	cpp = os.path.join(SRC, f)
	if not os.path.isfile(cpp):
		report.append((f, 0, len(byfile[f]), ['(no source file)']))
		total_all += len(byfile[f])
		continue
	cited = cited_addrs(cpp)
	present = absent = 0
	miss = []
	for dbg, rtl in byfile[f]:
		if dbg in cited or (rtl and rtl in cited):
			present += 1
		else:
			absent += 1
			miss.append(dbg + (('/' + rtl) if rtl else '/-'))
	report.append((f, present, present + absent, miss))
	total_present += present
	total_all += present + absent

report.sort(key=lambda x: (x[2] - x[1]), reverse=True)
print('file\tpresent/total\tabsent')
for f, p, t, miss in report:
	print('%-24s\t%d/%d\t%d' % (f, p, t, t - p))
print('\nTOTAL\t%d/%d cited\t%d absent' % (total_present, total_all, total_all - total_present))

if len(sys.argv) > 1:
	want = sys.argv[1]
	for f, p, t, miss in report:
		if f == want:
			print('\n-- absent in %s --' % f)
			for m in miss:
				print(m)
