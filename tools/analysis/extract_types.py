
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Extract types.\npython tools/analysis/extract_types.py ref/a3dapi_33_dbg.dll docs/llm/groundtruth/filelines-dbg.tsv > out.tsv')
    raise SystemExit(0)
#
# extract_types.py - every type name that appears verbatim in a debug build,
# from a `sizeof()` inside an ASSERT and from an RTTI type descriptor.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Read sizeof operands from ASSERT rows in filelines-<build>.tsv.
# Scan the image for MSVC RTTI names .?AVClassName@@ / .?AUStructName@@,
# including namespace segments. Descriptors are {vfptr, spare, name},
# starting eight bytes before the string. Report references to each
# descriptor. Missing entries do not imply a type is absent.
#
# Usage:
#	python tools/analysis/extract_types.py ref/a3dapi_33_dbg.dll \
#	       docs/llm/groundtruth/filelines-dbg.tsv > out.tsv
#

import sys
import os
import re
import struct
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_image import PE, tsv_header

SIZEOF = re.compile(r'sizeof\s*\(\s*([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*\)')

# These sizeof operands name variables/constants, not types (verified in
# filelines-dbg.tsv). pResManBuffer measures a pointer at resman.cpp:3658,
# 3701; the other twelve sites measure the object, suggesting an original
# assertion defect.
NOT_A_TYPE = {
	'props':             'local variable (A3d3.cpp:1274)',
	'm_RefImageFilled':  'member array, sizeof()/sizeof(int) idiom (refaudbin.cpp:122)',
	'pResManBuffer':     'sizeof(pointer); every other site sizeof()s ResManBuffer '
			     '- looks like a copy-paste defect in the original ASSERT',
	'IID_IA3d2':         'a GUID constant, not a type (dalinfo.cpp:357)',
}

RTTI = re.compile(rb'\.\?A([VU])([ -~]*?)@@\x00')


def read_sizeof(path):
	"""{type name: [file, file, ...]}, one entry per ASSERT site."""
	out = defaultdict(list)
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		f = line.rstrip('\n').split('\t', 4)
		if len(f) != 5 or f[1] != 'assert':
			continue
		fname, text = f[2], f[4]
		for m in SIZEOF.finditer(text):
			out[m.group(1)].append(fname)
	return out


def demangle(raw):
	"""'ClassName@Namespace' (reversed, @-joined) -> 'Namespace::ClassName'."""
	parts = [p for p in raw.split('@') if p]
	return '::'.join(reversed(parts))


def read_rtti(pe):
	"""[(name, kind, struct_va, [xref va, ...])] for every RTTI descriptor.

	The descriptor object is `{ void *vfptr; void *spare; char name[]; }`,
	so the name string's address minus eight is the object MSVC code
	elsewhere holds a pointer to.  `vfptr` is validated as a plausible
	in-image address and `spare` as zero before the match is trusted, so
	a coincidental `.?AV`/`.?AU` inside unrelated bytes is not reported as
	a descriptor.
	"""
	out = []
	for m in RTTI.finditer(pe.data):
		kind = 'class' if m.group(1) == b'V' else 'struct'
		name = demangle(m.group(2).decode('ascii', 'replace'))
		name_off = m.start()
		struct_off = name_off - 8
		if struct_off < 0:
			continue
		vfptr, spare = struct.unpack_from('<II', pe.data, struct_off)
		lo, hi = pe.imagebase, pe.imagebase + max(
			s['rva'] + s['vsize'] for s in pe.sections)
		if spare != 0 or not (lo <= vfptr < hi):
			continue
		struct_rva = pe.off_to_rva(struct_off)
		if struct_rva is None:
			continue
		struct_va = pe.va(struct_rva)

		pat = struct.pack('<I', struct_va)
		xrefs = []
		for xm in re.finditer(re.escape(pat), pe.data):
			o = xm.start()
			if o == struct_off:
				continue
			xrva = pe.off_to_rva(o)
			if xrva is not None:
				xrefs.append(pe.va(xrva))
		out.append((name, kind, struct_va, sorted(set(xrefs))))
	return out


