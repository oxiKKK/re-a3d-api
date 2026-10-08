
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Extract vtables.\npython tools/analysis/extract_vtables.py ref/a3dapi_33_dbg.dll --regions docs/llm/groundtruth/regions-dbg.tsv\npython tools/analysis/extract_vtables.py ref/a3dapi_33_rtl.dll --against docs/llm/groundtruth/vtables-dbg.tsv')
    raise SystemExit(0)
#
# extract_vtables.py - every C++ vtable in an image, slot by slot.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Candidates are consecutive relocated code pointers with an installation
# store: mov dword ptr [reg+disp], offset vtbl (C7 /0). Reject runs
# referenced only by register loads, pushes or switch-table jumps. End a
# table at the next installed address or the end of its relocation run.
#
# Unrecognized/missing installers hide tables and can inflate an adjacent
# table's slot count. Matching counts across builds support identification
# but do not prove it.
#
# Only the CRT .?AVtype_info@@ RTTI descriptor is present. Class attribution
# uses Debug contribution symbols (??_7Class@@6BBase@@@) and sizes,
# constructor source regions, or matching ordered slot-count sequences
# from --against.
#
# Infer __purecall from the most frequent target. Resolve incremental
# jumps and adjustor thunks before reporting final targets: __stdcall
# thunks subtract from [esp+4]; __thiscall thunks subtract from ecx.
#
# Usage:
#	python tools/analysis/extract_vtables.py ref/a3dapi_33_dbg.dll \
#		--regions docs/llm/groundtruth/regions-dbg.tsv
#	python tools/analysis/extract_vtables.py ref/a3dapi_33_rtl.dll \
#		--against docs/llm/groundtruth/vtables-dbg.tsv
#

import sys
import os
import re
import struct
import difflib
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_image import PE, tsv_header


# -- the image ---------------------------------------------------------------

def code_section(pe):
	for s in pe.sections:
		if s['name'] == '.text':
			return s
	return None


def text_pointers(pe):
	"""{rva of a relocated dword: rva of the code it points at}."""
	code = code_section(pe)
	lo, hi = code['rva'], code['rva'] + code['vsize']
	out = {}
	for rva in pe.relocations():
		s = pe.section_of_rva(rva)
		if not s or s['name'] == '.text':
			continue
		o = pe.rva_to_off(rva)
		if o is None or o + 4 > len(pe.data):
			continue
		v = pe.u32(o) - pe.imagebase
		if lo <= v < hi:
			out[rva] = v
	return out


def install_sites(pe, ptr):
	"""{rva of an installed table: [rva of each store]}.

	A store is the imm32 of `mov r/m32, imm32` where the destination is
	memory through a register.  The immediate is located by decoding the
	instruction rather than by looking at the bytes in front of the fixup,
	because the C7 form puts the immediate after any SIB and displacement,
	and a `mov dword ptr [disp32], imm32` carries two fixups of its own.
	"""
	code = code_section(pe)
	lo, hi = code['rva'], code['rva'] + code['vsize']
	d = pe.data
	out = defaultdict(list)
	for rva in pe.relocations():
		if not lo <= rva < hi:
			continue
		o = pe.rva_to_off(rva)
		if o is None or o + 4 > len(d):
			continue
		v = pe.u32(o) - pe.imagebase
		if v not in ptr:
			continue
		for p in range(o - 8, o - 1):
			if d[p] != 0xc7:
				continue
			mod, rm = d[p + 1] >> 6, d[p + 1] & 7
			if mod == 3 or (mod == 0 and rm == 5):
				continue		# register, or [disp32]
			q = p + 2
			if rm == 4:
				q += 1			# SIB
			if mod == 1:
				q += 1
			elif mod == 2:
				q += 4
			if q == o:
				out[v].append(rva)
				break
	return out


def vtables(pe):
	"""[(rva, [target rva], [install site rva])], in address order."""
	ptr = text_pointers(pe)
	sites = install_sites(pe, ptr)

	runs = []
	for a in sorted(ptr):
		if runs and a == runs[-1][-1] + 4:
			runs[-1].append(a)
		else:
			runs.append([a])

	out = []
	for r in runs:
		hits = [i for i, a in enumerate(r) if a in sites]
		if not hits:
			continue
		edge = hits + [len(r)]
		for j, s in enumerate(hits):
			e = edge[j + 1]
			out.append((r[s], [ptr[a] for a in r[s:e]], sites[r[s]]))
	return out


# -- slot targets ------------------------------------------------------------

