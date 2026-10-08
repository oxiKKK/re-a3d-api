
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Match debug release functions.\npython tools/analysis/match_debug_release_functions.py ref/a3dapi_33_dbg.dll ref/a3dapi_33_rtl.dll docs/llm/groundtruth > docs/llm/groundtruth/funcmap.tsv')
    raise SystemExit(0)
#
# match_debug_release_functions.py - pair every Debug function with its Retail counterpart.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Pair Debug (/Od /ZI /INCREMENTAL) and Retail (/O2 /OPT:ICF) addresses in
# funcmap.tsv. Anchor precedence follows pass order; accepted pairs are
# never replaced:
#
#   export: named exports and the PE entry point.
#   vtable: aligned slot-count/kind sequences, then slot targets. Isolated
#     one/two-slot matches require an installer match.
#   string/import: a reference unique to one function in each build.
#   graph: weighted caller/callee, string, import and constant matches;
#     require score >= 1.0 and a lead >= 0.5.
#   seq: corresponding unpaired callees in aligned call sequences;
#     independent votes must agree.
#   array: equal-length code-pointer arrays; reject any contradiction.
#   order: equal-sized gaps between paired neighbors (weakest).
#
# Compatibility requires equal retn argument bytes when available, Retail
# size between 1/40 and 8 times Debug size, and no more than twice as many
# Retail callers. Exclude Debug-only ASSERT/TRACE reporters and debug new.
#
# Confidence: high for named/slot/unique-reference anchors; medium for
# propagation with independent corroboration; low for position alone.
# Verify low-confidence pairs before citing them.
#
# Inlining, dead-code removal and CRT differences leave unpaired rows (-).
# ICF produces many-to-one pairs; fold counts Debug partners. The byte-scan
# call graph can mistake an immediate 0xE8 for an edge. Source regions may
# have approximate boundaries; file attribution does not affect pairing.
#
# Usage:
#	python tools/analysis/match_debug_release_functions.py ref/a3dapi_33_dbg.dll \
#	       ref/a3dapi_33_rtl.dll docs/llm/groundtruth \
#	       > docs/llm/groundtruth/funcmap.tsv
#

import sys
import os
import struct
import bisect
import difflib
from collections import Counter, defaultdict


def clean(text, limit=48):
	"""A string quoted into the evidence column: printable, one line,
	tab-free, truncated."""
	out = ''.join(c if 0x20 <= ord(c) < 0x7f else '\\x%02x' % ord(c)
		      for c in text[:limit])
	return out

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_image import PE, tsv_header


# -- inputs ------------------------------------------------------------------

class Row(object):
	__slots__ = ('addr', 'size', 'term', 'cbargs', 'sha1', 'corr', 'file')


def read_functions(path):
	rows = []
	for line in open(path, encoding='utf-8'):
		if not line.startswith('0x'):
			continue
		f = line.rstrip('\n').split('\t')
		r = Row()
		r.addr = int(f[0], 16)
		r.size = int(f[1], 16)
		r.term = f[2]
		r.cbargs = int(f[3])
		r.sha1 = f[5]
		r.corr = f[6]
		r.file = f[7] if len(f) > 7 else '-'
		rows.append(r)
	rows.sort(key=lambda r: r.addr)
	return rows


def read_vtables(path):
	"""[{va, slots: [(target, kind, final)], cls, file}], in address order."""
	vts, cur = [], None
	for line in open(path, encoding='utf-8'):
		if not line.startswith('0x'):
			continue
		f = (line.rstrip('\n').split('\t') + [''] * 7)[:7]
		va = int(f[0], 16)
		if cur is None or cur['va'] != va:
			cur = {'va': va, 'slots': [], 'cls': f[5], 'file': f[6]}
			vts.append(cur)
		cur['slots'].append((int(f[2], 16), f[3], int(f[4], 16)))
	return vts


# -- one build and its features ----------------------------------------------

