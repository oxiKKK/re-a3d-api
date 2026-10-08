
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Extract sdkdocs.\npython tools/analysis/extract_sdkdocs.py <in.doc> <out.txt>')
    raise SystemExit(0)
#
# extract_sdkdocs.py - the A3D 3.0 SDK reference documents as plain text.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# The SDK documents specify argument meanings, defaults, ranges and error
# codes. They provide (SDK) evidence under AGENTS.md; check those contracts
# against observed binary behavior.
#
# Read the Word 97 OLE2 WordDocument and table streams. FIB references CLX,
# whose piece table supplies UTF-16 or 8-bit encoding per text fragment.
# Extract text only; formatting, tables and revision marks are unsupported.
# Field codes and binary fragments may remain in the searchable output.
#
# Uses only the Python standard library.
#
# Usage:
#	python tools/analysis/extract_sdkdocs.py <in.doc> <out.txt>
#

import sys
import os
import re
import struct

ENDOFCHAIN = 0xfffffffe
FREESECT = 0xffffffff


class Ole2(object):
	"""Enough of the compound document to read one named stream."""

	def __init__(self, data):
		self.d = data
		if data[:8] != b'\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1':
			raise ValueError('not an OLE2 compound document')

		self.ssz = 1 << struct.unpack_from('<H', data, 0x1e)[0]	# sector size
		self.mini_ssz = 1 << struct.unpack_from('<H', data, 0x20)[0]
		ndifat = struct.unpack_from('<I', data, 0x48)[0]
		difat_first = struct.unpack_from('<I', data, 0x44)[0]
		dir_first = struct.unpack_from('<I', data, 0x30)[0]
		self.mini_cutoff = struct.unpack_from('<I', data, 0x38)[0]
		mini_first = struct.unpack_from('<I', data, 0x3c)[0]

		# DIFAT: the first 109 entries are in the header, the rest chained.
		difat = list(struct.unpack_from('<109I', data, 0x4c))
		sect = difat_first
		for _ in range(ndifat):
			if sect in (ENDOFCHAIN, FREESECT):
				break
			blk = self.sector(sect)
			difat += list(struct.unpack_from('<%dI' % (self.ssz // 4 - 1), blk, 0))
			sect = struct.unpack_from('<I', blk, self.ssz - 4)[0]

		# FAT
		self.fat = []
		for s in difat:
			if s in (ENDOFCHAIN, FREESECT):
				continue
			self.fat += list(struct.unpack_from('<%dI' % (self.ssz // 4),
							    self.sector(s), 0))

		self.dir = self.read_chain(dir_first)
		self.minifat = []
		self.mini_stream = b''

		# The mini stream hangs off the root entry, entry 0.
		if self.dir:
			root_start = struct.unpack_from('<I', self.dir, 0x74)[0]
			self.mini_stream = self.read_chain(root_start)
			mf = struct.unpack_from('<I', data, 0x3c)[0]
			if mf not in (ENDOFCHAIN, FREESECT):
				raw = self.read_chain(mini_first)
				self.minifat = list(struct.unpack_from(
					'<%dI' % (len(raw) // 4), raw, 0))

	def sector(self, n):
		off = 512 + n * self.ssz
		return self.d[off:off + self.ssz]

	def read_chain(self, start, limit=None):
		out, s, guard = [], start, 0
		while s not in (ENDOFCHAIN, FREESECT) and guard < 1 << 22:
			out.append(self.sector(s))
			if s >= len(self.fat):
				break
			s = self.fat[s]
			guard += 1
		data = b''.join(out)
		return data if limit is None else data[:limit]

	def read_mini_chain(self, start, size):
		out, s, guard = [], start, 0
		while s not in (ENDOFCHAIN, FREESECT) and guard < 1 << 22:
			off = s * self.mini_ssz
			out.append(self.mini_stream[off:off + self.mini_ssz])
			if s >= len(self.minifat):
				break
			s = self.minifat[s]
			guard += 1
		return b''.join(out)[:size]

	def stream(self, name):
		"""The named stream's bytes, or None."""
		want = name.encode('utf-16-le')
		for i in range(0, len(self.dir), 128):
			ent = self.dir[i:i + 128]
			if len(ent) < 128:
				break
			nlen = struct.unpack_from('<H', ent, 0x40)[0]
			if nlen < 2:
				continue
			nm = ent[:nlen - 2]
			if nm != want:
				continue
			start = struct.unpack_from('<I', ent, 0x74)[0]
			size = struct.unpack_from('<I', ent, 0x78)[0]
			if size < self.mini_cutoff:
				return self.read_mini_chain(start, size)
			return self.read_chain(start, size)
		return None


def word_text(ole, doc):
	"""The document text, through the Word 97 piece table.

	Word 97 does not store the text as one run.  The FIB points at a CLX in
	one of the two table streams; inside it a piece table lists the pieces
	the text is cut into, and each piece says for itself whether it is
	UTF-16 or 8-bit.  Bit 30 of a piece's `fc` marks the 8-bit case, and
	the real offset is then the rest of the field halved.

	There is no document-wide encoding flag.  Reading one run and guessing
	an encoding produces CJK from an English document, which is what the
	first version of this did.
	"""
	flags = struct.unpack_from('<H', doc, 10)[0]
	table = ole.stream('1Table' if flags & 0x0200 else '0Table')
	if table is None:
		return doc.decode('cp1252', 'replace')

	fc_clx = struct.unpack_from('<I', doc, 0x01a2)[0]
	lcb_clx = struct.unpack_from('<I', doc, 0x01a6)[0]
	clx = table[fc_clx:fc_clx + lcb_clx]

	# Walk the CLX to the piece table: 0x01 entries are formatting runs to
	# skip, 0x02 introduces the piece table itself.
	i = 0
	pcdt = None
	while i < len(clx):
		if clx[i] == 0x01:
			cb = struct.unpack_from('<H', clx, i + 1)[0]
			i += 3 + cb
		elif clx[i] == 0x02:
			lcb = struct.unpack_from('<I', clx, i + 1)[0]
			pcdt = clx[i + 5:i + 5 + lcb]
			break
		else:
			break
	if not pcdt:
		return doc.decode('cp1252', 'replace')

	npieces = (len(pcdt) - 4) // 12
	cps = struct.unpack_from('<%dI' % (npieces + 1), pcdt, 0)

	out = []
	for n in range(npieces):
		off = 4 * (npieces + 1) + n * 8
		fc = struct.unpack_from('<I', pcdt, off + 2)[0]
		nchars = cps[n + 1] - cps[n]
		if fc & 0x40000000:			# 8-bit piece
			start = (fc & 0x3fffffff) // 2
			out.append(doc[start:start + nchars].decode('cp1252', 'replace'))
		else:					# UTF-16 piece
			out.append(doc[fc:fc + nchars * 2].decode('utf-16-le', 'replace'))
	return ''.join(out)


def clean(text):
	"""Word's control characters into something readable."""
	text = text.replace('\r', '\n').replace('\x07', '\n')
	text = text.replace('\x0b', '\n').replace('\x0c', '\n\n')
	text = text.replace('\x13', '').replace('\x14', '').replace('\x15', '')
	text = text.replace('\x01', '').replace('\x02', '').replace('\x08', '')
	text = text.replace('\xa0', ' ').replace('’', "'")
	text = text.replace('“', '"').replace('”', '"')
	text = text.replace('–', '-').replace('—', '-')
	text = ''.join(c for c in text if c == '\n' or c == '\t' or ' ' <= c <= '\U0010ffff')
	text = re.sub(r'[ \t]+\n', '\n', text)
	text = re.sub(r'\n{3,}', '\n\n', text)

	# The first piece carries a run of the document's own binary before the
	# prose starts.  Drop leading lines until one reads as a sentence, which
	# here means mostly letters and spaces.
	lines = text.split('\n')
	start = 0
	for n, line in enumerate(lines[:60]):
		s = line.strip()
		if len(s) < 12:
			continue
		letters = sum(1 for c in s if c.isalpha() or c in ' ,.:;()-')
		if letters / len(s) > 0.9:
			start = n
			break
	return '\n'.join(lines[start:]).strip() + '\n'


def main():
	if len(sys.argv) != 3:
		sys.stderr.write('usage: extract_sdkdocs.py <in.doc> <out.txt>\n')
		return 2

	src, dst = sys.argv[1], sys.argv[2]
	ole = Ole2(open(src, 'rb').read())
	doc = ole.stream('WordDocument')
	if doc is None:
		sys.stderr.write('%s: no WordDocument stream\n' % src)
		return 1

	text = clean(word_text(ole, doc))

	header = (
		'Extracted from %s by tools/analysis/extract_sdkdocs.py.\n'
		'Do not edit by hand: regenerate it.\n'
		'\n'
		'A3D 3.0 SDK documentation, February 2000, Aureal Inc.  This is\n'
		'(SDK) evidence under CLAUDE.md: what Aureal said a\n'
		'call does, as against what the binary shows it doing.  Where the\n'
		'two disagree the binary wins and the disagreement is worth\n'
		'recording, because the 2.0 SDK shipped a header describing a later\n'
		'build than its own subject.\n'
		'\n'
		'The conversion keeps text and drops formatting, tables and\n'
		'drawing objects, so layout is lost and some field noise survives.\n'
		'%s\n\n' % (os.path.basename(src), '-' * 70))

	open(dst, 'w', encoding='utf-8', newline='\n').write(header + text)

	words = len(text.split())
	sys.stderr.write('%s -> %s: %d characters, %d words\n'
			 % (os.path.basename(src), os.path.basename(dst),
			    len(text), words))
	return 0


if __name__ == '__main__':
	sys.exit(main())
