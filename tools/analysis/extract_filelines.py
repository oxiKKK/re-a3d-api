
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Extract filelines.\npython tools/analysis/extract_filelines.py ref/a3dapi_33_dbg.dll > out.tsv')
    raise SystemExit(0)
#
# extract_filelines.py - every call site in a debug build that carries a source
# file and a line number.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Source-location arguments identify source files, minimum line counts
# and code regions. The supported forms are:
#
#	ASSERT	reporter(2, file, line, 0, expr)		5 args
#	TRACE	reporter(level, file, line, fmt, ...)	varargs, level 0 or 1
#	new	operator new(cb, nBlockUse, file, line)	4 args, debug CRT
#
# Allocation sites identify files with few or no asserts, including much
# of the 3.3 Debug geometry code.
#
# The line argument is encoded differently in the two generations, and the
# difference is a compiler switch rather than a code change:
#
#	2.12	/Zi	push <imm>, the literal
#	3.3	/ZI	movsx reg, word ptr [base] / add reg, <delta> / push reg
#
# Under /ZI (edit and continue) MSVC materialises __LINE__ as a per-block base
# word in .data plus a constant, so an edit can shift lines without a
# recompile.  The base words are initialised data and are read out of the
# image, so both forms yield the same number.
#
# /INCREMENTAL also routes calls through a jump table, so a call target is
# followed through `jmp rel32` before it is compared against anything.
#
# The two helper functions are found by their own call sites rather than named
# by address, so this runs unchanged on any of the debug builds.
#
# Usage:
#	python tools/analysis/extract_filelines.py ref/a3dapi_33_dbg.dll > out.tsv
#

import sys
import os
import re
import struct
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_image import PE, tsv_header

CALL = re.compile(rb'\xe8....', re.S)


def thunk_map(pe):
	"""{thunk rva: final rva} for the incremental linker's jump table."""
	out = {}
	for m in re.finditer(rb'\xe9....', pe.data, re.S):
		o = m.start()
		src = pe.off_to_rva(o)
		if src is None:
			continue
		dst = src + 5 + pe.i32(o + 1)
		out[src] = dst
	return out


def resolve(rva, thunks, depth=4):
	while depth and rva in thunks:
		rva = thunks[rva]
		depth -= 1
	return rva


def push_line(pe, p):
	"""Decode the `push <line>` ending at file offset p. -> (line, next_p)."""
	d = pe.data

	if 0x50 <= d[p - 1] <= 0x57:			# push reg  (/ZI)
		reg = d[p - 1] - 0x50
		q = p - 1
		delta = 0
		if d[q - 3] == 0x83 and d[q - 2] == 0xc0 + reg:
			delta = struct.unpack_from('<b', d, q - 1)[0]
			q -= 3
		elif reg == 0 and d[q - 5] == 0x05:
			delta = pe.i32(q - 4)
			q -= 5
		elif d[q - 6] == 0x81 and d[q - 5] == 0xc0 + reg:
			delta = pe.i32(q - 4)
			q -= 6
		if d[q - 7] != 0x0f or d[q - 6] != 0xbf:
			return None
		if d[q - 5] != 0x05 | (reg << 3):	# [disp32] form only
			return None
		word = pe.u16_at_rva(pe.u32(q - 4) - pe.imagebase)
		if word is None:
			return None
		return word + delta, q - 7

	if d[p - 5] == 0x68:				# push imm32  (/Zi)
		return pe.u32(p - 4), p - 5
	if d[p - 2] == 0x6a:				# push imm8   (/Zi)
		return d[p - 1], p - 2
	return None


def parse_reporter(pe, call_off):
	"""reporter(level, file, line, ...) -> (level, file, line, expr, p)."""
	d = pe.data
	if d[call_off - 2] != 0x6a:			# push <level>
		return None
	level = d[call_off - 1]
	p = call_off - 2

	if d[p - 5] != 0x68:				# push offset <file>
		return None
	file_rva = pe.u32(p - 4)
	p -= 5

	got = push_line(pe, p)
	if got is None:
		return None
	line, p = got

	expr = None
	if level == 2 and d[p - 2:p] == b'\x6a\x00' and d[p - 7] == 0x68:
		expr = pe.u32(p - 6)			# push 0 ; push offset <expr>
	return level, file_rva, line, expr