class Build(object):
	def __init__(self, image, functable):
		self.name = os.path.basename(image)
		self.pe = PE(image)
		self.funcs = read_functions(functable)
		self.byaddr = {r.addr: r for r in self.funcs}
		self.starts = [r.addr for r in self.funcs]
		self.entries = set(self.starts)

		t = self.pe.sections[0]
		self.code_lo = self.pe.imagebase + t['rva']
		self.code_hi = self.code_lo + max(t['vsize'], t['rsize'])
		self.thunks = self._scan_ilt()
		self.iatmap = self.pe.iat()

		# Relocation sites as VAs, sorted, with their dword values.
		self.relocs = []
		for rva in self.pe.relocations():
			o = self.pe.rva_to_off(rva)
			if o is not None and o + 4 <= len(self.pe.data):
				self.relocs.append((self.pe.imagebase + rva,
						    self.pe.u32(o)))
		self.relocs.sort()
		self.relocvas = [va for va, _ in self.relocs]

		self.calls = defaultdict(Counter)	# caller -> callee
		self.callers = defaultdict(Counter)
		self.strings = defaultdict(set)		# addr -> texts
		self.strcarriers = defaultdict(set)	# text -> addrs
		self.imps = defaultdict(set)		# addr -> import names
		self.impcarriers = defaultdict(set)
		self.dats = defaultdict(set)		# addr -> data tokens
		self.datcarriers = defaultdict(set)
		self.trace = {}				# addr -> ordered items
		self._scan_features()

	def _scan_ilt(self):
		"""The incremental linker's jump table at the start of .text,
		one `jmp rel32` per function, padded with int3.  Retail has
		none and yields an empty map."""
		d = self.pe.data
		t = self.pe.sections[0]
		p, end = t['off'], t['off'] + min(t['vsize'], t['rsize'])
		out = {}
		while p < end:
			if d[p] == 0xcc:
				p += 1
			elif d[p] == 0xe9:
				src = self.pe.va(self.pe.off_to_rva(p))
				out[src] = src + 5 + struct.unpack_from(
					'<i', d, p + 1)[0]
				p += 5
			else:
				break
		return out

	def resolve(self, va, depth=8):
		while depth and va in self.thunks:
			va = self.thunks[va]
			depth -= 1
		return va

	def owner(self, va):
		i = bisect.bisect_right(self.starts, va) - 1
		if i >= 0 and va < self.funcs[i].addr + self.funcs[i].size:
			return self.funcs[i].addr
		return None

	def _scan_features(self):
		"""Per function: call and tail-jmp targets, imports called
		through the IAT, referenced strings, and referenced data
		constants, each also kept as an ordered trace of the body for
		the call-site sequence pass."""
		d = self.pe.data
		for f in self.funcs:
			o = self.pe.rva_to_off(f.addr - self.pe.imagebase)
			end = f.addr + f.size
			evts = []
			i = 0
			while i + 5 <= f.size:
				b = d[o + i]
				if b in (0xe8, 0xe9):
					t = f.addr + i + 5 + struct.unpack_from(
						'<i', d, o + i + 1)[0]
					t = self.resolve(t)
					if t in self.entries and \
					   not f.addr <= t < end:
						self.calls[f.addr][t] += 1
						evts.append((i, 'f', t))
						i += 5
						continue
				elif b == 0xff and i + 6 <= f.size and \
				     d[o + i + 1] in (0x15, 0x25):
					v = struct.unpack_from('<I', d, o + i + 2)[0]
					name = self.iatmap.get(v)
					if name:
						self.imps[f.addr].add(name)
						self.impcarriers[name].add(f.addr)
						evts.append((i, 'i', name))
						i += 6
						continue
				i += 1

			# Relocated dwords inside the body: code refs count as
			# call edges, data refs are strings where they read as
			# text and 8-byte constants otherwise.  Both builds
			# initialise the same globals with the same bytes, so
			# the constant is comparable even though the data
			# address is not.
			lo = bisect.bisect_left(self.relocvas, f.addr)
			hi = bisect.bisect_left(self.relocvas, end)
			for k in range(lo, hi):
				site, v = self.relocs[k]
				if self.code_lo <= v < self.code_hi:
					t = self.resolve(v)
					if t in self.entries and \
					   not f.addr <= t < end:
						self.calls[f.addr][t] += 1
						evts.append((site - f.addr,
							     'f', t))
				else:
					s = self.pe.cstr_at_rva(
						v - self.pe.imagebase)
					if s and len(s) >= 5:
						self.strings[f.addr].add(s)
						self.strcarriers[s].add(f.addr)
						evts.append((site - f.addr,
							     's', s))
					else:
						tok = self._data_token(v)
						if tok:
							self.dats[f.addr].add(tok)
							self.datcarriers[tok].add(f.addr)
							evts.append((site - f.addr,
								     'd', tok))
			evts.sort()
			self.trace[f.addr] = [(k, v) for _p, k, v in evts]

		for caller, cs in self.calls.items():
			for callee, n in cs.items():
				self.callers[callee][caller] += n

	def _data_token(self, va):
		"""Eight bytes of the pointed-at data, hex, or None where the
		address is unmapped or the bytes are all zero.  Zero-filled
		storage is not distinctive and mostly reads through to another
		build's unrelated global."""
		o = self.pe.rva_to_off(va - self.pe.imagebase)
		if o is None or o + 8 > len(self.pe.data):
			return None
		b = self.pe.data[o:o + 8]
		if b == b'\0' * 8:
			return None
		return b.hex()


