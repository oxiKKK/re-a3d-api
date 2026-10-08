
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Extract symbols.\npython tools/analysis/extract_symbols.py ref/a3dapi_33_dbg.dll docs/llm/groundtruth/regions-dbg.tsv > out.tsv')
    raise SystemExit(0)
#
# extract_symbols.py - the object names and decorated C++ symbols MSVC 6 left
# past the end of the code in an incrementally-linked debug build.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Scan .text for NUL-terminated object paths and decorated symbols from
# the /ZI /INCREMENTAL contribution table. DebugViewer yields no rows;
# see docs/llm/DEBUG-GENERATIONS.md.
#
# obj rows preserve both clean and byte-prefixed Aureal paths; the prefix
# meaning is unknown. The 38 CRT objects have one copy each. sym rows
# exclude ??_C@ string literals and invalid matches. The Debug image has
# 150 contiguous symbols plus splash_screen@splash.
#
# The demangler supports methods, constructors/destructors, vtables (??_7),
# operators (??2/??3), adjustor thunks (W3AG) and argument backreferences.
# Lock and GetOrientation were checked against DirectSound's seven-argument
# and two-D3DVECTOR-pointer signatures. Template backreferences remain
# unsupported, including D2DBuffer::Init's CList parameter; retain the raw
# symbol and leave decoded empty for unsupported forms.
#
# Object attribution uses filenames; symbol attribution uses region spans.
# Most entries lie beyond the mapped code ending below 0x100c0000,
# so an empty region column is expected.
#
# Usage:
#	python tools/analysis/extract_symbols.py ref/a3dapi_33_dbg.dll \
#	       docs/llm/groundtruth/regions-dbg.tsv > out.tsv
#

import sys
import os
import re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_image import PE, tsv_header

OBJ_RE = re.compile(rb'[ -~]{3,}\.obj\x00')
SYM_RE = re.compile(rb'\?[ -~]{2,300}\x00')


# ---------------------------------------------------------------------------
# the demangler
# ---------------------------------------------------------------------------

class Unsupported(Exception):
	"""A form this parser does not cover.  Not an error: the caller leaves
	the symbol's decoded column empty."""


PRIMITIVES = {
	'X': 'void', 'D': 'char', 'C': 'signed char', 'E': 'unsigned char',
	'F': 'short', 'G': 'unsigned short', 'H': 'int', 'I': 'unsigned int',
	'J': 'long', 'K': 'unsigned long', 'M': 'float', 'N': 'double',
	'O': 'long double',
}

EXT_PRIMITIVES = {
	'_N': 'bool', '_W': 'wchar_t', '_J': '__int64', '_K': 'unsigned __int64',
}

TAGS = {'T': 'union', 'U': 'struct', 'V': 'class'}

# first letter of the three-letter (or, for a thunk, four-part) access
# group on a member function: access level, and whether it is an instance,
# static or virtual member, or an adjustor thunk for one of the latter.
ACCESS = {
	'A': ('private', 'instance'), 'B': ('private', 'instance'),
	'C': ('private', 'static'), 'D': ('private', 'static'),
	'E': ('private', 'virtual'), 'F': ('private', 'virtual'),
	'G': ('private', 'virtual-thunk'), 'H': ('private', 'virtual-thunk'),
	'I': ('protected', 'instance'), 'J': ('protected', 'instance'),
	'K': ('protected', 'static'), 'L': ('protected', 'static'),
	'M': ('protected', 'virtual'), 'N': ('protected', 'virtual'),
	'O': ('protected', 'virtual-thunk'), 'P': ('protected', 'virtual-thunk'),
	'Q': ('public', 'instance'), 'R': ('public', 'instance'),
	'S': ('public', 'static'), 'T': ('public', 'static'),
	'U': ('public', 'virtual'), 'V': ('public', 'virtual'),
	'W': ('public', 'virtual-thunk'), 'X': ('public', 'virtual-thunk'),
}

