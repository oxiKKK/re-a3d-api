
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Extract functions.\npython tools/analysis/extract_functions.py ref/a3dapi_33_dbg.dll docs/llm/groundtruth/regions-dbg.tsv > docs/llm/groundtruth/functions-dbg.tsv\npython tools/analysis/extract_functions.py ref/a3dapi_33_rtl.dll > docs/llm/groundtruth/functions-rtl.tsv')
    raise SystemExit(0)
#
# extract_functions.py - every function in an a3dapi image: entry address,
# size, terminator, stdcall argument bytes, body hash, owning source file.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
#
#
# Debug (/Od /ZI /INCREMENTAL): scan frame prologues after the jump table,
# using int3 padding as boundaries. Exclude the edit-and-continue table
# after an int3 run of at least 0x4000 bytes; reject prologues beyond it.
#
# Retail (/O2 /OPT:ICF): seed entries from exports/PE entry, relocated code
# pointers, calls/tail jumps and relocated immediates, then scan gaps as
# listed-only candidates. Decode through the furthest forward branch to
# the next ret/retn/unconditional jmp.
#
# Exclude switch tables, C++ EH thunks/funclets and SEH3 interior targets
# from source-function entries. Tag EH rows as eh. Switch tables require
# a jmp [tbl+reg*4], cmp bound and relocation run; relocated memory operands
# pointing into .text identify data.
#
# Limits: listed-only entries are inferred. cbargs is the retn immediate,
# or zero for ret/tail jmp; it cannot distinguish __cdecl from __stdcall
# with no arguments. Rows report addr, size, term, cbargs, entry byte,
# 16-digit sha1 prefix, corroboration and optional source-file attribution.
#
# Usage:
#	python tools/analysis/extract_functions.py ref/a3dapi_33_dbg.dll \
#	       docs/llm/groundtruth/regions-dbg.tsv > docs/llm/groundtruth/functions-dbg.tsv
#	python tools/analysis/extract_functions.py ref/a3dapi_33_rtl.dll \
#	       > docs/llm/groundtruth/functions-rtl.tsv
#

import sys
import os
import re
import struct
import bisect
import hashlib
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_image import PE, tsv_header

# -- x86 length decoder ------------------------------------------------------

PREFIX = frozenset((0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67,
		    0xf0, 0xf2, 0xf3))

ONE_MODRM = frozenset(
	[op for op in range(0x40) if op & 7 <= 3 and op != 0x0f] +
	[0x62, 0x63, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b,
	 0x8c, 0x8d, 0x8e, 0x8f, 0xc4, 0xc5, 0xd0, 0xd1, 0xd2, 0xd3,
	 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xfe])
ONE_IMM8 = frozenset(
	[op for op in range(0x40) if op & 7 == 4] +
	[0x6a, 0xa8, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	 0xcd, 0xd4, 0xd5, 0xe4, 0xe5, 0xe6, 0xe7])
ONE_IMMZ = frozenset(
	[op for op in range(0x40) if op & 7 == 5] +
	[0x68, 0xa9, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf])
ONE_NONE = frozenset(
	[0x06, 0x07, 0x0e, 0x27, 0x2f, 0x37, 0x3f] +
	list(range(0x40, 0x62)) +
	[0x6c, 0x6d, 0x6e, 0x6f] + list(range(0x90, 0xa0)) +
	[0xa4, 0xa5, 0xa6, 0xa7, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
	 0xc3, 0xc9, 0xcb, 0xcc, 0xce, 0xcf, 0xd6, 0xd7,
	 0xec, 0xed, 0xee, 0xef, 0xf1, 0xf4, 0xf5,
	 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd])
TWO_NONE = frozenset((0x05, 0x06, 0x07, 0x08, 0x09, 0x0b, 0x0e,
		      0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x77, 0xa0, 0xa1,
		      0xa8, 0xa9, 0xaa) + tuple(range(0xc8, 0xd0)))
TWO_MODRM_IMM8 = frozenset((0x70, 0x71, 0x72, 0x73, 0xa4, 0xac, 0xba,
			    0xc2, 0xc4, 0xc5, 0xc6))