def jump_table(pe):
	"""{rva: rva} for the incremental linker's `jmp rel32` table.

	The table is packed, one 5-byte `jmp rel32` after another, so it is
	found as the runs of at least eight such jumps back to back.  Requiring
	a run keeps an ordinary function that happens to start with a jump out
	of the map.  A build linked without /INCREMENTAL has no run that long
	and yields an empty map, which is what Retail does.
	"""
	code = code_section(pe)
	d = pe.data
	lo, hi = code['off'], code['off'] + code['vsize']
	out, o = {}, lo
	while o < hi:
		if d[o] != 0xe9:
			o += 1
			continue
		q = o
		while q + 5 <= hi and d[q] == 0xe9:
			q += 5
		if (q - o) // 5 >= 8:
			for p in range(o, q, 5):
				src = pe.off_to_rva(p)
				out[src] = src + 5 + pe.i32(p + 1)
		o = max(q, o + 1)
	return out


def adjustor(pe, rva):
	"""(delta, destination) if rva is an adjustor thunk, else None."""
	o = pe.rva_to_off(rva)
	if o is None or o + 13 > len(pe.data):
		return None
	d = pe.data
	if d[o:o + 4] == b'\x83\x6c\x24\x04' and d[o + 5] == 0xe9:
		return d[o + 4], rva + 10 + pe.i32(o + 6)	# sub [esp+4], imm8
	if d[o:o + 4] == b'\x81\x6c\x24\x04' and d[o + 8] == 0xe9:
		return pe.u32(o + 4), rva + 13 + pe.i32(o + 9)
	if d[o] == 0x83 and d[o + 1] == 0xe9 and d[o + 3] == 0xe9:
		return d[o + 2], rva + 8 + pe.i32(o + 4)	# sub ecx, imm8
	if d[o] == 0x81 and d[o + 1] == 0xe9 and d[o + 6] == 0xe9:
		return pe.u32(o + 2), rva + 11 + pe.i32(o + 7)
	return None


# -- the Debug build's link table --------------------------------------------

NAMED = re.compile(rb'\?\?_7[\w@?$]+')


def link_vtables(pe):
	"""[[(name, slots)]], one list per contribution chunk, in image order.

	The incremental linker leaves its contribution table in .text: records
	carrying a section name, a contribution size and a relocation count,
	followed by a length-prefixed blob of symbol names that the records
	index by offset.  A record is found by searching for its name offset and
	then for the `03 01` field in front of it, and it is only accepted when
	the relocation count equals the size in dwords, which every vtable
	satisfies because every slot is relocated.  There is no address in the
	record, so this gives the name and the slot count and nothing more.

	A build linked without the table, which is both DebugViewer and Retail,
	yields nothing.
	"""
	d = pe.data
	chunks, seen = [], {}
	for m in NAMED.finditer(d):
		s = m.start()
		found = blob_base(d, s)
		if found is None:
			continue
		base, size = found
		if base not in seen:
			seen[base] = []
			chunks.append((base, size, seen[base]))
		name = d[s:d.find(b'\0', s)].decode('ascii', 'replace')
		seen[base].append((name, s - base))

	out, prev = [], 0
	for base, size, names in chunks:
		got, floor = [], prev
		for name, noff in names:
			slots, floor = contribution(d, base, noff, floor)
			got.append((name, slots))
		out.append(got)
		prev = base + 4 + size
	return out


def blob_base(d, s):
	"""(offset of the length word, length) of the name blob holding s."""
	for b in range(s - 1, max(0, s - 0x8000), -1):
		n = struct.unpack_from('<I', d, b)[0]
		if not 0x40 <= n <= 0x40000 or not b + 4 <= s < b + 4 + n:
			continue
		blk = d[b + 4:b + 4 + n]
		if blk[-1] != 0 or not 0x21 <= blk[0] <= 0x7e:
			continue
		if any(c and not 0x20 <= c <= 0x7e for c in blk):
			continue
		return b, n
	return None


def contribution(d, hi, noff, floor):
	"""(slots, position) for the record naming blob offset noff.

	Records lie in the same order as the names they point at, so a record
	is only accepted at or after the one taken for the previous name.  That
	is what keeps a small offset such as 0x24, which occurs all over the
	table as an ordinary field value, from matching the wrong record.
	"""
	want = struct.pack('<I', noff)
	for m in re.finditer(re.escape(want), d[floor:hi]):
		q = floor + m.start()
		for f in range(q - 0x30, q):
			if d[f] != 0x03 or d[f + 1] != 0x01:
				continue
			size = struct.unpack_from('<I', d, f + 2)[0]
			nrel = struct.unpack_from('<I', d, f + 6)[0]
			if 4 <= size <= 0x2000 and size % 4 == 0 and nrel == size // 4:
				return size // 4, q + 4
	return None, floor


# -- attribution -------------------------------------------------------------

def read_regions(path):
	out = []
	for line in open(path, encoding='utf-8'):
		if not line.startswith('0x'):
			continue
		f = line.rstrip('\n').split('\t')
		out.append((int(f[0], 16), int(f[1], 16), f[4]))
	return out


