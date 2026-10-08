#!/usr/bin/env python3

import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Classify missing reconstructions.\npython tools/analysis/classify_missing_reconstructions.py [--groundtruth DIR] [--src DIR]')
    raise SystemExit(0)
#
# classify_missing_reconstructions.py - classify uncited functions from
# report_reconstruction_coverage.py using the binary evidence tables.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Classify uncited addresses; these categories measure citation gaps, not
# implementation completeness:
#
#   icf-fold: another Debug function sharing the Retail address is cited.
#   compiler-generated: mapped Retail adjustor-thunk targets or scalar
#     deleting destructors. The latter match size 0x36, cbargs 4, prologue
#     0x55; 40 rows differ only in call displacements (checked at
#     dbg:0x1001EEC0, 0x10088BD0 and 0x1003C360). Plain destructors remain
#     separate. Debug thunk targets are absent from functions-dbg.tsv.
#   not-a-function-head: unaligned 0x8B/0xC7 entries, typically EH resume
#     labels. Verified for 14 A3dSource.cpp rows and sampled at 0x100322cd,
#     0x100332bd, 0x10035632, 0x10035661, 0x1004a7b3 and 0x1004ad77.
#     Sweep sizes can exclude EH tails: 0x100247e0 measures 0x737 versus
#     IDA's 0x787. Unaligned 0x55 entries remain eligible.
#   unproven: listed-only entries that Hex-Rays could not decompile.
#   unmapped: no funcmap.tsv row. An existing unpaired row is still
#     eligible, e.g. resman.cpp 0x1005b8d0/0x1006a1c0/0x1006a260.
#   genuine: remaining unclassified citation gaps; not proof of absent code.
#
# Excluded from the 2,732-row reference scope:
# the CRT (file ends `.c`, or is `dbgdel.cpp`), rows with no region (`file`
# is `-`), and the unattributed 6-row span between Listener.cpp and
# PropertySetItem.cpp, which the source now attributes to NamedObject.cpp.
#
# Inputs under docs/llm/groundtruth/: functions-dbg.tsv, funcmap.tsv,
# vtables-dbg.tsv, vtables-rtl.tsv, unproven.tsv and symbols.tsv. Symbol
# classification recognizes ??_G/??_E and array allocation operators;
# the current Debug symbol table supplies none.
#
# Usage:
#   python tools/analysis/classify_missing_reconstructions.py [--groundtruth DIR] [--src DIR]
#       writes docs/llm/groundtruth/coverage.tsv and prints the per-file and
#       per-bucket totals.

import os, re, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GT   = os.path.join(ROOT, 'docs', 'llm', 'groundtruth')
SRC  = os.path.join(ROOT, 'src', 'a3dapi')

UNNAMED_SPAN = '(unnamed, between Listener.cpp and PropertySetItem.cpp)'


def rows(path):
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		yield line.rstrip('\n').split('\t')


def norm(a):
	return a.strip().lower()


def is_crt(name):
	return name.endswith('.c') or name == 'dbgdel.cpp'


def is_eligible_file(name):
	"""Exclude CRT and unattributed regions from the reference scope."""
	return name not in ('-', UNNAMED_SPAN) and not is_crt(name)