class Insn(object):
	__slots__ = ('off', 'ln', 'kind', 'rel', 'imm16', 'disp_pos',
		     'disp_sz', 'tbl', 'byteload', 'cmpimm')

	def __init__(self):
		self.kind = 'other'
		self.rel = None		# branch target, file offset
		self.imm16 = 0		# retn operand
		self.disp_pos = None	# offset of a disp32/moffs field
		self.disp_sz = 0
		self.tbl = False	# jmp [disp32 + reg*4]
		self.byteload = False	# byte load with a disp32 table base
		self.cmpimm = None	# cmp against an immediate


def _modrm(d, q):
	m = d[q]
	mod, reg, rm = m >> 6, (m >> 3) & 7, m & 7
	q += 1
	sib = None
	if mod != 3 and rm == 4:
		sib = d[q]
		q += 1
	disp_pos, disp_sz = None, 0
	if mod == 1:
		disp_pos, disp_sz = q, 1
		q += 1
	elif mod == 2 or (mod == 0 and rm == 5) or \
	     (mod == 0 and sib is not None and sib & 7 == 5):
		disp_pos, disp_sz = q, 4
		q += 4
	return q, mod, reg, rm, sib, disp_pos, disp_sz


def decode(d, p):
	"""Length-decode one instruction at file offset p, or None."""
	ins = Insn()
	ins.off = p
	q = p
	opsize = 4
	while q < len(d) and d[q] in PREFIX:
		if d[q] == 0x66:
			opsize = 2
		q += 1
		if q - p > 4:
			return None
	if q >= len(d):
		return None
	op = d[q]
	q += 1

	def rel_branch(sz, kind):
		if sz == 1:
			t = q + 1 + struct.unpack_from('<b', d, q)[0]
		else:
			t = q + 4 + struct.unpack_from('<i', d, q)[0]
		ins.kind = kind
		ins.rel = t
		return q + sz

	if op == 0x0f:
		op2 = d[q]
		q += 1
		if 0x80 <= op2 <= 0x8f:
			q = rel_branch(4, 'jcc')
		elif op2 in TWO_NONE:
			pass
		elif op2 in TWO_MODRM_IMM8:
			q, mod, reg, rm, sib, ins.disp_pos, ins.disp_sz = _modrm(d, q)
			q += 1
		else:
			q, mod, reg, rm, sib, ins.disp_pos, ins.disp_sz = _modrm(d, q)
			if op2 in (0xb6, 0xbe) and mod != 3 and ins.disp_sz == 4:
				ins.byteload = True
	elif op in ONE_MODRM:
		q, mod, reg, rm, sib, ins.disp_pos, ins.disp_sz = _modrm(d, q)
		if op == 0x8a and mod != 3 and ins.disp_sz == 4:
			ins.byteload = True
	elif op in ONE_IMM8:
		if op == 0x3c:
			ins.cmpimm = d[q]
		q += 1
	elif op in ONE_IMMZ:
		if op == 0x3d:
			ins.cmpimm = struct.unpack_from('<I', d, q)[0] \
				if opsize == 4 else struct.unpack_from('<H', d, q)[0]
		q += opsize
	elif op in ONE_NONE:
		if op == 0xc3 or op == 0xcb:
			ins.kind = 'ret'
		elif op == 0xcc:
			ins.kind = 'int3'
	elif op in (0x80, 0x82, 0x83, 0x6b, 0xc0, 0xc1, 0xc6):
		q, mod, reg, rm, sib, ins.disp_pos, ins.disp_sz = _modrm(d, q)
		if op in (0x80, 0x83) and reg == 7:
			ins.cmpimm = d[q]
		q += 1
	elif op in (0x81, 0x69, 0xc7):
		q, mod, reg, rm, sib, ins.disp_pos, ins.disp_sz = _modrm(d, q)
		if op == 0x81 and reg == 7:
			ins.cmpimm = struct.unpack_from('<I', d, q)[0] \
				if opsize == 4 else struct.unpack_from('<H', d, q)[0]
		q += opsize
	elif op in (0xf6, 0xf7):
		q, mod, reg, rm, sib, ins.disp_pos, ins.disp_sz = _modrm(d, q)
		if reg in (0, 1):
			q += 1 if op == 0xf6 else opsize
	elif op == 0xff:
		q, mod, reg, rm, sib, ins.disp_pos, ins.disp_sz = _modrm(d, q)
		if reg in (2, 3):
			ins.kind = 'calli'
		elif reg in (4, 5):
			ins.kind = 'jmpi'
			if reg == 4 and mod == 0 and sib is not None and \
			   sib & 7 == 5 and sib >> 6 == 2 and (sib >> 3) & 7 != 4:
				ins.tbl = True
	elif op == 0xe8:
		q = rel_branch(4, 'call')
	elif op == 0xe9:
		q = rel_branch(4, 'jmp')
	elif op == 0xeb:
		q = rel_branch(1, 'jmp')
	elif 0x70 <= op <= 0x7f or op in (0xe0, 0xe1, 0xe2, 0xe3):
		q = rel_branch(1, 'jcc')
	elif op == 0xc2 or op == 0xca:
		ins.kind = 'retn'
		ins.imm16 = struct.unpack_from('<H', d, q)[0]
		q += 2
	elif op in (0xa0, 0xa1, 0xa2, 0xa3):
		ins.disp_pos, ins.disp_sz = q, 4
		q += 4
	elif op in (0x9a, 0xea):
		ins.kind = 'call' if op == 0x9a else 'jmp'
		q += 6
	elif op == 0xc8:
		q += 3
	else:
		return None
	ins.ln = q - p
	return ins


