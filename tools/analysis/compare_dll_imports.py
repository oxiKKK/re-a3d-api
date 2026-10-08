
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Compare dll imports.\npython tools/analysis/compare_dll_imports.py [--aureal]')
    raise SystemExit(0)
#
# compare_dll_imports.py - which imports Aureal's Retail DLL has that ours does not.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Missing imports can expose unreachable reconstructed functions removed
# by the linker. Report each missing import with its Retail caller and
# the source file assigned by funcmap.tsv. Citation coverage alone cannot
# detect these missing call paths.
#
# Compiler differences also change imports: MSVC 6/19 use different CRT
# startup routines, and newer compilers inline Interlocked operations.
# --aureal excludes entries classified by CRT_FILES and CRT_NAMES.
#
# Usage:
#	python tools/analysis/compare_dll_imports.py [--aureal]
#

import bisect
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GT = os.path.join(ROOT, 'docs', 'llm', 'groundtruth')

REF = os.path.join(ROOT, 'ref', 'a3dapi_33_rtl.dll')
OURS = os.path.join(ROOT, 'build', 'Release', 'a3dapi.dll')

#
# The Retail files funcmap.tsv attributes to MSVC 6's own CRT, and the two
# Interlocked entry points MSVC 19 makes intrinsic.  A row against any of these
# says nothing about Aureal's code.
#
CRT_FILES = set('''
	_getbuf.c _sftbuf.c _open.c chsize.c sprintf.c vsprintf.c
	_freebuf.c
'''.split())

CRT_NAMES = set('''
	InterlockedIncrement InterlockedDecrement InterlockedExchange
'''.split())


def pe(path):
	"""(bytes, imagebase, rva->offset, import directory rva)."""
	b = open(path, 'rb').read()
	e = struct.unpack_from('<I', b, 0x3C)[0]
	opt = e + 0x18
	magic = struct.unpack_from('<H', b, opt)[0]
	nsec = struct.unpack_from('<H', b, e + 6)[0]
	sizeopt = struct.unpack_from('<H', b, e + 20)[0]
	base = struct.unpack_from('<I', b, opt + 28)[0]
	ddir = opt + (0x60 if magic == 0x10b else 0x70)
	imp_rva = struct.unpack_from('<I', b, ddir + 8)[0]

	secs = []
	off = e + 24 + sizeopt
	for i in range(nsec):
		_, vsize, vaddr, rawsize, rawptr = struct.unpack_from(
			'<8sIIII', b, off + i * 40)
		secs.append((vaddr, vsize, rawptr, rawsize))

	def r2o(rva):
		for vaddr, vsize, rawptr, rawsize in secs:
			if vaddr <= rva < vaddr + max(vsize, rawsize):
				return rawptr + (rva - vaddr)
		return None

	def o2v(off):
		for vaddr, vsize, rawptr, rawsize in secs:
			if rawptr <= off < rawptr + rawsize:
				return base + vaddr + (off - rawptr)
		return None

	return b, base, r2o, o2v, imp_rva


def imports(path):
	"""{(DLL, func): the IAT slot's virtual address}."""
	b, base, r2o, _, imp_rva = pe(path)
	out = {}
	p = r2o(imp_rva)
	while True:
		oft, _, _, nrva, fdirva = struct.unpack_from('<IIIII', b, p)
		if nrva == 0:
			break
		o = r2o(nrva)
		dll = b[o:b.index(b'\0', o)].decode().upper()
		thunk = r2o(oft or fdirva)
		slot = fdirva
		while True:
			v = struct.unpack_from('<I', b, thunk)[0]
			if v == 0:
				break
			if v & 0x80000000:
				name = '#%d' % (v & 0xFFFF)
			else:
				fo = r2o(v)
				name = b[fo + 2:b.index(b'\0', fo + 2)].decode()
			out[(dll, name)] = base + slot
			thunk += 4
			slot += 4
		p += 20
	return out


def call_sites(path, wanted):
	"""{iat va: [virtual addresses of `call`/`jmp dword ptr [iat]`]}."""
	b, _, _, o2v, _ = pe(path)
	key = {}
	for va in wanted:
		key[struct.pack('<I', va)] = va
	hits = {}
	for i in range(len(b) - 6):
		if b[i] == 0xFF and b[i + 1] in (0x15, 0x25):
			va = key.get(b[i + 2:i + 6])
			if va is not None:
				hits.setdefault(va, []).append(o2v(i))
	return hits


def rows(name):
	out = []
	with open(os.path.join(GT, name), encoding='utf-8') as fp:
		for line in fp:
			if line.startswith('#') or not line.strip():
				continue
			out.append(line.rstrip('\n').split('\t'))
	return out


def main():
	aureal_only = '--aureal' in sys.argv[1:]

	for path in (REF, OURS):
		if not os.path.exists(path):
			sys.stderr.write('%s is not built\n' % path)
			return 2

	theirs = imports(REF)
	ours = imports(OURS)
	missing = sorted(set(theirs) - set(ours))

	hits = call_sites(REF, [theirs[k] for k in missing])

	funcs = sorted((int(r[0], 16), int(r[1], 16)) for r in rows('functions-rtl.tsv'))
	starts = [a for a, _ in funcs]

	# A Retail address takes its file from the Debug function funcmap pairs it
	# with, because only the Debug build carries the __FILE__ sites.
	rtl_file = {}
	for r in rows('funcmap.tsv'):
		if r[1] != '-':
			rtl_file[int(r[1], 16)] = r[5] if len(r) > 5 and r[5] != '-' else '?'

	def owner(va):
		i = bisect.bisect_right(starts, va) - 1
		if i >= 0:
			a, size = funcs[i]
			if a <= va < a + size:
				return a, rtl_file.get(a, '?')
		return None, '?'

	shown = 0
	lines = []
	for dll, fn in missing:
		where = []
		for va in hits.get(theirs[(dll, fn)], ()):
			a, f = owner(va)
			if a is not None and (a, f) not in where:
				where.append((a, f))

		if aureal_only:
			if fn in CRT_NAMES:
				continue
			if where and all(f in CRT_FILES for _, f in where):
				continue

		shown += 1
		lines.append('%-14s %-28s %s'
			     % (dll, fn,
				'; '.join('0x%08x %s' % (a, f) for a, f in where)
				or '(no direct call found)'))

	print('%d imports the reference has and ours does not%s.\n'
	      % (shown, ', outside the CRT' if aureal_only else ''))
	print('\n'.join(lines))
	return 0


if __name__ == '__main__':
	sys.exit(main())