# -- the pair pool -----------------------------------------------------------

# The passes run strongest first and a pair is never replaced, so anchor
# precedence is the pass order in main().  A later pass disagreeing with an
# existing pair is recorded as a conflict and changes nothing.

class Pool(object):
	def __init__(self, dbg, rtl):
		self.dbg, self.rtl = dbg, rtl
		self.pair = {}		# dbg addr -> rtl addr
		self.how = {}		# dbg addr -> (anchor, evidence)
		self.rmap = defaultdict(set)
		self.conflicts = []

	def compat(self, d, r):
		df, rf = self.dbg.byaddr[d], self.rtl.byaddr[r]
		if df.term in ('ret', 'retn') and rf.term in ('ret', 'retn'):
			return df.cbargs == rf.cbargs
		return True

	def plausible(self, d, r):
		"""A size sanity bound for the weak anchors.  Over the pairs
		the strong anchors settled, the Retail body runs from 0.027 to
		6.1 times the Debug body (a folded getter against a body that
		swallowed its callees), so only a ratio outside 1/40 to 8 is
		rejected.  Wide on purpose: it exists to stop a hub function
		pairing with a stub, not to arbitrate close calls."""
		df, rf = self.dbg.byaddr[d], self.rtl.byaddr[r]
		if df.size > 40 * rf.size or rf.size > 8 * df.size:
			return False
		# Reject excessive Retail caller counts to avoid pairing a local helper
		# with a shared CRT function. Allow slack for tail-jump scan differences.
		dc = len(self.dbg.callers.get(d, ()))
		rc = len(self.rtl.callers.get(r, ()))
		return rc <= 2 * dc + 8

	def propose(self, d, r, anchor, ev):
		if d not in self.dbg.byaddr or r not in self.rtl.byaddr:
			self.conflicts.append(
				(d, r, anchor, 'address not in the function table'))
			return False
		if d in self.pair:
			if self.pair[d] != r:
				self.conflicts.append((d, r, anchor,
					'already paired with 0x%08x by %s'
					% (self.pair[d], self.how[d][0])))
			return False
		if not self.compat(d, r):
			self.conflicts.append((d, r, anchor,
				'return convention mismatch'))
			return False
		self.pair[d] = r
		self.how[d] = (anchor, ev)
		self.rmap[r].add(d)
		return True


# -- anchor passes -----------------------------------------------------------

def pass_exports(pool):
	dexp = {name: pool.dbg.resolve(pool.dbg.pe.imagebase + rva)
		for _o, name, rva in pool.dbg.pe.exports()}
	rexp = {name: rva + pool.rtl.pe.imagebase
		for _o, name, rva in pool.rtl.pe.exports()}
	for name, d in sorted(dexp.items()):
		if name in rexp:
			pool.propose(d, rexp[name], 'export', name)
	dep = pool.dbg.resolve(pool.dbg.pe.imagebase + pool.dbg.pe.entrypoint)
	rep = pool.rtl.pe.imagebase + pool.rtl.pe.entrypoint
	pool.propose(dep, rep, 'entry', 'PE entry point')


def vt_key(v):
	return (len(v['slots']), tuple(k for _t, k, _f in v['slots']))


def vt_files(v):
	return set(v['file'].split(',')) - {'', '-'}


def pair_vtables(dvt, rvt):
	"""[(i, j)] pairs plus the unpaired of both sides.

	Alignment is on (slot count, slot kinds), which separates an all-pure
	interface vtable from a concrete one of the same size and carries the
	adjustor deltas, so a pair agrees on every slot's kind by construction.
	Leftovers are matched by that same key plus the installing file, then
	by key alone where the leftover groups are the same size, in order.
	"""
	dk = [vt_key(v) for v in dvt]
	rk = [vt_key(v) for v in rvt]
	sm = difflib.SequenceMatcher(a=dk, b=rk, autojunk=False)
	pairs, di, rj = [], set(), set()
	for tag, i1, i2, j1, j2 in sm.get_opcodes():
		if tag != 'equal':
			continue
		# A single vtable of one or two slots matching alone, with both
		# neighbours differing, carries no information of its own: the
		# builds' .rdata orders diverge locally, and a lone small count
		# often matches by coincidence.  Those are left to the installer
		# pass, which pairs a vtable through the
		# constructor that stores it.
		if i2 - i1 == 1 and dk[i1][0] <= 2:
			continue
		for k in range(i2 - i1):
			pairs.append((i1 + k, j1 + k))
			di.add(i1 + k)
			rj.add(j1 + k)

	dleft = [i for i in range(len(dvt)) if i not in di]
	rleft = [j for j in range(len(rvt)) if j not in rj]

	# By key and installing file.
	for i in list(dleft):
		cands = [j for j in rleft if rk[j] == dk[i] and
			 vt_files(rvt[j]) and vt_files(dvt[i]) & vt_files(rvt[j])]
		if len(cands) == 1:
			pairs.append((i, cands[0]))
			dleft.remove(i)
			rleft.remove(cands[0])

	# By key alone, where the two sides have the same number left, in
	# address order.  Keys of one or two slots stay out for the reason
	# above.
	dgrp, rgrp = defaultdict(list), defaultdict(list)
	for i in dleft:
		dgrp[dk[i]].append(i)
	for j in rleft:
		rgrp[rk[j]].append(j)
	for key, ds in dgrp.items():
		rs = rgrp.get(key, [])
		if key[0] > 2 and len(ds) == len(rs):
			for i, j in zip(ds, rs):
				pairs.append((i, j))
				dleft.remove(i)
				rleft.remove(j)

	pairs.sort()
	return pairs, dleft, rleft