# -- the image and its fixed structures --------------------------------------

class Image(object):
	def __init__(self, path):
		self.pe = PE(path)
		self.d = self.pe.data
		t = self.pe.sections[0]
		if t['name'] != '.text':
			raise SystemExit('%s: first section is not .text' % path)
		self.t_off0 = t['off']
		self.t_off1 = t['off'] + min(t['vsize'], t['rsize'])
		self.t_rva0 = t['rva']

		# Relocation sites as file offsets, and per-site values.
		self.relocset = set()
		self.relocsorted = []
		for rva in self.pe.relocations():
			o = self.pe.rva_to_off(rva)
			if o is not None:
				self.relocset.add(o)
				self.relocsorted.append(o)
		self.relocsorted.sort()

		self.thunks = {}
		self.code0 = self._scan_ilt()
		self.code1 = self._scan_ec_cap()

	def va(self, off):
		return self.pe.va(self.pe.off_to_rva(off))

	def off_of_va(self, va):
		o = self.pe.rva_to_off(va - self.pe.imagebase)
		return o

	def in_code(self, off):
		return off is not None and self.code0 <= off < self.code1

	def _scan_ilt(self):
		"""The incremental linker's jump table at the start of .text."""
		d, p = self.d, self.t_off0
		while p < self.t_off1:
			if d[p] == 0xcc:
				p += 1
			elif d[p] == 0xe9:
				t = p + 5 + struct.unpack_from('<i', d, p + 1)[0]
				self.thunks[p] = t
				p += 5
			else:
				break
		if p == self.t_off0:
			self.thunks = {}
		return p

	def _scan_ec_cap(self):
		"""Where code ends.  An incremental image reserves a large int3
		run before its edit-and-continue link table; a non-incremental
		image has no such run and code reaches the end of .text."""
		m = re.search(rb'\xcc{16384,}', self.d[self.code0:self.t_off1])
		if not m:
			self.past_cap = 0
			return self.t_off1
		cap = self.code0 + m.start()
		# The table region carries file/line thunks, symbol names, and
		# orphaned copies of functions from earlier incremental links.
		# None of it is reachable: main() verifies that no export and
		# no incremental jump-table thunk resolves past the cap.
		self.past_cap = self.d[self.code0 + m.end():self.t_off1] \
			.count(b'\x55\x8b\xec')
		for t in self.thunks.values():
			if t >= cap:
				raise SystemExit('incremental thunk targets '
						 '0x%08x past the link-table '
						 'boundary 0x%08x'
						 % (self.va(t), self.va(cap)))
		return cap

	def resolve(self, off, depth=8):
		while depth and off in self.thunks:
			off = self.thunks[off]
			depth -= 1
		return off


