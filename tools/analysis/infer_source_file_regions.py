
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Infer source file regions.\npython tools/analysis/infer_source_file_regions.py docs/llm/groundtruth/filelines-dbg.tsv')
    raise SystemExit(0)
#
# infer_source_file_regions.py - turn the file/line sites into a region of .text per
# source file.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# In unoptimized builds, object code follows source order and objects
# follow link order. Group each file's first-to-last source-location sites
# and split intervening gaps at the midpoint. Boundary functions without
# such sites may be assigned to the neighboring file.
#
# Two kinds of file are held out of the layout.
#
#   linklist.h	a header, so its inline code is emitted into whichever object
#		includes it.  Its sites are reported against the region they
#		land in rather than given a region of their own.
#   the CRT	*.c and dbgdel.cpp, which are not Aureal's.  They keep their
#		regions, because they bound the Aureal ones.
#
# Usage:
#	python tools/analysis/infer_source_file_regions.py docs/llm/groundtruth/filelines-dbg.tsv
#

import sys
import os
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_image import tsv_header

HEADERS = ('linklist.h',)

# Set false to see what the midpoint rule alone gives, for comparison.
overrides_on = True

#
# Override midpoint boundaries using constructor/vtable evidence.
# Constructors of one class should share a source region. The entries
# below correct 10 boundaries that split their install sites and one
# unreachable by the midpoint rule. Each records the new function boundary
# and supporting evidence.
#
# These are addresses in the Debug build, `ref/a3dapi_33_dbg.dll`.  The
# DebugViewer build lays out differently and keeps the midpoint rule, so its
# table is the unweighted cross-check rather than the working one.
#
# docs/llm/groundtruth/boundaries.tsv carries the full reasoning per boundary.
#
OVERRIDES = {
	# after           before                new start   settled by
	#
	# The first A3d3.cpp:741 assertion omits 19 preceding functions. Extend
	# the region to 0x10007810, paired with Retail's first function
	# (rtl:0x10001000) in funcmap.tsv.
	#
	'A3d3.cpp':		(0x10007810, 'funcmap pairs it with rtl:0x10001000, the first function of Retail .text'),
	'ChunkPage.cpp':	(0x1003c200, 'CA3dMapperSecBuffer is pushed up to 0x1003c195, so apimapper.cpp runs to there'),
	'd2dbuffer.cpp':	(0x10041f10, 'the constructor installing the four ??_7D2DBuffer@@6B... vtables'),
	'A3dRoot.cpp':		(0x1001ce40, 'a vtable installed here and again inside A3dRoot.cpp\'s witnessed range'),
	'A3dList.cpp':		(0x100173c0, 'the constructor whose vtable QueryInterface accepts IID_IA3dList'),
	'refaudbin.cpp':	(0x1003e550, 'the constructor whose member block the refaudbin.cpp:53 assert reads'),
	'rmstatbuffer.cpp':	(0x1006d0d0, 'new(0x8D0) feeds this constructor, and sizeof(ResManStatBuffer) is 0x8D0'),
	'rmstreambuffer.cpp':	(0x100703f0, 'new(0x8EC) feeds this constructor, and sizeof(ResManStreamBuffer) is 0x8EC'),
	'A3dScene.cpp':		(0x1007d090, 'the constructor naming its own subobject "Scene"'),
	'Polygon.cpp':		(0x10085130, 'the frame class, whose member function carries the Polygon.cpp:95 new'),
}

#
# Six unnamed-file functions at 0x1003d6f0..0x1003d8bf implement a
# 256-byte name buffer and SetName/SetNameBuffer/GetNameBuffer. They
# precede CPropertySetItem (ctor 0x1003d8c0, size 0x58). No source-location,
# TRACE or COFF evidence identifies their filename; leave it unassigned.
#
UNPLACED = (
	(0x1003d6f0, 0x1003d8bf, '(unnamed, between Listener.cpp and PropertySetItem.cpp)'),
)


def is_crt(name):
	return name.endswith('.c') or name == 'dbgdel.cpp'


def read(path):
	rows = []
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		addr, kind, fname, lineno, text = (line.rstrip('\n').split('\t') + [''])[:5]
		rows.append((int(addr, 16), kind, fname, int(lineno), text))
	return rows