def vt_installers(build, vts):
	"""{vtable va: functions holding a relocated reference to it}.  The
	store that installs a vtable lies in a constructor or destructor, so
	a paired installer places a vtable the alignment could not."""
	want = {v['va']: set() for v in vts}
	for site, val in build.relocs:
		if build.code_lo <= site < build.code_hi and val in want:
			o = build.owner(site)
			if o is not None:
				want[val].add(o)
	return want


def pass_vtables2(pool, dvt, rvt, dleft, rleft, dinst, rinst):
	"""Pair leftover vtables through their installers.  A Debug vtable
	whose installer is paired with an installer of exactly one leftover
	Retail vtable of the same shape is that vtable, however the .rdata
	orders diverge.  Runs inside the fixpoint because the installers are
	mostly paired by the graph."""
	accepted = 0
	for i in list(dleft):
		key = vt_key(dvt[i])
		hits = [j for j in rleft
			if vt_key(rvt[j]) == key and
			any(a in pool.pair and
			    pool.pair[a] in rinst[rvt[j]['va']]
			    for a in dinst[dvt[i]['va']])]
		if len(hits) != 1:
			continue
		j = hits[0]
		dleft.remove(i)
		rleft.remove(j)
		for k, (ds, rs) in enumerate(zip(dvt[i]['slots'],
						 rvt[j]['slots'])):
			if ds[1] == 'pure':
				continue
			if pool.propose(ds[2], rs[2], 'vtable',
					'vt dbg:0x%08x/rtl:0x%08x slot %d, '
					'paired by installer'
					% (dvt[i]['va'], rvt[j]['va'], k)):
				accepted += 1
	return accepted


def pass_vtables(pool, dvt, rvt, vpairs):
	"""Every non-pure slot of every paired vtable proposes a pair.  The
	proposals are collected as votes first, because one Debug function
	fills many slots, and a function whose slots disagree about its Retail
	counterpart is a misalignment; it is reported and left unpaired."""
	votes = defaultdict(Counter)
	where = defaultdict(list)
	for i, j in vpairs:
		dv, rv = dvt[i], rvt[j]
		for k, (dslot, rslot) in enumerate(zip(dv['slots'], rv['slots'])):
			if dslot[1] == 'pure':
				continue
			votes[dslot[2]][rslot[2]] += 1
			where[(dslot[2], rslot[2])].append(
				'vt dbg:0x%08x/rtl:0x%08x slot %d'
				% (dv['va'], rv['va'], k))
	for d, ctr in sorted(votes.items()):
		if len(ctr) > 1:
			pool.conflicts.append((d, ctr.most_common(1)[0][0],
				'vtable', 'slots disagree: %s'
				% ', '.join('0x%08x x%d' % (r, n)
					    for r, n in ctr.most_common())))
			continue
		r = next(iter(ctr))
		pool.propose(d, r, 'vtable', where[(d, r)][0])


def pass_unique(pool, kind):
	"""Strings or imports carried by exactly one function on each side."""
	if kind == 'string':
		dcar, rcar = pool.dbg.strcarriers, pool.rtl.strcarriers
	else:
		dcar, rcar = pool.dbg.impcarriers, pool.rtl.impcarriers
	for text in sorted(set(dcar) & set(rcar)):
		ds, rs = dcar[text], rcar[text]
		if len(ds) != 1 or len(rs) != 1:
			continue
		d, r = next(iter(ds)), next(iter(rs))
		if d in pool.pair:
			if pool.pair[d] != r:
				pool.conflicts.append((d, r, kind,
					'"%s" disagrees with the %s pair 0x%08x'
					% (clean(text, 40), pool.how[d][0],
					   pool.pair[d])))
			continue
		ev = text if kind == 'import' else '"%s"' % clean(text)
		pool.propose(d, r, kind, ev)