def parse_eh(img):
	"""C++ EH thunks and the code addresses their FuncInfo name, and the
	SEH3 scope tables' filter and handler addresses.

	-> (ehthunks {off: funcinfo_off}, eh_targets set, handler offs)"""
	d = img.d
	ehthunks, targets, handlers = {}, set(), set()

	for m in re.finditer(rb'\xb8....\xe9', d, re.S):
		o = m.start()
		if not img.in_code(o):
			continue
		fi = img.off_of_va(struct.unpack_from('<I', d, o + 1)[0])
		if fi is None or fi + 28 > len(d) or img.pe.u32(fi) != 0x19930520:
			continue
		ehthunks[o] = fi
		handlers.add(img.resolve(o + 10 + struct.unpack_from('<i', d, o + 6)[0]))

		nstate = img.pe.u32(fi + 4)
		um = img.off_of_va(img.pe.u32(fi + 8))
		if um is not None and nstate < 0x1000:
			for i in range(nstate):
				act = img.pe.u32(um + i * 8 + 4)
				t = img.off_of_va(act)
				if img.in_code(t):
					targets.add(t)
		ntry = img.pe.u32(fi + 12)
		tb = img.off_of_va(img.pe.u32(fi + 16))
		if tb is not None and ntry < 0x1000:
			for i in range(ntry):
				nc = img.pe.u32(tb + i * 20 + 12)
				ha = img.off_of_va(img.pe.u32(tb + i * 20 + 16))
				if ha is None or nc >= 0x100:
					continue
				for j in range(nc):
					t = img.off_of_va(img.pe.u32(ha + j * 16 + 12))
					if img.in_code(t):
						targets.add(t)

	seh = re.compile(rb'\x68....\x68....\x64\xa1\x00\x00\x00\x00\x50', re.S)
	seh_mid = set()
	for m in seh.finditer(d):
		o = m.start()
		if not img.in_code(o):
			continue
		handlers.add(img.resolve(img.off_of_va(img.pe.u32(o + 6))))
		tbl = img.off_of_va(img.pe.u32(o + 1))
		if tbl is None:
			continue
		for i in range(64):
			prev = img.pe.i32(tbl + i * 12)
			filt = img.off_of_va(img.pe.u32(tbl + i * 12 + 4))
			hnd = img.off_of_va(img.pe.u32(tbl + i * 12 + 8))
			if prev != -1 and not 0 <= prev < i:
				break
			if not img.in_code(hnd):
				break
			seh_mid.add(hnd)
			if img.in_code(filt):
				seh_mid.add(filt)
	return ehthunks, targets, handlers, seh_mid


# -- the sweep ---------------------------------------------------------------

class Func(object):
	__slots__ = ('off', 'end', 'term', 'cbargs', 'why', 'err')