def main():
	if len(sys.argv) not in (2, 3):
		sys.stderr.write('usage: infer_source_file_regions.py <filelines.tsv>'
				 ' [--no-overrides]\n')
		return 2

	global overrides_on
	overrides_on = '--no-overrides' not in sys.argv

	rows = read(sys.argv[1])
	rows.sort()

	sites = defaultdict(list)
	for addr, kind, fname, lineno, _ in rows:
		sites[fname].append((addr, kind, lineno))

	# Span of each file that gets a region.
	spans = []
	for fname, ss in sites.items():
		if fname in HEADERS:
			continue
		spans.append((min(a for a, _, _ in ss), max(a for a, _, _ in ss), fname))
	spans.sort()

	# Widen each span to the midpoint of the gap to its neighbours, then let
	# the overrides replace the guess where something harder settled it.
	starts, ends = {}, {}
	for i, (lo, hi, fname) in enumerate(spans):
		starts[fname] = lo if i == 0 else (spans[i - 1][1] + lo) // 2 + 1
		ends[fname] = hi if i == len(spans) - 1 else (hi + spans[i + 1][0]) // 2

	applied = []
	for i, (lo, hi, fname) in enumerate(spans):
		if fname not in OVERRIDES or not overrides_on:
			continue
		new_start, why = OVERRIDES[fname]
		applied.append((fname, starts[fname], new_start, why))
		starts[fname] = new_start
		if i:						# the file before it ends here
			ends[spans[i - 1][2]] = new_start - 1

	# A span that belongs to no named file takes its addresses out of
	# whichever region the midpoint rule handed them to.
	for lo, hi, label in (UNPLACED if overrides_on else ()):
		for fname in list(starts):
			if starts[fname] <= lo and ends[fname] >= lo:
				ends[fname] = lo - 1
			if starts[fname] <= hi and ends[fname] >= hi:
				starts[fname] = hi + 1

	regions = [(starts[f], ends[f], lo, hi, f) for lo, hi, f in spans]
	if overrides_on:
		regions += [(lo, hi, lo, hi, label) for lo, hi, label in UNPLACED]
	regions.sort()

	# Interleaving: a site of file A inside file B's region is a boundary that
	# the midpoint rule got wrong, or code that was inlined across objects.
	def owner(addr):
		for start, end, _, _, fname in regions:
			if start <= addr <= end:
				return fname
		return None

	strays = defaultdict(list)
	for fname, ss in sites.items():
		for addr, kind, lineno in ss:
			o = owner(addr)
			if o != fname:
				strays[fname].append((addr, o, lineno))

	note = ('One region of .text per source file, from the sites that name it.\n'
		'`start` and `end` are the span widened to the midpoint of the gap\n'
		'to the neighbouring file; `first` and `last` are the outermost\n'
		'sites themselves, so the difference between them is how much of\n'
		'the region is inferred rather than witnessed.\n'
		'\n'
		'`lines` is the highest line number seen, a lower bound on the\n'
		'length of the file.  `strays` counts sites of another file that\n'
		'land inside this region, which is inlining or a boundary error.\n'
		'\n'
		'linklist.h has no region: it is a header, and its inline code is\n'
		'emitted into whichever object includes it.  The listing below the\n'
		'table says which regions its sites fall in.')

	out = tsv_header(os.path.basename(sys.argv[1]), note)
	out.append('# start\tend\tfirst\tlast\tfile\tsites\tlines\tstrays')

	for start, end, lo, hi, fname in regions:
		if fname not in sites:				# an unplaced span
			out.append('0x%08x	0x%08x	0x%08x	0x%08x	%s	0	0	0'
				   % (start, end, lo, hi, fname))
			continue
		ss = sites[fname]
		nstray = sum(1 for a, o, _ in strays.get(fname, []) if True)
		out.append('0x%08x\t0x%08x\t0x%08x\t0x%08x\t%s\t%d\t%d\t%d'
			   % (start, end, lo, hi, fname, len(ss),
			      max(l for _, _, l in ss), nstray))

	# Where the header's inline code landed.
	for h in HEADERS:
		if h not in sites:
			continue
		by = defaultdict(int)
		for addr, _, _ in sites[h]:
			by[owner(addr)] += 1
		out.append('#')
		out.append('# %s: %d sites, inlined into' % (h, len(sites[h])))
		for fname, n in sorted(by.items(), key=lambda kv: -kv[1]):
			out.append('#   %-24s %d' % (fname, n))

	sys.stdout.write('\n'.join(out) + '\n')

	# A region that ends before it starts means two files' sites interleave,
	# so sorting by first-site put them in an order the midpoint rule cannot
	# separate.  It happens among the CRT's one-site files and says the two
	# cannot be told apart, not that either address is wrong.
	inverted = [r for r in regions if r[1] < r[0]]

	for fname, was, now, why in applied:
		sys.stderr.write('  override: %-20s start 0x%08x -> 0x%08x  (%s)\n'
				 % (fname, was, now, why))

	aureal = [r for r in regions if not is_crt(r[4])]
	sys.stderr.write('%d regions, %d of them Aureal, %d files with strays\n'
			 % (len(regions), len(aureal),
			    sum(1 for f in strays if f not in HEADERS)))
	for start, end, _, _, fname in inverted:
		sys.stderr.write('  inverted: %-16s 0x%08x..0x%08x%s\n'
				 % (fname, start, end,
				    '' if is_crt(fname) else '   <- NOT the CRT'))
	for fname in sorted(strays):
		if fname in HEADERS:
			continue
		got = strays[fname]
		sys.stderr.write('  %-22s %d stray: %s\n'
				 % (fname, len(got),
				    ', '.join('0x%08x in %s' % (a, o) for a, o, _ in got[:3])))
	return 0


if __name__ == '__main__':
	sys.exit(main())