CALLCONV = {
	'A': '__cdecl', 'B': '__cdecl', 'C': '__pascal', 'D': '__pascal',
	'E': '__thiscall', 'F': '__thiscall', 'G': '__stdcall', 'H': '__stdcall',
	'I': '__fastcall', 'J': '__fastcall',
}


def read_compressed_number(s, i):
	"""A digit is its own value; a run of A-P nibbles ends in `@`.  Returned
	as the raw token text - this parser does not assert what the adjustor
	thunk offset the token encodes actually is."""
	if s[i].isdigit():
		return s[i], i + 1
	j = i
	while s[j] != '@':
		j += 1
	return s[i:j], j + 1


class TypeReader(object):
	"""Reads the argument list of one symbol.  `backrefs` is the flat,
	top-level table MSVC fills as each new distinct compound parameter type
	is seen; a later parameter that repeats one spells only its index
	(`0`-`9`).  This project checked that model against three symbols whose
	parameter list is independently known (Lock, GetOrientation, Duplicate)
	before trusting it.  A backreference used anywhere else - inside a
	template argument list, which is its own numbering context this parser
	does not model - raises Unsupported."""

	def __init__(self, s, i):
		self.s = s
		self.i = i
		self.backrefs = []

	def read_type(self, top_level):
		s = self.s
		c = s[self.i]

		if c.isdigit():
			if not top_level or int(c) >= len(self.backrefs):
				raise Unsupported('backreference out of context')
			self.i += 1
			return self.backrefs[int(c)]

		if c == '_':
			code = s[self.i:self.i + 2]
			if code not in EXT_PRIMITIVES:
				raise Unsupported('extended primitive ' + code)
			self.i += 2
			return EXT_PRIMITIVES[code]

		if c in PRIMITIVES:
			self.i += 1
			return PRIMITIVES[c]

		if c in ('P', 'A'):
			ptr = '*' if c == 'P' else '&'
			self.i += 1
			cv = s[self.i]
			if cv not in 'ABCD':
				raise Unsupported('pointer cv code ' + cv)
			self.i += 1
			cvtext = {'A': '', 'B': 'const ', 'C': 'volatile ',
				  'D': 'const volatile '}[cv]
			inner = self.read_type(top_level=False)
			body = cvtext + inner
			text = body + ptr if body[-1:] in '*&' else body + ' ' + ptr
			if top_level and len(self.backrefs) < 10:
				self.backrefs.append(text)
			return text

		if c in TAGS:
			self.i += 1
			text = self.read_name()
			if top_level and len(self.backrefs) < 10:
				self.backrefs.append(text)
			return text

		raise Unsupported('type code ' + c)

	def read_name(self):
		"""One or more `@`-terminated components, ending at the empty
		component.  Every name in this table's argument lists is a single
		component; the loop is written for the general case anyway rather
		than assuming that stays true."""
		if self.s[self.i] == '?':
			raise Unsupported('template name')
		parts = []
		while True:
			j = self.s.index('@', self.i)
			part = self.s[self.i:j]
			self.i = j + 1
			if part == '':
				break
			parts.append(part)
		return '::'.join(reversed(parts))

	def read_args(self):
		if self.s[self.i:self.i + 2] == 'XZ':
			self.i += 2
			return []
		args = []
		while self.s[self.i] != '@':
			args.append(self.read_type(top_level=True))
		self.i += 1
		if self.s[self.i] != 'Z':
			raise Unsupported('argument list not Z-terminated')
		self.i += 1
		return args


def read_qualifiers(s, i):
	"""The `Name@Class@...@@` chain ahead of the access group.  Returns
	(innermost_name, outer_chain_innermost_first, next_i)."""
	parts = []
	while True:
		j = s.index('@', i)
		part = s[i:j]
		i = j + 1
		if part == '':
			break
		parts.append(part)
	return parts[0], parts[1:], i