def load(groundtruth):
	gt = groundtruth

	# dbg -> (rtl or None, anchor, fold count).  A dbg address absent from
	# this dict entirely has no funcmap.tsv row at all, distinct from a
	# `rtl` of None, which is a row that IS there but says `unpaired`.
	funcmap = {}
	for r in rows(os.path.join(gt, 'funcmap.tsv')):
		if len(r) >= 5 and r[0].startswith('0x'):
			dbg = norm(r[0])
			rtl = norm(r[1]) if r[1] != '-' else None
			anchor = r[2]
			fold = int(r[4]) if r[4].strip().lstrip('-').isdigit() else 1
			funcmap[dbg] = (rtl, anchor, fold)

	# file -> [(dbg, rtl, corroboration)], every function the binary
	# assigns to that file, eligible files only.
	byfile = collections.defaultdict(list)
	all_by_dbg = {}
	for r in rows(os.path.join(gt, 'functions-dbg.tsv')):
		if len(r) < 8 or not r[0].startswith('0x'):
			continue
		dbg = norm(r[0])
		size = norm(r[1])
		cbargs = r[3].strip()
		firstbyte = norm(r[4])
		corrob = r[6]
		fname = r[7].strip()
		has_row = dbg in funcmap
		rtl, anchor, fold = funcmap.get(dbg, (None, 'unpaired', 0))
		all_by_dbg[dbg] = (fname, corrob, rtl, anchor, fold)
		if is_eligible_file(fname):
			byfile[fname].append((dbg, rtl, corrob, fold, has_row,
					       firstbyte, size, cbargs))

	# rtl -> set of dbg addrs that map to it (fold groups), over ALL
	# functions, not just the eligible ones, since a fold sibling's
	# citation can sit in a neighbouring file.
	rtl2dbgs = collections.defaultdict(set)
	for dbg, (rtl, anchor, fold) in funcmap.items():
		if rtl:
			rtl2dbgs[rtl].add(dbg)

	# Map Retail adjustor-thunk targets to Debug addresses; the Debug function
	# sweep omits the corresponding thunk stubs.
	rtl2dbg_single = {}
	for dbg, (rtl, anchor, fold) in funcmap.items():
		if rtl:
			rtl2dbg_single.setdefault(rtl, []).append(dbg)
	thunk_targets = set()
	for r in rows(os.path.join(gt, 'vtables-dbg.tsv')):
		if len(r) >= 4 and r[3].startswith('thunk:'):
			thunk_targets.add(norm(r[2]))
	for r in rows(os.path.join(gt, 'vtables-rtl.tsv')):
		if len(r) >= 4 and r[3].startswith('thunk:'):
			for dbg in rtl2dbg_single.get(norm(r[2]), ()):
				thunk_targets.add(dbg)

	# Recognize compiler-generated decorated names when symbols.tsv supplies
	# additional E&C entries.
	COMPILER_NAME_RE = re.compile(r'\?\?_[GE]@|\?\?_[UV]@')
	compiler_named = set()
	symbols_path = os.path.join(gt, 'symbols.tsv')
	if os.path.isfile(symbols_path):
		for r in rows(symbols_path):
			if len(r) >= 3 and r[1] == 'sym' and COMPILER_NAME_RE.search(r[2]):
				compiler_named.add(norm(r[0]))

	# addr -> (exported, decompiled) from verify_function_candidates.py's table.
	unproven_checked = {}
	unproven_path = os.path.join(gt, 'unproven.tsv')
	if os.path.isfile(unproven_path):
		for r in rows(unproven_path):
			if len(r) >= 4 and r[0].startswith('0x'):
				unproven_checked[norm(r[0])] = (r[2] == '1', r[3] == '1')

	return byfile, rtl2dbgs, thunk_targets, compiler_named, unproven_checked


def cited_addrs(cpp):
	text = open(cpp, encoding='utf-8', errors='replace').read()
	return set(norm(a) for a in re.findall(r'0x[0-9A-Fa-f]{6,8}', text))


def source_cited_index(src_dir, files):
	"""file -> set of cited addresses, for every eligible file that has a
	source file on disk.  Also a flat union, for the icf-fold cross-file
	search (a sibling's citation can be in any file, not just this one)."""
	per_file = {}
	union = set()
	for f in files:
		cpp = os.path.join(src_dir, f)
		if os.path.isfile(cpp):
			c = cited_addrs(cpp)
			per_file[f] = c
			union |= c
	return per_file, union


EH_TAIL_FIRST_BYTES = ('0x8b', '0xc7')

# MSVC's scalar deleting destructor, by shape.  See the banner.
SCALAR_DELETING_DTOR = ('0x36', '4', '0x55')	# size, cbargs, prologue byte


def is_scalar_deleting_dtor(size, cbargs, firstbyte):
	return (size, cbargs, firstbyte) == SCALAR_DELETING_DTOR


def is_function_head(dbg, firstbyte):
	"""False where functions-dbg.tsv's row is a label inside another
	function rather than a function start.  See the banner's
	not-a-function-head entry for the two signals and how they were
	checked."""
	if int(dbg, 16) % 16 == 0:
		return True
	return firstbyte not in EH_TAIL_FIRST_BYTES