def region_of(regions, va):
	for a, b, name in regions:
		if a <= va <= b:
			return name
	return ''


def read_table(path):
	"""[(vtable va, slots, class, file)] from a table this script wrote."""
	out, seen = [], {}
	for line in open(path, encoding='utf-8'):
		if not line.startswith('0x'):
			continue
		f = (line.rstrip('\n').split('\t') + [''] * 7)[:7]
		va = int(f[0], 16)
		if va not in seen:
			seen[va] = [va, 0, f[5], f[6]]
			out.append(seen[va])
		seen[va][1] += 1
	return [tuple(x) for x in out]


def name_classes(vts, pure, chunks):
	"""{vtable rva: symbol} for the vtables the link table can place.

	A `??_7Class@@6BBase@@@` run is placed by matching its slot-count
	sequence against consecutive vtables.  Where more than one run of
	vtables has that shape the lowest unclaimed one is taken, which is a
	guess, and the listing under the table says so.

	An interface vtable, `??_7Iface@@6B@`, holds nothing but `__purecall`.
	The linker keeps one copy of it however many objects emit it, so it is
	looked for among the all-pure vtables that follow the class run, and
	failing that anywhere in the image if exactly one all-pure vtable has
	its slot count.
	"""
	allpure = [i for i, (a, sl, st) in enumerate(vts)
		   if sl and all(t == pure for t in sl)]
	out, taken, notes = {}, set(), []

	for chunk in chunks:
		own = [(n, c) for n, c in chunk if '6B' in n and not n.endswith('6B@')]
		iface = [(n, c) for n, c in chunk if n.endswith('6B@')]
		if not own or any(c is None for _, c in own):
			continue

		want = [c for _, c in own]
		hit = [i for i in range(len(vts) - len(want) + 1)
		       if [len(vts[i + k][1]) for k in range(len(want))] == want
		       and not (set(range(i, i + len(want))) & taken)]
		if not hit:
			notes.append((None, '%s: %s not matched'
				      % (own[0][0],
					 ' '.join(str(c) for c in want))))
			continue
		if len(hit) > 1:
			notes.append((None, '%s: %d runs have shape %s, '
				      'lowest taken'
				      % (own[0][0], len(hit),
					 ' '.join(str(c) for c in want))))
		at = hit[0]
		for k, (n, c) in enumerate(own):
			out[vts[at + k][0]] = n
			taken.add(at + k)

		nxt = at + len(want)
		while nxt in allpure and nxt not in taken:
			for j, (n, c) in enumerate(iface):
				if c == len(vts[nxt][1]):
					out[vts[nxt][0]] = n
					taken.add(nxt)
					iface.pop(j)
					break
			else:
				break
			nxt += 1
		for n, c in iface:
			cand = [i for i in allpure
				if len(vts[i][1]) == c and i not in taken]
			if len(cand) == 1:
				out[vts[cand[0]][0]] = n
				taken.add(cand[0])
			else:
				notes.append((n, '%s: %d all-pure vtables of '
					      '%s slots, none taken'
					      % (n, len(cand), c)))
	return out, [t for n, t in notes if n is None or n not in out.values()]


def carry(vts, other):
	"""{vtable rva: (class, file)} carried from another build's table.

	Both builds lay their vtables down in link order, so the ordered
	slot-count sequences align.  Only positions inside an exactly equal
	block are carried; a block the two builds disagree on is left blank
	rather than guessed at.
	"""
	a = [len(sl) for _, sl, _ in vts]
	b = [n for _, n, _, _ in other]
	sm = difflib.SequenceMatcher(a=a, b=b, autojunk=False)
	out = {}
	for tag, i1, i2, j1, j2 in sm.get_opcodes():
		if tag != 'equal':
			continue
		for k in range(i2 - i1):
			out[vts[i1 + k][0]] = (other[j1 + k][2], other[j1 + k][3])
	return out


# -- output ------------------------------------------------------------------

NOTE = """Every C++ vtable, every slot, and the function each slot points at.

  vtable  the address of slot 0
  slot    the index, so the address of the row is vtable + 4 * slot
  target  the dword in the slot, verbatim
  kind    empty, `pure` for __purecall, or `thunk:0xNN` for an adjustor
          thunk that subtracts NN from `this` before jumping
  final   target after the incremental linker's jump table and the
          adjustor thunk have been followed
  class   the decorated vtable symbol from the Debug link table, which
          names the class and the base interface the vtable is for
  file    the source file the constructor that installs it lies in, and
          two names separated by a comma where the stores fall either
          side of a region boundary, which puts the boundary wrong

A run of relocated dwords holding code addresses is a vtable when a
constructor stores its address into an object with `mov dword ptr
[reg+disp], offset`.  It ends at the next stored address in the same run
or where the run ends, so a vtable that is never installed is missed, and
where a missed one follows a found one the slot count is too high by its
length.  Where the two builds agree on a count neither is affected.

An adjustor thunk is a slot of a secondary base's vtable, so a class with
several of them is a class with multiple inheritance.  A vtable of one
slot is a class whose only virtual function is its destructor, and the
slot holds the compiler's deleting destructor or a thunk onto one.

There is no RTTI in the image, so no vtable names its own class.  `class`
is filled only from the Debug link table.  Its records carry a symbol
name and a contribution size but no address, so the slot count is exact
and the address it is matched to is not."""