def decode_vtable(s):
	i = 4					# past '??_7'
	klass, _, i = read_qualifiers(s, i)
	if s[i:i + 2] != '6B':
		raise Unsupported('vtable code ' + s[i:i + 2])
	i += 2
	if s[i] == '@':
		return 'vftable for %s' % klass
	base, _, i = read_qualifiers(s, i)
	return 'vftable for %s, as %s' % (klass, base)


def decode_ctor_dtor(s, is_ctor):
	i = 3					# past '??0' / '??1'
	klass, _, i = read_qualifiers(s, i)
	sig, i = decode_access_group(s, i)
	access, kind, thunk, callconv = sig
	if s[i] != '@':
		raise Unsupported('ctor/dtor return type present')
	i += 1
	args = TypeReader(s, i).read_args()
	name = klass if is_ctor else '~' + klass
	return format_member(access, kind, thunk, callconv, None, klass, name,
			      args)


def decode_global_operator(s, is_new):
	i = 3					# past '??2' / '??3'
	if s[i] == '@':
		scope = ''
		i += 1
	else:
		name, _, i = read_qualifiers(s, i)
		scope = name + '::'
	if s[i:i + 2] != 'YA':
		raise Unsupported('operator not a free __cdecl function')
	i += 2
	tr = TypeReader(s, i)
	ret = tr.read_type(top_level=False)
	args = tr.read_args()
	name = 'operator new' if is_new else 'operator delete'
	return '%s %s%s(%s)' % (ret, scope, name, ', '.join(args) or 'void')


def decode_access_group(s, i):
	acc = s[i]
	if acc not in ACCESS:
		raise Unsupported('access code ' + acc)
	access, kind = ACCESS[acc]
	i += 1
	thunk = None
	if kind == 'virtual-thunk':
		thunk, i = read_compressed_number(s, i)
	if kind != 'static':
		i += 1				# near/far - always near ('A') here
	cc = s[i]
	if cc not in CALLCONV:
		raise Unsupported('calling convention code ' + cc)
	i += 1
	return (access, kind, thunk, CALLCONV[cc]), i


def format_member(access, kind, thunk, callconv, ret, klass, name, args):
	tag = access
	if kind == 'virtual':
		tag += ', virtual'
	elif kind == 'virtual-thunk':
		tag += ', virtual, adjustor thunk (code %s)' % thunk
	elif kind == 'static':
		tag += ', static'
	tag += ', ' + callconv
	argtext = ', '.join(args) if args else 'void'
	if ret is None:
		return '[%s] %s::%s(%s)' % (tag, klass, name, argtext)
	return '[%s] %s %s::%s(%s)' % (tag, ret, klass, name, argtext)


def decode(sym):
	"""The undecoded-by-hand C++ signature for one `?`-prefixed decorated
	symbol, or None where this parser does not cover its form."""
	try:
		if sym[1] != '?':
			method, chain, i = read_qualifiers(sym, 1)
			klass = '::'.join(reversed(chain))
			(access, kind, thunk, callconv), i = decode_access_group(sym, i)
			if sym[i] == '@':
				raise Unsupported('member with no return type')
			tr = TypeReader(sym, i)
			ret = tr.read_type(top_level=False)
			args = tr.read_args()
			return format_member(access, kind, thunk, callconv, ret,
					      klass, method, args)
		if sym[2:4] == '_7':
			return decode_vtable(sym)
		if sym[2] == '0':
			return decode_ctor_dtor(sym, is_ctor=True)
		if sym[2] == '1':
			return decode_ctor_dtor(sym, is_ctor=False)
		if sym[2] == '2':
			return decode_global_operator(sym, is_new=True)
		if sym[2] == '3':
			return decode_global_operator(sym, is_new=False)
		raise Unsupported('special form ?? ' + sym[2])
	except Unsupported:
		return None
	except (IndexError, ValueError):
		return None


# ---------------------------------------------------------------------------
# extraction
# ---------------------------------------------------------------------------

def read_regions(path):
	out = []
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		f = line.rstrip('\n').split('\t')
		out.append((int(f[0], 16), int(f[1], 16), f[4]))
	return sorted(out)


