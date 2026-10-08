
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Extract labels.\npython tools/analysis/extract_labels.py ref/a3dapi_33_dbg.dll docs/llm/groundtruth/regions-dbg.tsv > out.tsv')
    raise SystemExit(0)
#
# extract_labels.py - the TRACE `Class::Method` labels, and the file each one
# is pushed from.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# TRACE Class::Method strings identify methods. Their push-site regions
# associate classes with source files.
#
# The region a push falls in comes from tools/analysis/infer_source_file_regions.py.
#
# Usage:
#	python tools/analysis/extract_labels.py ref/a3dapi_33_dbg.dll \
#	       docs/llm/groundtruth/regions-dbg.tsv > out.tsv
#

import sys
import os
import re
import struct
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_image import PE, tsv_header

# Search within NUL-terminated diagnostics such as
# "CA3dRoot::SetOutputMode() - Function not supported". Record whether
# the label starts the string; only that position is directly pushed.
STRING = re.compile(rb'[ -~\t\r\n]{6,300}\x00')
LABEL = re.compile(r'[A-Za-z_][A-Za-z0-9_]{2,40}::[~A-Za-z_][A-Za-z0-9_]{1,40}')


def read_regions(path):
	out = []
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		f = line.rstrip('\n').split('\t')
		out.append((int(f[0], 16), int(f[1], 16), f[4]))
	return sorted(out)


def main():
	if len(sys.argv) != 3:
		sys.stderr.write('usage: extract_labels.py <image> <regions.tsv>\n')
		return 2

	pe = PE(sys.argv[1])
	regions = read_regions(sys.argv[2])

	def owner(va):
		for start, end, fname in regions:
			if start <= va <= end:
				return fname
		return ''

	# Every string in a read-only or data section that carries a label.
	found = []
	for m in STRING.finditer(pe.data):
		rva = pe.off_to_rva(m.start())
		if rva is None:
			continue
		sec = pe.section_of_rva(rva)
		if not sec or sec['name'] not in ('.rdata', '.data'):
			continue
		text = m.group()[:-1].decode('ascii')
		lm = LABEL.search(text)
		if not lm:
			continue
		found.append((pe.va(rva), text, lm.group(), lm.start() == 0))

	# Every push of a string's own address, and the region it lands in.
	rows = []
	for va, text, label, heads in found:
		sites = []
		for m in re.finditer(re.escape(struct.pack('<I', va)), pe.data):
			o = m.start()
			if pe.data[o - 1] != 0x68:		# push imm32
				continue
			site = pe.off_to_rva(o - 1)
			if site is None:
				continue
			sec = pe.section_of_rva(site)
			if not sec or sec['name'] != '.text':
				continue
			sites.append(pe.va(site))
		cls, _, method = label.partition('::')
		if not sites:
			rows.append((va, cls, method, 0, '', heads))
		for s in sorted(sites):
			rows.append((va, cls, method, s, owner(s), heads))

	rows.sort(key=lambda r: (r[1], r[2], r[3]))

	note = ('Every TRACE `Class::Method` label, and each address that pushes\n'
		'it.  `file` is the region that push falls in, from\n'
		'tools/analysis/infer_source_file_regions.py, so it names the file by code rather\n'
		'than by resemblance to the class name.\n'
		'\n'
		'`heads` says the label starts its string rather than sitting\n'
		'inside a longer diagnostic.  Only a heading label has an address\n'
		'of its own for anything to push, so a row with `push` 0 is\n'
		'normally one that does not head its string.')

	out = tsv_header(os.path.basename(sys.argv[1]), note)
	out.append('# string\tclass\tmethod\theads\tpush\tfile')
	for va, cls, method, site, fname, heads in rows:
		out.append('0x%08x\t%s\t%s\t%s\t%s\t%s'
			   % (va, cls, method, 'y' if heads else 'n',
			      '0x%08x' % site if site else '0', fname))
	sys.stdout.write('\n'.join(out) + '\n')

	# Which file owns each class, by where its labels are pushed from.
	byclass = defaultdict(lambda: defaultdict(int))
	for _, cls, _, site, fname, _h in rows:
		if site and fname:
			byclass[cls][fname] += 1

	sys.stderr.write('%d labels, %d classes, %d push sites\n'
			 % (len(set((r[1], r[2]) for r in rows)), len(byclass),
			    sum(1 for r in rows if r[3])))
	for cls in sorted(byclass):
		where = sorted(byclass[cls].items(), key=lambda kv: -kv[1])
		total = sum(n for _, n in where)
		best, n = where[0]
		flag = '' if n == total else '   (also %s)' % ', '.join(
			'%s x%d' % (f, c) for f, c in where[1:])
		sys.stderr.write('  %-24s %-22s %d/%d%s\n'
				 % (cls, best, n, total, flag))
	return 0


if __name__ == '__main__':
	sys.exit(main())