CARRIED = """`class` and `file` are not read out of this image.  They are carried
from %s, position by position, over the runs where the two
builds' slot-count sequences match exactly.  %d of the %d vtables here
are inside such a run and the rest are left blank."""

FOLDED = """This image has no incremental linker jump table, so it is linked with
/OPT:ICF, which folds bodies that assemble identically.  Several slots
sharing one target is that folding and not an extraction error.  %d
slots reach %d distinct functions, and %d slots reach %d of them once
__purecall is set aside."""


def main():
	args, opt, rest = [], {}, list(sys.argv[1:])
	while rest:
		a = rest.pop(0)
		if a.startswith('--') and rest:
			opt[a[2:]] = rest.pop(0)
		else:
			args.append(a)
	if len(args) != 1:
		sys.stderr.write('usage: extract_vtables.py <image> '
				 '[--regions <tsv>] [--against <tsv>]\n')
		return 2

	path = args[0]
	pe = PE(path)
	vts = vtables(pe)
	jumps = jump_table(pe)

	slots = Counter()
	for _, sl, _ in vts:
		slots.update(sl)
	pure = slots.most_common(1)[0][0] if slots else -1

	regions = read_regions(opt['regions']) if 'regions' in opt else []
	other = read_table(opt['against']) if 'against' in opt else []

	chunks = link_vtables(pe)
	named, notes = name_classes(vts, pure, chunks)
	across = carry(vts, other) if other else {}

	note = NOTE
	if across:
		note += '\n\n' + CARRIED % (os.path.basename(opt['against']),
					    len(across), len(vts))
	if not jumps:
		bare = Counter(t for t in slots.elements() if t != pure)
		note += '\n\n' + FOLDED % (sum(slots.values()), len(slots),
					   sum(bare.values()), len(bare))

	out = tsv_header(os.path.basename(path), note)
	out.append('# vtable\tslot\ttarget\tkind\tfinal\tclass\tfile')

	nthunk = 0
	for rva, sl, sites in vts:
		cls = named.get(rva, '')
		# Conflicting installer regions may indicate a misplaced file boundary.
		# Retain both candidate filenames.
		src = ','.join(sorted(set(
			region_of(regions, pe.va(x)) for x in sites))) if regions else ''
		if not cls and not src:
			cls, src = across.get(rva, ('', ''))
		for i, t in enumerate(sl):
			kind, end = '', jumps.get(t, t)
			if t == pure:
				kind = 'pure'
			adj = adjustor(pe, end)
			if adj:
				kind = 'thunk:0x%x' % adj[0]
				end = jumps.get(adj[1], adj[1])
				nthunk += 1
			out.append('0x%08x\t%d\t0x%08x\t%s\t0x%08x\t%s\t%s'
				   % (pe.va(rva), i, pe.va(t), kind,
				      pe.va(end), cls, src))

	if chunks:
		out.append('#')
		out.append('# The Debug link table names these vtables.  It '
			   'gives the slot count')
		out.append('# exactly and no address, so `at` is this script\'s '
			   'match and not the')
		out.append('# linker\'s.')
		out.append('#')
		rev = {}
		for rva, n in named.items():
			rev.setdefault(n, []).append(rva)
		for chunk in chunks:
			for n, c in chunk:
				at = rev.get(n)
				out.append('#   %-42s %s slots\t%s'
					   % (n, c,
					      '0x%08x' % pe.va(at[0]) if at
					      else 'not placed'))
		for line in notes:
			out.append('#   note: ' + line)

	sys.stdout.write('\n'.join(out) + '\n')

	npure = sum(1 for _, sl, _ in vts if sl and all(t == pure for t in sl))
	sys.stderr.write('%s: %d vtables, %d slots, %d all-pure tables, '
			 '%d __purecall slots at 0x%08x, %d thunk slots\n'
			 % (os.path.basename(path), len(vts),
			    sum(len(sl) for _, sl, _ in vts), npure,
			    slots[pure], pe.va(pure), nthunk))
	sys.stderr.write('%s: %d distinct targets over %d slots, '
			 '%d named by the link table, %d carried across\n'
			 % (os.path.basename(path), len(slots),
			    sum(slots.values()), len(named), len(across)))
	return 0


if __name__ == '__main__':
	sys.exit(main())