def tokens_dbg(pool, d):
	out = set()
	for c in pool.dbg.calls.get(d, ()):
		if c in pool.pair:
			out.add(('c', pool.pair[c]))
	callers = pool.dbg.callers.get(d, ())
	if len(callers) <= 16:
		for c in callers:
			if c in pool.pair:
				out.add(('r', pool.pair[c]))
	for s in pool.dbg.strings.get(d, ()):
		if s in pool.rtl.strcarriers:
			out.add(('s', s))
	for n in pool.dbg.imps.get(d, ()):
		out.add(('i', n))
	for t in pool.dbg.dats.get(d, ()):
		if t in pool.rtl.datcarriers:
			out.add(('d', t))
	return out


def tokens_rtl(pool, r):
	out = set()
	for c in pool.rtl.calls.get(r, ()):
		if c in pool.rmap:
			out.add(('c', c))
	callers = pool.rtl.callers.get(r, ())
	if len(callers) <= 16:
		for c in callers:
			if c in pool.rmap:
				out.add(('r', c))
	for s in pool.rtl.strings.get(r, ()):
		out.add(('s', s))
	for n in pool.rtl.imps.get(r, ()):
		out.add(('i', n))
	for t in pool.rtl.dats.get(r, ()):
		out.add(('d', t))
	return out


def pass_graph(pool, dbg_pool, rtl_pool):
	"""One voting round.  Returns how many pairs it accepted."""
	rtok = {}
	index = defaultdict(list)
	for r in rtl_pool:
		rtok[r] = tokens_rtl(pool, r)
		for t in rtok[r]:
			index[t].append(r)

	dtok = {d: tokens_dbg(pool, d) for d in dbg_pool}
	dcount = Counter(t for ts in dtok.values() for t in ts)

	accepted = 0
	for d in sorted(dbg_pool):
		ts = dtok[d]
		if not ts:
			continue
		score = Counter()
		for t in ts:
			carriers = index.get(t, ())
			if not carriers or len(carriers) > 64:
				continue
			w = 1.0 / max(dcount[t], len(carriers))
			for r in carriers:
				score[r] += w
		if not score:
			continue
		best = score.most_common(2)
		r, s = best[0]
		second = best[1][1] if len(best) > 1 else 0.0
		if s >= 1.0 and s - second >= 0.5 and \
		   pool.compat(d, r) and pool.plausible(d, r):
			shared = ts & rtok[r]
			ev = 'score %.2f: %s' % (s, ' '.join(sorted(
				'%s:0x%08x' % (k, v) if k in 'cr'
				else ('i:%s' % v if k == 'i'
				      else 's:"%s"' % clean(v, 24))
				for k, v in shared)[:6]))
			if pool.propose(d, r, 'graph', ev):
				accepted += 1
	return accepted


def pass_seq(pool, eligible_d, eligible_r):
	"""Call-site sequence alignment inside every paired function.

	Both compilers emit a body's calls, string references and constant
	references in source order, so the two traces of a paired function
	align.  Items equal across the builds (a paired callee, the same
	import, the same string or constant) anchor the alignment, and a
	replaced stretch holding the same number of unpaired callees on both
	sides pairs them position by position.  Every paired function that
	witnesses the same candidate pair votes for it; a candidate is
	accepted only when all its votes agree and the conventions do.

	The limit: /O2 can reorder or inline a call away, which changes the
	counts, and the stretch is skipped.  A helper inlined into its only
	Retail caller produces no vote at all."""
	votes = defaultdict(Counter)
	witness = {}
	for d, r in sorted(pool.pair.items()):
		ta = pool.dbg.trace.get(d, ())
		tb = pool.rtl.trace.get(r, ())
		if not ta or not tb:
			continue
		ka = [('F', pool.pair[v]) if k == 'f' and v in pool.pair
		      else (('uD', v) if k == 'f' else (k, v))
		      for k, v in ta]
		kb = [('F', v) if k == 'f' and v in pool.rmap
		      else (('uR', v) if k == 'f' else (k, v))
		      for k, v in tb]
		sm = difflib.SequenceMatcher(a=ka, b=kb, autojunk=False)
		for tag, i1, i2, j1, j2 in sm.get_opcodes():
			if tag != 'replace':
				continue
			da = [v for k, v in ka[i1:i2] if k == 'uD'
			      and v in eligible_d]
			rb = [v for k, v in kb[j1:j2] if k == 'uR'
			      and v in eligible_r]
			if not da or len(da) != len(rb):
				continue
			for a, b in zip(da, rb):
				votes[a][b] += 1
				witness.setdefault((a, b),
					'trace of dbg:0x%08x/rtl:0x%08x'
					% (d, r))
	accepted = 0
	for a, ctr in sorted(votes.items()):
		if a in pool.pair or len(ctr) != 1:
			continue
		b = next(iter(ctr))
		if b in pool.rmap or not pool.plausible(a, b):
			continue
		if pool.propose(a, b, 'seq', witness[(a, b)]):
			accepted += 1
	return accepted