def main():
	if len(sys.argv) != 3:
		sys.stderr.write('usage: extract_types.py <image> <filelines.tsv>\n')
		return 2

	image_path, filelines_path = sys.argv[1], sys.argv[2]
	pe = PE(image_path)

	sizeof_hits = read_sizeof(filelines_path)
	rtti_hits = read_rtti(pe)

	established = {}		# type -> set of 'sizeof' / 'rtti'
	sizeof_files = {}		# type -> sorted unique file list
	sizeof_count = {}		# type -> occurrence count
	rtti_addr = {}			# type -> struct va
	rtti_refs = {}			# type -> [va, ...]
	notes = {}

	for t, files in sizeof_hits.items():
		established.setdefault(t, set()).add('sizeof')
		sizeof_files[t] = sorted(set(files))
		sizeof_count[t] = len(files)
		if t in NOT_A_TYPE:
			notes[t] = 'not a type: ' + NOT_A_TYPE[t]

	for name, kind, va, xrefs in rtti_hits:
		established.setdefault(name, set()).add('rtti')
		rtti_addr[name] = va
		rtti_refs[name] = xrefs
		tag = 'RTTI descriptor: %s' % kind
		notes[name] = (notes[name] + '; ' + tag) if name in notes else tag

	rows = []
	for t in sorted(established):
		how = established[t]
		tag = 'both' if len(how) == 2 else next(iter(how))
		rows.append((
			t, tag,
			sizeof_count.get(t, 0),
			', '.join(sizeof_files.get(t, [])),
			'0x%08x' % rtti_addr[t] if t in rtti_addr else '',
			', '.join('0x%08x' % x for x in rtti_refs.get(t, [])),
			notes.get(t, ''),
		))

	note = ('Every type named verbatim in a debug build: by sizeof() inside\n'
		'an ASSERT (from the `assert` rows of the filelines table given\n'
		'on the command line) and by an RTTI type descriptor read\n'
		'directly from the image.\n'
		'\n'
		'  established  sizeof, rtti, or both\n'
		'  count        sizeof: the number of ASSERT sites naming the\n'
		'               type.  rtti-only: the number of places in the\n'
		'               image that hold the descriptor\'s address.\n'
		'  files        source files the sizeof() sites fall in, from\n'
		'               the filelines table; empty for an rtti-only row\n'
		'  rtti_addr    the descriptor object\'s address (vfptr, spare,\n'
		'               name), eight bytes before the name string\n'
		'  rtti_refs    addresses that hold rtti_addr as a 4-byte\n'
		'               pointer; empty where sizeof() is the only\n'
		'               evidence\n'
		'\n'
		'A row\'s `note` flags a sizeof() operand that is a variable or a\n'
		'constant rather than a type (checked by hand against the\n'
		'filelines table, listed in this script\'s NOT_A_TYPE), and says\n'
		'whether an RTTI descriptor names a class (V) or a struct (U).')

	out = tsv_header(os.path.basename(image_path), note)
	out.append('# type\testablished\tcount\tfiles\trtti_addr\trtti_refs\tnote')
	for row in rows:
		out.append('\t'.join(str(x) for x in row))
	sys.stdout.write('\n'.join(out) + '\n')

	n_sizeof = sum(1 for t in established if 'sizeof' in established[t])
	n_rtti = sum(1 for t in established if 'rtti' in established[t])
	n_both = sum(1 for t in established if len(established[t]) == 2)
	sys.stderr.write(
		'%s: %d types, %d by sizeof (%d occurrences), %d by rtti, %d by both\n'
		% (os.path.basename(image_path), len(established), n_sizeof,
		   sum(sizeof_count.values()), n_rtti, n_both))
	return 0


if __name__ == '__main__':
	sys.exit(main())