def region_by_addr(regions, va):
	for start, end, fname in regions:
		if start <= va <= end:
			return fname
	return ''


def obj_stem(text):
	"""The bare file stem, ignoring any leading junk before the last path
	separator - which is where the second copy's unexplained extra byte
	sits - and the `.obj` extension."""
	tail = re.split(r'[\\/]', text)[-1]
	return os.path.splitext(tail)[0]


def region_by_basename(regions, base):
	base = base.lower()
	for _, _, fname in regions:
		stem = os.path.splitext(fname)[0].lower()
		if stem == base:
			return fname
	return ''


def text_section_span(pe):
	sec = pe.sections[0]
	if sec['name'] != '.text':
		for s in pe.sections:
			if s['name'] == '.text':
				sec = s
				break
	return sec['off'], sec['off'] + sec['rsize']


def main():
	if len(sys.argv) not in (2, 3):
		sys.stderr.write('usage: extract_symbols.py <image> [regions.tsv]\n')
		return 2

	pe = PE(sys.argv[1])
	regions = read_regions(sys.argv[2]) if len(sys.argv) == 3 else []

	lo, hi = text_section_span(pe)
	window = pe.data[lo:hi]

	rows = []

	for m in OBJ_RE.finditer(window):
		off = lo + m.start()
		rva = pe.off_to_rva(off)
		text = m.group()[:-1].decode('ascii')
		rows.append((pe.va(rva), 'obj', text, '',
			     region_by_basename(regions, obj_stem(text))))

	seen_syms = set()
	for m in SYM_RE.finditer(window):
		off = lo + m.start()
		rva = pe.off_to_rva(off)
		text = m.group()[:-1].decode('ascii')
		if '@' not in text or text.startswith('??_C@'):
			continue			# string-literal name, or garbage
		va = pe.va(rva)
		rows.append((va, 'sym', text, decode(text) or '',
			     region_by_addr(regions, va)))
		seen_syms.add(text)

	rows.sort()

	note = ('Every `.obj` name and decorated C++ symbol MSVC 6 left in the\n'
		'edit-and-continue table past the end of the code, in the one\n'
		'image that has one.  `addr` is where the string itself sits.\n'
		'\n'
		'  obj  a compiland name.  Every Aureal object appears twice, the\n'
		'       second copy carrying one unexplained leading byte; a CRT\n'
		'       object appears once.  Both copies are rows.\n'
		'  sym  a decorated symbol.  `decoded` is this project\'s own\n'
		'       undecoded-by-hand C++ signature, derived in\n'
		'       tools/analysis/extract_symbols.py, and is empty where that\n'
		'       parser does not cover the form - a correct result, not a\n'
		'       gap in the data.\n'
		'\n'
		'`region` is docs/llm/groundtruth/regions-dbg.tsv, matched by base\n'
		'name for an obj row and by address for a sym row.  Region\n'
		'building stopped below 0x100c0000, so most rows here, which sit\n'
		'above it, carry no region.')

	out = tsv_header(os.path.basename(sys.argv[1]), note)
	out.append('# addr\tkind\tsymbol\tdecoded\tregion')
	for va, kind, text, decoded, region in rows:
		out.append('0x%08x\t%s\t%s\t%s\t%s' % (va, kind, text, decoded, region))
	sys.stdout.write('\n'.join(out) + '\n')

	nobj = sum(1 for r in rows if r[1] == 'obj')
	nsym = sum(1 for r in rows if r[1] == 'sym')
	ndecoded = sum(1 for r in rows if r[1] == 'sym' and r[3])
	uobj = len(set(obj_stem(r[2]) for r in rows if r[1] == 'obj'))
	usym = len(seen_syms)
	sys.stderr.write('%s: %d obj rows (%d distinct names), %d sym rows '
			 '(%d distinct, %d decoded)\n'
			 % (os.path.basename(sys.argv[1]), nobj, uobj, nsym,
			    usym, ndecoded))
	return 0


if __name__ == '__main__':
	sys.exit(main())