def code_ptr_runs(build):
	"""Runs of consecutive relocated code pointers outside .text.  This is
	what a vtable looks like before the installed-store test, so it also
	catches callback tables and dispatch arrays that no constructor
	installs."""
	runs, cur, prev = [], [], None
	for site, v in build.relocs:
		if build.code_lo <= site < build.code_hi:
			continue
		t = build.resolve(v)
		ok = t in build.entries
		if ok and prev is not None and site == prev + 4:
			cur.append(t)
		else:
			if len(cur) > 1:
				runs.append(cur)
			cur = [t] if ok else []
		prev = site if ok else None
	if len(cur) > 1:
		runs.append(cur)
	return runs


def pass_arrays(pool, druns, rruns, eligible_d, eligible_r):
	"""Align the two builds' code-pointer arrays by run length and pair
	the elements.  A run pair is used only while nothing in it contradicts
	the map so far: one element already paired elsewhere discards the
	whole run pair as a misalignment.  Votes must be unanimous, as in the
	sequence pass."""
	sm = difflib.SequenceMatcher(a=[len(x) for x in druns],
				     b=[len(x) for x in rruns],
				     autojunk=False)
	votes = defaultdict(Counter)
	witness = {}
	for tag, i1, i2, j1, j2 in sm.get_opcodes():
		if tag != 'equal':
			continue
		for k in range(i2 - i1):
			da, rb = druns[i1 + k], rruns[j1 + k]
			if any(a in pool.pair and pool.pair[a] != b
			       for a, b in zip(da, rb)):
				continue
			for a, b in zip(da, rb):
				if a in pool.pair or a not in eligible_d or \
				   b not in eligible_r:
					continue
				votes[a][b] += 1
				witness.setdefault((a, b),
					'code-pointer array, run of %d'
					% len(da))
	accepted = 0
	for a, ctr in sorted(votes.items()):
		if a in pool.pair or len(ctr) != 1:
			continue
		b = next(iter(ctr))
		if not pool.plausible(a, b):
			continue
		if pool.propose(a, b, 'array', witness[(a, b)]):
			accepted += 1
	return accepted


def pass_order(pool, eligible_d, eligible_r):
	"""Between two paired neighbours, equal counts pair positionally."""
	paired = sorted((d, pool.pair[d]) for d in pool.pair)
	dstarts = [f.addr for f in pool.dbg.funcs]
	rstarts = [f.addr for f in pool.rtl.funcs]
	accepted = 0
	for (d1, r1), (d2, r2) in zip(paired, paired[1:]):
		if r2 <= r1:
			continue
		dw = [a for a in dstarts[bisect.bisect_right(dstarts, d1):
					 bisect.bisect_left(dstarts, d2)]
		      if a in eligible_d and a not in pool.pair]
		rw = [a for a in rstarts[bisect.bisect_right(rstarts, r1):
					 bisect.bisect_left(rstarts, r2)]
		      if a in eligible_r and a not in pool.rmap]
		if not dw or len(dw) != len(rw) or len(dw) > 10:
			continue
		if not all(pool.compat(a, b) and pool.plausible(a, b)
			   for a, b in zip(dw, rw)):
			continue
		for a, b in zip(dw, rw):
			if pool.propose(a, b, 'order',
					'window 0x%08x..0x%08x, %d each side'
					% (d1, d2, len(dw))):
				accepted += 1
	return accepted


def corroboration(pool, d, r):
	"""Independent witnesses a pair has beyond the anchor that made it:
	shared strings, imports and data constants, plus callees and callers
	that are themselves paired and agree across the pair.  Recomputed on
	the finished map, so it can count neighbours that were paired later
	than the pair itself."""
	n = len(pool.dbg.strings.get(d, set()) & pool.rtl.strings.get(r, set()))
	n += len(pool.dbg.imps.get(d, set()) & pool.rtl.imps.get(r, set()))
	n += len(pool.dbg.dats.get(d, set()) & pool.rtl.dats.get(r, set()))
	rc = set(pool.rtl.calls.get(r, ()))
	n += sum(1 for c in pool.dbg.calls.get(d, ())
		 if c in pool.pair and pool.pair[c] in rc)
	rr = set(pool.rtl.callers.get(r, ()))
	n += sum(1 for c in pool.dbg.callers.get(d, ())
		 if c in pool.pair and pool.pair[c] in rr)
	return n