def classify_row(dbg, rtl, corrob, fold, has_funcmap_row, firstbyte, size, cbargs,
		  rtl2dbgs, thunk_targets, compiler_named, unproven_checked,
		  cited_this_file, cited_anywhere):
	# Not a function at all: the sweep found an EH-tail label.  Tested
	# before the fold and thunk rules, since such a row has no meaningful
	# Retail pairing to fold against in the first place.
	if not is_function_head(dbg, firstbyte):
		return 'not-a-function-head'

	# icf-fold: a sibling sharing this row's Retail address is cited
	# somewhere in the tree already.
	if rtl and fold > 1:
		siblings = rtl2dbgs.get(rtl, set())
		for sib in siblings:
			if sib == dbg:
				continue
			if sib in cited_anywhere or rtl in cited_anywhere:
				return 'icf-fold'

	if (dbg in thunk_targets or dbg in compiler_named
			or is_scalar_deleting_dtor(size, cbargs, firstbyte)):
		return 'compiler-generated'

	if corrob == 'listed-only':
		exported, decompiled = unproven_checked.get(dbg, (None, None))
		if exported and not decompiled:
			return 'unproven'

	if not has_funcmap_row and dbg not in cited_anywhere:
		return 'unmapped'

	return 'genuine'


def main():
	groundtruth = GT
	src_dir = SRC
	argv = sys.argv[1:]
	i = 0
	while i < len(argv):
		if argv[i] == '--groundtruth' and i + 1 < len(argv):
			groundtruth = argv[i + 1]; i += 2
		elif argv[i] == '--src' and i + 1 < len(argv):
			src_dir = argv[i + 1]; i += 2
		else:
			i += 1

	byfile, rtl2dbgs, thunk_targets, compiler_named, unproven_checked = load(groundtruth)
	per_file_cited, cited_anywhere = source_cited_index(src_dir, byfile)

	report = []
	bucket_totals = collections.Counter()
	total_present = total_all = 0
	out_rows = []

	for f in sorted(byfile):
		cited_this_file = per_file_cited.get(f, set())
		present = absent = 0
		buckets_here = collections.Counter()
		for dbg, rtl, corrob, fold, has_row, firstbyte, size, cbargs in byfile[f]:
			if dbg in cited_this_file or (rtl and rtl in cited_this_file):
				present += 1
				continue
			absent += 1
			bucket = classify_row(dbg, rtl, corrob, fold, has_row, firstbyte,
					       size, cbargs, rtl2dbgs, thunk_targets,
					       compiler_named, unproven_checked,
					       cited_this_file, cited_anywhere)
			buckets_here[bucket] += 1
			bucket_totals[bucket] += 1
			out_rows.append((f, dbg, rtl or '-', corrob, bucket))
		report.append((f, present, present + absent, buckets_here))
		total_present += present
		total_all += present + absent

	report.sort(key=lambda x: x[3]['genuine'], reverse=True)

	print('file\tpresent/total\tgenuine\ticf-fold\tcompiler-gen\tnot-a-func\tunproven\tunmapped')
	for f, p, t, b in report:
		print('%-24s\t%d/%d\t%d\t%d\t%d\t%d\t%d\t%d' % (
			f, p, t, b['genuine'], b['icf-fold'], b['compiler-generated'],
			b['not-a-function-head'], b['unproven'], b['unmapped']))

	print('\nTOTAL\t%d/%d cited\t%d absent' % (total_present, total_all, total_all - total_present))
	print('buckets:')
	for name in ('genuine', 'icf-fold', 'compiler-generated', 'not-a-function-head',
		     'unproven', 'unmapped'):
		print('  %-20s%d' % (name, bucket_totals[name]))

	note = (
		"Classification of every absent row report_reconstruction_coverage.py reports, over\n"
		"the 2,732-row reference scope (CRT, `-`, and\n"
		"the unnamed 6-row span excluded).  Generated by\n"
		"tools/analysis/classify_missing_reconstructions.py; do not edit by hand, regenerate it.\n"
		"\n"
		"`bucket` is one of: icf-fold, compiler-generated, not-a-function-head,\n"
		"unproven, unmapped, genuine.  See tools/analysis/classify_missing_reconstructions.py's banner\n"
		"for what each means.\n"
		"`genuine` means an unclassified citation gap, not a proven absent body.\n"
		"\n"
		"# file\tdbg\trtl\tcorroboration\tbucket")
	header = ['# ' + line for line in note.split('\n')]
	dest = os.path.join(groundtruth, 'coverage.tsv')
	with open(dest, 'w', encoding='utf-8', newline='\n') as fh:
		fh.write('\n'.join(header) + '\n')
		for f, dbg, rtl, corrob, bucket in out_rows:
			fh.write('%s\t%s\t%s\t%s\t%s\n' % (f, dbg, rtl, corrob, bucket))
	sys.stderr.write('\nwrote %s (%d rows)\n' % (dest, len(out_rows)))

	return 0


if __name__ == '__main__':
	sys.exit(main())
