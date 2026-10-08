#!/usr/bin/env python3

import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Compare assertion sites.\npython tools/analysis/extract_filelines.py build/Debug/a3dapi.dll > ours.tsv\npython tools/analysis/compare_assertion_sites.py ours.tsv')
    raise SystemExit(0)
#
# compare_assertion_sites.py - compare a rebuilt Debug binary's own extracted
# asserts (tools/analysis/extract_filelines.py's output) against
# docs/llm/groundtruth/filelines-dbg.tsv, and report the real reproduction rate.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Match by case-insensitive filename and normalized expression because
# reconstructed source line numbers differ from Aureal's.
#
# Normalize whitespace, decompiler DWORD/hex formatting, redundant outer
# parentheses, DSBUFFERDESC1 aliases and named SDK constants
# (WAIT_OBJECT_0, WAIT_TIMEOUT and DirectSound OUTPUT_* values).
# Preserve != NULL versus != 0: they produce different diagnostic text.
#
# Usage:
#	python tools/analysis/extract_filelines.py build/Debug/a3dapi.dll > ours.tsv
#	python tools/analysis/compare_assertion_sites.py ours.tsv
#

import sys
import os
import re
from collections import defaultdict


def strip_outer_parens(s):
	while s.startswith('(') and s.endswith(')'):
		depth = 0
		ok = True
		for i, c in enumerate(s):
			if c == '(':
				depth += 1
			elif c == ')':
				depth -= 1
				if depth == 0 and i != len(s) - 1:
					ok = False
					break
		if ok:
			s = s[1:-1].strip()
		else:
			break
	return s


def norm(s):
	s = s.strip()
	s = re.sub(r'\s+', ' ', s)
	# The decompiler's rendering of WAIT_OBJECT_0 and WAIT_TIMEOUT, applied
	# before the general hex-constant fold below so their exact digit width
	# doesn't matter.
	s = re.sub(r'\(\(\(\(DWORD \)0x0+L\)\s*\)\s*\+\s*0\s*\)', 'WAIT_OBJECT_0', s)
	s = re.sub(r'\(\(DWORD \)0x0*102L\)', 'WAIT_TIMEOUT', s)
	s = re.sub(r'0x0*ffffffffL?\b', 'INFINITE', s, flags=re.IGNORECASE)
	s = s.replace('((DWORD )', '(DWORD)')
	s = re.sub(r'0x0*([0-9A-Fa-f]+)', lambda m: hex(int(m.group(1), 16)), s)
	s = s.replace(' L', 'L')
	s = strip_outer_parens(s)
	s = s.replace('DSBUFFERDESC)', 'DSBUFFERDESC1)')	# CLAUDE.md-mandated
	s = re.sub(r'(\w) \*\)', r'\1*)', s)			# "char *)" vs "char*)"
	s = re.sub(r'\s*,\s*', ',', s)				# comma spacing
	s = re.sub(r'\s*(&&|\|\|)\s*', r'\1', s)		# &&/|| spacing
	s = re.sub(r'\b0x0\b', '0', s)				# 0x0 vs plain 0
	return s


def load(path):
	rows = []
	with open(path, encoding='utf-8') as f:
		for line in f:
			if line.startswith('#') or not line.strip():
				continue
			parts = line.rstrip('\n').split('\t')
			if len(parts) < 5:
				continue
			addr, kind, file, ln, text = parts[0], parts[1], parts[2], parts[3], parts[4]
			if kind != 'assert':
				continue
			rows.append((file.lower(), ln, text))
	return rows


def main():
	if len(sys.argv) != 2:
		print(f"usage: {sys.argv[0]} <ours.tsv>", file=sys.stderr)
		return 1

	root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
	gt = load(os.path.join(root, 'docs', 'llm', 'groundtruth', 'filelines-dbg.tsv'))
	ours = load(sys.argv[1])

	by_file_gt = defaultdict(list)
	for file, ln, text in gt:
		by_file_gt[file].append((ln, text))

	by_file_ours = defaultdict(list)
	for file, ln, text in ours:
		by_file_ours[file].append((ln, text))

	matched = 0
	unmatched = []
	for file, ln, text in gt:
		nt = norm(text)
		hit = any(norm(t2) == nt for ln2, t2 in by_file_ours.get(file, []))
		if hit:
			matched += 1
		else:
			unmatched.append((file, ln, text))

	print(f"matched (file+text): {matched}/{len(gt)}  ({100.0 * matched / len(gt):.1f}%)")

	by_file_unmatched = defaultdict(list)
	for file, ln, text in unmatched:
		by_file_unmatched[file].append((ln, text))

	print("\nunmatched by file:")
	for file, rows in sorted(by_file_unmatched.items(), key=lambda x: -len(x[1])):
		total = len(by_file_gt[file])
		print(f"  {file}: {len(rows)}/{total} unmatched")

	print("\nunmatched rows:")
	for file, rows in sorted(by_file_unmatched.items()):
		for ln, text in rows:
			print(f"  {file}:{ln}\t{text}")

	return 0


if __name__ == '__main__':
	sys.exit(main())