STRONG = frozenset(('export', 'entry', 'vtable', 'string', 'import'))


def confidence(pool, d, r, anchor):
	"""high for the anchors that identify a function by name or slot,
	medium for a propagated pair with at least one independent witness,
	low for a purely positional pair nothing else corroborates."""
	if anchor in STRONG:
		return 'high'
	return 'medium' if corroboration(pool, d, r) else 'low'


# -- main --------------------------------------------------------------------

def main():
	if len(sys.argv) != 4:
		sys.stderr.write('usage: match_debug_release_functions.py <debug image> '
				 '<retail image> <groundtruth dir>\n')
		return 2

	gt = sys.argv[3]
	dbg = Build(sys.argv[1], os.path.join(gt, 'functions-dbg.tsv'))
	rtl = Build(sys.argv[2], os.path.join(gt, 'functions-rtl.tsv'))
	dvt = read_vtables(os.path.join(gt, 'vtables-dbg.tsv'))
	rvt = read_vtables(os.path.join(gt, 'vtables-rtl.tsv'))

	pool = Pool(dbg, rtl)
	pass_exports(pool)
	vpairs, vdleft, vrleft = pair_vtables(dvt, rvt)
	pass_vtables(pool, dvt, rvt, vpairs)
	pass_unique(pool, 'string')
	pass_unique(pool, 'import')

	# EH plumbing is compiler-generated per function and is not paired.
	eh_d = {f.addr for f in dbg.funcs if 'eh' in f.corr.split(',')}
	eh_r = {f.addr for f in rtl.funcs if 'eh' in f.corr.split(',')}
	eligible_d = set(dbg.entries) - eh_d
	eligible_r = set(rtl.entries) - eh_r

	# Exclude Debug-only reporters and debug new identified by filelines-dbg.tsv
	# before positional matching.
	dbg_only = set()
	fl = os.path.join(gt, 'filelines-dbg.tsv')
	if os.path.exists(fl):
		for line in open(fl, encoding='utf-8'):
			if not line.startswith('0x'):
				continue
			site = int(line.split('	', 1)[0], 16)
			o = dbg.pe.rva_to_off(site - dbg.pe.imagebase)
			if o is not None and dbg.pe.data[o] == 0xe8:
				t = dbg.resolve(site + 5 + struct.unpack_from(
					'<i', dbg.pe.data, o + 1)[0])
				if t in dbg.entries:
					dbg_only.add(t)
	eligible_d -= dbg_only

	druns = code_ptr_runs(dbg)
	rruns = code_ptr_runs(rtl)
	dinst = vt_installers(dbg, dvt)
	rinst = vt_installers(rtl, rvt)

	rounds = []
	for _round in range(60):
		n = pass_graph(pool,
			       [d for d in eligible_d if d not in pool.pair],
			       [r for r in eligible_r if r not in pool.rmap])
		n += pass_seq(pool, eligible_d, eligible_r)
		n += pass_vtables2(pool, dvt, rvt, vdleft, vrleft, dinst, rinst)
		n += pass_arrays(pool, druns, rruns, eligible_d, eligible_r)
		n += pass_order(pool, eligible_d, eligible_r)
		rounds.append(n)
		if not n:
			break

	# Rows, Debug side first in address order, then Retail-only.
	name = '%s + %s' % (dbg.name, rtl.name)
	note = ('The Debug-to-Retail function map.  `dbg` and `rtl` are the\n'
		'entry addresses from functions-dbg.tsv and functions-rtl.tsv;\n'
		'`-` on either side is a function with no counterpart found.\n'
		'\n'
		'`anchor` is the evidence class that settled the pair, in\n'
		'decreasing strength: export and entry are paired by name,\n'
		'vtable by the aligned vtable tables slot by slot, string and\n'
		'import by a reference unique on both sides, graph by paired\n'
		'neighbours voting, seq by call-site order inside a paired\n'
		'function, array by a code-pointer table outside .text, order\n'
		'by position between two paired neighbours with equal counts.\n'
		'\n'
		'`conf` grades the pair.  high: the anchor identifies the\n'
		'function by name or slot.  medium: a propagated pair with at\n'
		'least one witness beyond its anchor (a shared string, import\n'
		'or constant, or an agreeing paired neighbour).  low: a purely\n'
		'positional pair nothing else corroborates; re-establish it\n'
		'before citing it.\n'
		'\n'
		'`fold` is how many Debug functions share the row\'s Retail\n'
		'address; above 1 it is an /OPT:ICF fold, legitimately\n'
		'many-to-one.  `file` is the owning source file per\n'
		'regions-dbg.tsv, informational only, and carries that\n'
		'table\'s known boundary errors.  `evidence` is one witness\n'
		'for the pair, not all of them.\n'
		'\n'
		'An unpaired Debug function is expected in bulk: the Debug\n'
		'CRT, EH plumbing (`eh`), the ASSERT and TRACE machinery, and\n'
		'bodies inlined away or discarded as unreferenced by the\n'
		'Retail link.  An unpaired Retail function had no anchor\n'
		'reach it.\n'
		'\n'
		'The A3dGeom.cpp vtables dbg:0x10127624, dbg:0x10127628 and\n'
		'dbg:0x101276d4 pair with nothing because their class\n'
		'hierarchy is dead code: the installing constructor\n'
		'dbg:0x1000e9e0 has no caller, the 15 non-pure slot targets\n'
		'have no caller either, and Retail holds no code-pointer run\n'
		'of 30 or 36 slots anywhere, installed or not, so the Retail\n'
		'link discarded the hierarchy outright.')
	hdr = tsv_header(name, note)
	hdr.append('# dbg\trtl\tanchor\tconf\tfold\tfile\tevidence')
	print('\n'.join(hdr))

	nconf = Counter()
	for f in dbg.funcs:
		d = f.addr
		if d in pool.pair:
			r = pool.pair[d]
			anchor, ev = pool.how[d]
			conf = confidence(pool, d, r, anchor)
			nconf[conf] += 1
			print('0x%08x\t0x%08x\t%s\t%s\t%d\t%s\t%s'
			      % (d, r, anchor, conf, len(pool.rmap[r]),
				 f.file, ev))
		else:
			why = 'eh' if d in eh_d else ''
			print('0x%08x\t-\tunpaired\t-\t0\t%s\t%s'
			      % (d, f.file, why))
	for f in rtl.funcs:
		if f.addr not in pool.rmap:
			why = 'eh' if f.addr in eh_r else ''
			print('-\t0x%08x\tunpaired\t-\t0\t%s\t%s'
			      % (f.addr, f.file, why))

	# Summary, to stderr.
	byanchor = Counter(a for a, _ in pool.how.values())
	folds = {r: ds for r, ds in pool.rmap.items() if len(ds) > 1}
	aureal_d = [f for f in dbg.funcs if f.file not in ('-', '')]
	aureal_paired = sum(1 for f in aureal_d if f.addr in pool.pair)
	sys.stderr.write('paired %d of %d Debug functions onto %d Retail '
			 'addresses (%d of %d Retail rows)\n'
			 % (len(pool.pair), len(dbg.funcs), len(pool.rmap),
			    len(pool.rmap), len(rtl.funcs)))
	sys.stderr.write('by anchor: %s\n' % dict(byanchor))
	sys.stderr.write('by confidence: %s\n' % dict(nconf))
	sys.stderr.write('Aureal-region Debug functions: %d of %d paired\n'
			 % (aureal_paired, len(aureal_d)))
	sys.stderr.write('%d folds (%d Debug functions onto them)\n'
			 % (len(folds), sum(len(v) for v in folds.values())))
	sys.stderr.write('vtables: %d paired (%d of them by installer), '
			 '%d Debug-only, %d Retail-only\n'
			 % (len(dvt) - len(vdleft),
			    len(dvt) - len(vdleft) - len(vpairs),
			    len(vdleft), len(vrleft)))
	for i in vdleft:
		v = dvt[i]
		sys.stderr.write('  Debug-only vtable dbg:0x%08x, %d slots, '
				 '%s\n' % (v['va'], len(v['slots']),
					   v['file'] or v['cls'] or '?'))
	for j in vrleft:
		v = rvt[j]
		sys.stderr.write('  Retail-only vtable rtl:0x%08x, %d slots, '
				 '%s\n' % (v['va'], len(v['slots']),
					   v['file'] or v['cls'] or '?'))
	sys.stderr.write('pairs per round: %s\n' % rounds)
	conflicts = sorted(set(pool.conflicts))
	sys.stderr.write('%d distinct rejected proposals\n' % len(conflicts))
	for d, r, anchor, msg in conflicts[:80]:
		sys.stderr.write('  0x%08x / 0x%08x (%s): %s\n'
				 % (d, r, anchor, msg))
	return 0


if __name__ == '__main__':
	sys.exit(main())