def sweep(img, entry, bound, claimed, out):
	"""Decode one function.  Returns a Func and feeds discovered call
	targets, code refs and table claims into `out` (a dict of sets and
	Counters)."""
	d = img.d
	f = Func()
	f.off, f.term, f.cbargs, f.err = entry, 'err', 0, None
	p = entry
	maxfwd = entry
	ring = []

	while p < bound:
		hi = claimed_end(claimed, p)
		if hi is not None:
			# A switch table in the middle of the function; the
			# code the jump reaches continues past it.
			if maxfwd >= hi:
				p = hi
				continue
			f.err = 'ran into claimed data at 0x%08x' % img.va(p)
			break
		ins = decode(d, p)
		if ins is None:
			f.err = 'undecodable byte 0x%02x at 0x%08x' % (d[p], img.va(p))
			break
		q = p + ins.ln
		ring.append(ins)
		if len(ring) > 8:
			ring.pop(0)

		# Relocated fields inside the instruction.
		relocs_here = [r for r in range(p, q - 3) if r in img.relocset]
		for r in relocs_here:
			v = img.off_of_va(img.pe.u32(r))
			if ins.disp_pos is not None and r == ins.disp_pos:
				if img.in_code(v) and not ins.tbl and not ins.byteload:
					out['datamarks'].add(v)
			elif img.in_code(v):
				out['refs'][img.resolve(v)] += 1

		if ins.kind == 'call' and ins.rel is not None:
			t = img.resolve(ins.rel)
			if img.in_code(t):
				out['calls'][t] += 1
		elif ins.kind in ('jmp', 'jcc') and ins.rel is not None:
			t = ins.rel
			if q <= t < bound:
				maxfwd = max(maxfwd, t)
			elif t < entry or t >= bound:
				t = img.resolve(t)
				if img.in_code(t) and ins.kind == 'jmp':
					out['calls'][t] += 1
				elif ins.kind == 'jcc':
					out['jcc_out'] += 1
		elif ins.tbl:
			tbl = img.off_of_va(img.pe.u32(ins.disp_pos))
			if tbl is not None:
				bl = next((i for i in ring[-4:-1] if i.byteload), None)
				cmp_ = next((i.cmpimm for i in reversed(ring[:-1])
					     if i.cmpimm is not None), None)
				maxfwd = max(maxfwd, claim_tables(
					img, tbl, bl, cmp_, entry, bound,
					claimed, out))

		if ins.kind in ('ret', 'retn', 'jmp', 'jmpi'):
			if maxfwd < q:
				f.term = {'ret': 'ret', 'retn': 'retn',
					  'jmp': 'jmp', 'jmpi': 'jmp'}[ins.kind]
				f.cbargs = ins.imm16
				f.end = q
				return f
		elif ins.kind == 'int3':
			if maxfwd < q:
				f.term = 'call' if len(ring) > 1 and \
					ring[-2].kind in ('call', 'calli') else 'int3'
				f.end = p
				return f
			# An inline int3 (or a noreturn call's padding) with
			# reachable code past it.  Step over the run.
			while q < bound and d[q] == 0xcc:
				q += 1
		p = q

	f.end = min(p, bound)
	if not f.err:
		if p == bound and maxfwd < bound:
			# Fell into the next entry with no terminator.  The
			# CRT's x87 dispatch stubs do this.
			f.term = 'fall'
		else:
			f.err = 'flow crosses the sweep bound at 0x%08x' \
				% img.va(f.end)
	return f


def claim_tables(img, tbl, byteload, cmpimm, entry, bound, claimed, out):
	"""Claim a switch's dword table, and its byte index table if the jmp
	went through one.  Returns the furthest in-function target."""
	def claim(lo, hi):
		for k in range(lo >> 6, ((hi - 1) >> 6) + 1):
			claimed.setdefault(k, []).append((lo, hi))

	# The relocation run bounds the dword table from the linker's side.
	run = 0
	while tbl + run * 4 in img.relocset and \
	      img.in_code(img.off_of_va(img.pe.u32(tbl + run * 4))):
		run += 1

	# A byte index table base is a relocated pointer into the code
	# section; anything else near the jump is an ordinary byte load.
	if byteload is not None:
		if byteload.disp_pos not in img.relocset or \
		   not img.in_code(img.off_of_va(img.pe.u32(byteload.disp_pos))):
			byteload = None
	if byteload is not None:
		btbl = img.off_of_va(img.pe.u32(byteload.disp_pos))
		if cmpimm is None or cmpimm >= 0x1000:
			# No visible bound for the index table; claim nothing
			# and let the dword table stand on its relocation run.
			out['tbl_nobound'] += 1
			n = run
		else:
			bn = cmpimm + 1
			idx = img.d[btbl:btbl + bn]
			n = (max(idx) + 1) if idx else 0
			claim(btbl, btbl + bn)
	elif cmpimm is not None and cmpimm < 0x1000:
		n = cmpimm + 1
		if run and run != n:
			out['tbl_mismatch'] += 1
			n = min(n, run)
	else:
		n = run
		out['tbl_nobound'] += 1

	maxfwd = entry
	if n and n <= run:
		claim(tbl, tbl + n * 4)
		for i in range(n):
			t = img.off_of_va(img.pe.u32(tbl + i * 4))
			if t is not None and entry <= t < bound:
				maxfwd = max(maxfwd, t)
	else:
		out['tbl_mismatch'] += 1
	return maxfwd