def parse_new(pe, call_off):
	"""operator new(cb, nBlockUse, file, line) -> (cb, file, line).

	Pushed right to left, so the size is nearest the call and the line is
	furthest from it.
	"""
	d = pe.data

	# Allocation size may be computed between pushes. Anchor on file/nBlockUse
	# and search for the known allocation target within the instruction window.
	anchor = None
	for i in range(call_off - 48, call_off - 6):
		if d[i] == 0x68 and d[i + 5] == 0x6a:
			anchor = i
	if anchor is None:
		return None

	file_rva = pe.u32(anchor + 1)

	if d[call_off - 2] == 0x6a:			# push <cb>
		cb = d[call_off - 1]
	elif d[call_off - 5] == 0x68:
		cb = pe.u32(call_off - 4)
	else:
		cb = -1					# computed at run time

	got = push_line(pe, anchor)			# push <line>
	if got is None:
		return None
	return cb, file_rva, got[0]


def main():
	if len(sys.argv) != 2:
		sys.stderr.write('usage: extract_filelines.py <image>\n')
		return 2

	path = sys.argv[1]
	pe = PE(path)
	thunks = thunk_map(pe)

	# Every call, with its target followed through the jump table.
	calls = []
	for m in CALL.finditer(pe.data):
		o = m.start()
		src = pe.off_to_rva(o)
		if src is None:
			continue
		calls.append((o, resolve(src + 5 + pe.i32(o + 1), thunks)))

	# Find the two helpers by which target the most sites parse against.  A
	# vote only counts when the file operand really is a source filename,
	# which is what keeps an unrelated four-argument call from winning.
	def names_a_source(file_rva):
		s = pe.cstr_at_rva(file_rva - pe.imagebase, 300)
		return bool(s) and s.lower().endswith(('.cpp', '.c', '.h'))

	rep_votes, new_votes = Counter(), Counter()
	for o, t in calls:
		got = parse_reporter(pe, o)
		if got and names_a_source(got[1]):
			rep_votes[t] += 1
		got = parse_new(pe, o)
		if got and names_a_source(got[1]):
			new_votes[t] += 1
	reporter = rep_votes.most_common(1)[0][0] if rep_votes else None
	opnew = None
	for t, _ in new_votes.most_common(8):
		if t != reporter:
			opnew = t
			break

	rows, unparsed, traces = [], 0, 0
	for o, t in calls:
		if t not in (reporter, opnew):
			continue
		site = pe.off_to_rva(o)

		if t == reporter:
			got = parse_reporter(pe, o)
			if got is None:
				# TRACE passes zero for the file, so the operand
				# is `push 0` rather than a pointer and the shape
				# does not parse.  Those are counted, not lost.
				if pe.data[o - 2] == 0x6a and pe.data[o - 1] in (0, 1):
					traces += 1
				else:
					unparsed += 1
				continue
			level, file_rva, line, expr_rva = got
			if level != 2:
				traces += 1
				continue
			kind = 'assert'
			text = pe.cstr_at_rva(expr_rva - pe.imagebase) if expr_rva else ''
		else:
			got = parse_new(pe, o)
			if got is None:
				unparsed += 1
				continue
			cb, file_rva, line = got
			kind = 'new'
			text = '0x%x' % cb if cb >= 0 else ''

		fname = pe.cstr_at_rva(file_rva - pe.imagebase)
		if fname is None:
			unparsed += 1
			continue
		fname = os.path.basename(fname.replace('\\', '/'))
		rows.append((pe.va(site), kind, fname, line, text or ''))

	rows.sort()

	note = ('Every call site carrying a __FILE__ and __LINE__ pair.  `addr` is\n'
		'the call, so it lies inside the function that made it.\n'
		'\n'
		'  assert  ASSERT, and `text` is the asserted expression\n'
		'  new     debug operator new; `text` is the allocation size, or\n'
		'          empty where the size is computed at run time\n'
		'\n'
		'TRACE reaches the same reporter but passes zero for both the file\n'
		'and the line, so it carries neither and is not listed here.  Its\n'
		'format strings are the Class::Method labels, which\n'
		'tools/analysis/extract_labels.py reads.\n'
		'\n'
		'The reporter and operator new are located by their own call\n'
		'sites, and call targets are followed through the incremental\n'
		'linker jump table, so this runs on any of the debug builds.')

	out = tsv_header(os.path.basename(path), note)
	out.append('# addr\tkind\tfile\tline\ttext')
	for addr, kind, fname, line, text in rows:
		out.append('0x%08x\t%s\t%s\t%d\t%s' % (addr, kind, fname, line, text))
	sys.stdout.write('\n'.join(out) + '\n')

	kinds = Counter(r[1] for r in rows)
	files = sorted(set(r[2] for r in rows))
	sys.stderr.write('%s: reporter 0x%08x, operator new 0x%08x\n'
			 % (os.path.basename(path), pe.va(reporter), pe.va(opnew)))
	sys.stderr.write('%s: %d sites %s, %d files, %d TRACE (carries neither), '
			 '%d unparsed\n'
			 % (os.path.basename(path), len(rows), dict(kinds),
			    len(files), traces, unparsed))
	return 0


if __name__ == '__main__':
	sys.exit(main())