# The multi-byte NOP idioms MSVC pads alignment with between functions.
NOPS = (b'\x8b\xff', b'\x8d\x49\x00', b'\x8d\x64\x24\x00',
	b'\x8d\xa4\x24\x00\x00\x00\x00', b'\x8d\x9b\x00\x00\x00\x00',
	b'\x8d\x80\x00\x00\x00\x00', b'\x8d\xb4\x26\x00\x00\x00\x00',
	b'\x8d\xbf\x00\x00\x00\x00')


def nop_len(d, s, g1):
	for n in NOPS:
		if d[s:s + len(n)] == n and s + len(n) <= g1:
			return len(n)
	return 0


def claimed_end(claimed, p):
	for lo, hi in claimed.get(p >> 6, ()):
		if lo <= p < hi:
			return hi
	return None


def is_claimed(claimed, p):
	return claimed_end(claimed, p) is not None


def main():
	if len(sys.argv) not in (2, 3):
		sys.stderr.write('usage: extract_functions.py <image> [regions.tsv]\n')
		return 2

	img = Image(sys.argv[1])
	d = img.d
	debugish = bool(img.thunks)

	regions = []
	if len(sys.argv) == 3:
		for line in open(sys.argv[2], encoding='utf-8'):
			if line.startswith('#') or not line.strip():
				continue
			f = line.rstrip('\n').split('\t')
			regions.append((int(f[0], 16), int(f[1], 16), f[4]))
		regions.sort()

	def owner(va):
		for lo, hi, fname in regions:
			if lo <= va <= hi:
				return fname
		return '-'

	ehthunks, eh_targets, eh_handlers, seh_mid = parse_eh(img)

	# Anchor entries.  {off: set(tokens)}
	strong = {}

	def add(off, token):
		if img.in_code(off):
			strong.setdefault(off, set()).add(token)

	for _ord, name, rva in img.pe.exports():
		t = img.resolve(img.pe.rva_to_off(rva))
		if not img.in_code(t):
			raise SystemExit('export %s resolves outside the code '
					 'range' % name)
		add(t, 'export')
	ep = img.resolve(img.pe.rva_to_off(img.pe.entrypoint))
	if not img.in_code(ep):
		raise SystemExit('the entry point resolves outside the code range')
	add(ep, 'entry')
	for h in eh_handlers:
		add(h, 'called')	# reached by jmp from every EH thunk

	# Code pointers from the data sections: vtable slots and callback
	# tables.  EH structure fields are excluded; they point inside
	# functions.
	for r in img.relocsorted:
		if img.t_off0 <= r < img.t_off1:
			continue
		v = img.off_of_va(img.pe.u32(r))
		if not img.in_code(v):
			continue
		v = img.resolve(v)
		if v in eh_targets or v in seh_mid or v in ehthunks:
			continue
		add(v, 'vtable')

	# Iterate: sweep everything known, harvest call targets and code
	# refs, then look in the unclaimed gaps.
	# FuncInfo unwind and catch funclets are NOT entries: MSVC 6 places
	# them inside the owning function's span, jumped over by normal flow,
	# so the sweep passes through them.  The set only tags the ones that
	# end up past their function's terminator and surface as gap code.
	entries = dict(strong)
	for t in ehthunks:
		entries.setdefault(t, set()).add('eh')
	inferred = set()

	funcs = {}
	stats = None
	for _pass in range(16):
		claimed = {}
		out = {'calls': Counter(), 'refs': Counter(),
		       'datamarks': set(), 'jcc_out': 0,
		       'tbl_nobound': 0, 'tbl_mismatch': 0}
		strong_sorted = sorted(k for k, why in entries.items()
				       if why & {'export', 'entry', 'vtable',
						 'called', 'eh'})
		funcs = {}
		last_end = img.code0
		dropped = []
		for e in sorted(entries):
			if e < last_end or is_claimed(claimed, e):
				dropped.append(e)
				continue
			i = bisect.bisect_right(strong_sorted, e)
			bound = strong_sorted[i] if i < len(strong_sorted) \
				else img.code1
			f = sweep(img, e, min(bound, img.code1), claimed, out)
			funcs[e] = f
			last_end = f.end

		# Fold harvested references in.
		new = 0
		for t in out['calls']:
			if t not in entries:
				new += 1
			entries.setdefault(t, set()).add('called')
		for t in out['refs']:
			if t in entries:
				entries[t].add('ref')
				continue
			if t in out['datamarks'] or \
			   any(f.off < t < f.end for f in funcs.values()):
				continue
			entries[t] = {'ref'}
			new += 1

		# Gap sweep.
		p = img.code0
		ends = sorted((f.off, f.end) for f in funcs.values())
		gap_data, gap_open = [], []
		idx = 0
		while p < img.code1:
			if idx < len(ends) and p >= ends[idx][0]:
				p = max(p, ends[idx][1])
				idx += 1
				continue
			nxt = ends[idx][0] if idx < len(ends) else img.code1
			g0, g1 = p, min(nxt, img.code1)
			p = nxt
			s = g0
			gap_claimed = {k: list(v) for k, v in claimed.items()}
			gap_out = {'calls': Counter(), 'refs': Counter(),
				   'datamarks': set(), 'jcc_out': 0,
				   'tbl_nobound': 0, 'tbl_mismatch': 0}
			while s < g1:
				# Walk past padding, claimed switch tables,
				# short alignment filler in front of a table
				# (the `mov edi, edi` and `lea ecx, [ecx]`
				# idioms), and orphan runs of relocated code
				# pointers (table entries past the cmp bound
				# that claimed the rest).
				moved = True
				while moved and s < g1:
					moved = False
					while s < g1 and d[s] in (0xcc, 0x90, 0x00):
						s += 1
						moved = True
					npad = nop_len(d, s, g1)
					if npad:
						s += npad
						moved = True
					hi = claimed_end(gap_claimed, s)
					if hi is not None:
						s = min(hi, g1)
						moved = True
						continue
					while s < g1 and s in img.relocset and \
					      img.in_code(img.off_of_va(img.pe.u32(s))):
						s += 4
						moved = True
					nc = next((t for t in range(s, min(s + 9, g1))
						   if is_claimed(gap_claimed, t)), None)
					if nc is not None:
						s = nc
						moved = True
				if s >= g1:
					break
				# The CRT's `VC20XC00` marker sits in .text as
				# an 8-byte string, not code.
				if d[s:s + 8] == b'VC20XC00':
					s += 8
					continue
				if any(s <= m < g1 for m in out['datamarks']):
					gap_data.append((s, g1))
					break
				if s in entries:
					break
				# Accept consecutive gap candidates only after a complete decode.
				# Do not require the /Od prologue: thunks and CRT helpers may omit it.
				i = bisect.bisect_right(strong_sorted, s)
				bnd = strong_sorted[i] if i < len(strong_sorted) \
					else img.code1
				trial = sweep(img, s, min(bnd, img.code1),
					      gap_claimed, gap_out)
				if trial.err or trial.term == 'fall':
					gap_open.append((s, g1))
					break
				why = {'eh'} if s in eh_targets else set()
				entries[s] = why
				inferred.add(s)
				new += 1
				s = trial.end
		stats = (out, gap_data, gap_open, dropped, claimed)
		if not new:
			break
	else:
		raise SystemExit('no fixpoint after 16 passes')

	out, gap_data, gap_open, dropped, claimed = stats

	# Rows.
	rows = []
	errs = []
	for e in sorted(funcs):
		f = funcs[e]
		if f.err:
			errs.append((e, f.err))
			continue
		why = entries.get(e, set())
		tokens = []
		if 'export' in why:
			tokens.append('export')
		if 'entry' in why:
			tokens.append('entry')
		if 'vtable' in why:
			tokens.append('vtable')
		n = out['calls'].get(e, 0)
		if n:
			tokens.append('called:%d' % n)
		n = out['refs'].get(e, 0)
		if n:
			tokens.append('ref:%d' % n)
		if 'eh' in why:
			tokens.append('eh')
		if not tokens:
			tokens.append('listed-only')
		body = d[f.off:f.end]
		rows.append((img.va(f.off), f.end - f.off, f.term, f.cbargs,
			     d[f.off], hashlib.sha1(body).hexdigest()[:16],
			     ','.join(tokens), owner(img.va(f.off))))

	name = os.path.basename(sys.argv[1])
	note = ('Every function recovered from the image.  `addr` is the entry,\n'
		'`size` the byte length up to and including the terminator,\n'
		'`term` how the body ends, `cbargs` the bytes its retn pops (0\n'
		'for a plain ret or a tail jmp), `entry` the first body byte,\n'
		'`sha1` the first 16 hex digits of a SHA-1 over the body bytes,\n'
		'and `file` the owning source file from the Debug regions table,\n'
		'or - where no region covers the address.\n'
		'\n'
		'corroboration says how the entry is known:\n'
		'  export       named in the export table\n'
		'  entry        the PE entry point\n'
		'  vtable       a relocated code pointer in a data section\n'
		'  called:N     N call or tail-jmp sites in decoded code\n'
		'  ref:N        N relocated immediates naming the address\n'
		'  eh           compiler EH plumbing (a funclet or an\n'
		'               `ehhandler` thunk), not a source function\n'
		'  listed-only  found by the sweep alone, so inferred\n'
		'\n'
		'The incremental jump table and the edit-and-continue link\n'
		'table of the Debug build are excluded; they hold no compiled\n'
		'function.  Switch tables are excluded as data.\n'
		'\n'
		'/OPT:ICF folds identical COMDAT functions onto one address, so a\n'
		'fold the linker made surfaces as one Retail address carrying\n'
		'several vtable or call citations, which the Debug-to-Retail map\n'
		'resolves.  Two rows with the same sha1 are identical bodies the\n'
		'linker left as separate copies, so it could fold them only if\n'
		'they were COMDAT and evidently they were not.')
	hdr = tsv_header(name, note)
	hdr.append('# addr\tsize\tterm\tcbargs\tentry\tsha1\tcorroboration\tfile')
	print('\n'.join(hdr))
	for addr, size, term, cbargs, eb, sha, corr, fname in rows:
		print('0x%08x\t0x%x\t%s\t%d\t0x%02x\t%s\t%s\t%s'
		      % (addr, size, term, cbargs, eb, sha, corr, fname))

	# Summary and problems, to stderr.
	kinds = Counter(r[2] for r in rows)
	e_eh = sum(1 for r in rows if 'eh' in r[6])
	e_inf = sum(1 for r in rows if r[6] == 'listed-only')
	dup = Counter(r[5] for r in rows)
	dupg = {k: v for k, v in dup.items() if v > 1}
	sys.stderr.write('%s: code 0x%08x..0x%08x, %d ILT thunks, %d orphan '
			 'prologues in the link-table region\n'
			 % (name, img.va(img.code0), img.va(img.code1),
			    len(img.thunks), img.past_cap))
	sys.stderr.write('%s: %d functions (%d eh, %d listed-only), terms %s\n'
			 % (name, len(rows), e_eh, e_inf, dict(kinds)))
	sys.stderr.write('%s: %d duplicate-hash groups over %d rows\n'
			 % (name, len(dupg), sum(dupg.values())))
	sys.stderr.write('%s: tables: %d without a cmp bound, %d bound/run '
			 'mismatches; %d cond jumps leaving their function\n'
			 % (name, out['tbl_nobound'], out['tbl_mismatch'],
			    out['jcc_out']))
	for e, msg in errs:
		sys.stderr.write('  sweep error at 0x%08x: %s\n' % (img.va(e), msg))
	for e in dropped:
		sys.stderr.write('  entry 0x%08x lies inside another function; '
				 'dropped\n' % img.va(e))
	for s, g1 in gap_open:
		sys.stderr.write('  unexplained gap 0x%08x..0x%08x\n'
				 % (img.va(s), img.va(g1)))
	for s, g1 in gap_data:
		sys.stderr.write('  data in .text 0x%08x..0x%08x\n'
				 % (img.va(s), img.va(g1)))
	if errs:
		sys.stderr.write('%s: %d sweep errors; the table above is '
				 'incomplete\n' % (name, len(errs)))
		return 1
	return 0


if __name__ == '__main__':
	sys.exit(main())
