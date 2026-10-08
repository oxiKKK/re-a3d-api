#
# pe_image.py - the PE reader the evidence extractors share.
#
# NOT PART OF THE ORIGINAL.  Project tooling.  Nothing here corresponds to an
# address in any Aureal binary.
#
# Enough of PE32 to turn a file offset into an RVA and back, read a C string at
# an RVA, and walk the export and import tables.  The extractors in this
# directory read the reference binaries in ref/ and write the tables in
# docs/llm/groundtruth/.
#

import struct


class PE(object):
	"""One 32-bit image, mapped from its file bytes."""

	def __init__(self, path):
		self.path = path
		self.data = open(path, 'rb').read()
		d = self.data

		if d[:2] != b'MZ':
			raise ValueError('%s: not an image' % path)

		self.pe = struct.unpack_from('<I', d, 0x3c)[0]
		if d[self.pe:self.pe + 4] != b'PE\0\0':
			raise ValueError('%s: no PE header' % path)

		nsec = struct.unpack_from('<H', d, self.pe + 6)[0]
		self.timestamp = struct.unpack_from('<I', d, self.pe + 8)[0]
		optsz = struct.unpack_from('<H', d, self.pe + 20)[0]

		opt = self.pe + 24
		magic = struct.unpack_from('<H', d, opt)[0]
		if magic != 0x10b:
			raise ValueError('%s: not PE32' % path)

		self.imagebase = struct.unpack_from('<I', d, opt + 28)[0]
		self.entrypoint = struct.unpack_from('<I', d, opt + 16)[0]
		ddoff = opt + 96
		self.ddir = [struct.unpack_from('<II', d, ddoff + i * 8)
			     for i in range(struct.unpack_from('<I', d, opt + 92)[0])]

		self.sections = []
		for i in range(nsec):
			o = self.pe + 24 + optsz + i * 40
			name = d[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
			vsize, va, rsize, roff = struct.unpack_from('<IIII', d, o + 8)
			chars = struct.unpack_from('<I', d, o + 36)[0]
			self.sections.append({
				'name': name, 'rva': va, 'vsize': vsize,
				'off': roff, 'rsize': rsize, 'chars': chars,
			})

	# -- address conversion ------------------------------------------------

	def rva_to_off(self, rva):
		for s in self.sections:
			if s['rva'] <= rva < s['rva'] + max(s['vsize'], s['rsize']):
				o = s['off'] + (rva - s['rva'])
				return o if o < len(self.data) else None
		return None

	def off_to_rva(self, off):
		for s in self.sections:
			if s['off'] <= off < s['off'] + s['rsize']:
				return s['rva'] + (off - s['off'])
		return None

	def va(self, rva):
		return self.imagebase + rva

	def section_of_rva(self, rva):
		for s in self.sections:
			if s['rva'] <= rva < s['rva'] + max(s['vsize'], s['rsize']):
				return s
		return None

	# -- reading -----------------------------------------------------------

	def u8(self, off):
		return self.data[off]

	def u16(self, off):
		return struct.unpack_from('<H', self.data, off)[0]

	def u32(self, off):
		return struct.unpack_from('<I', self.data, off)[0]

	def i32(self, off):
		return struct.unpack_from('<i', self.data, off)[0]

	def cstr_at_rva(self, rva, limit=400):
		"""The NUL-terminated ASCII string at an RVA, or None."""
		o = self.rva_to_off(rva)
		if o is None:
			return None
		e = self.data.find(b'\0', o, o + limit)
		if e < 0:
			return None
		try:
			return self.data[o:e].decode('ascii')
		except UnicodeDecodeError:
			return None

	def u16_at_rva(self, rva):
		o = self.rva_to_off(rva)
		return None if o is None else self.u16(o)

	# -- tables ------------------------------------------------------------

	def exports(self):
		"""[(ordinal, name, rva)], sorted by ordinal."""
		if not self.ddir or not self.ddir[0][0]:
			return []
		eo = self.rva_to_off(self.ddir[0][0])
		base = self.u32(eo + 16)
		nnames = self.u32(eo + 24)
		afun, anam, aord = (self.u32(eo + 28), self.u32(eo + 32),
				    self.u32(eo + 36))
		out = []
		for i in range(nnames):
			nrva = self.u32(self.rva_to_off(anam) + i * 4)
			name = self.cstr_at_rva(nrva)
			idx = self.u16(self.rva_to_off(aord) + i * 2)
			rva = self.u32(self.rva_to_off(afun) + idx * 4)
			out.append((base + idx, name, rva))
		return sorted(out)

	def imports(self):
		"""{dll: [name-or-#ordinal]}."""
		if len(self.ddir) < 2 or not self.ddir[1][0]:
			return {}
		off = self.rva_to_off(self.ddir[1][0])
		out = {}
		while True:
			ent = self.data[off:off + 20]
			if len(ent) < 20 or ent == b'\0' * 20:
				break
			oft, _, _, nrva, first = struct.unpack('<IIIII', ent)
			dll = self.cstr_at_rva(nrva)
			names = []
			t = self.rva_to_off(oft or first)
			while t is not None:
				v = self.u32(t)
				if not v:
					break
				if v & 0x80000000:
					names.append('#%d' % (v & 0xffff))
				else:
					names.append(self.cstr_at_rva(v + 2))
				t += 4
			out[dll] = names
			off += 20
		return out

	def iat(self):
		"""{VA of an IAT slot: 'DLL!name'}, from the import descriptors.

		The names come from the original first thunk array where one
		exists and from the IAT itself otherwise, but the keys are
		always the IAT slots, because that is what a compiled
		`call dword ptr [addr]` reads."""
		if len(self.ddir) < 2 or not self.ddir[1][0]:
			return {}
		off = self.rva_to_off(self.ddir[1][0])
		out = {}
		while True:
			ent = self.data[off:off + 20]
			if len(ent) < 20 or ent == b'\0' * 20:
				break
			oft, _, _, nrva, first = struct.unpack('<IIIII', ent)
			dll = self.cstr_at_rva(nrva)
			t = self.rva_to_off(oft or first)
			slot = first
			while t is not None:
				v = self.u32(t)
				if not v:
					break
				if v & 0x80000000:
					name = '#%d' % (v & 0xffff)
				else:
					name = self.cstr_at_rva(v + 2)
				out[self.imagebase + slot] = '%s!%s' % (dll, name)
				t += 4
				slot += 4
			off += 20
		return out

	def relocations(self):
		"""Sorted RVAs of every HIGHLOW base relocation site."""
		if len(self.ddir) < 6 or not self.ddir[5][0]:
			return []
		off = self.rva_to_off(self.ddir[5][0])
		end = off + self.ddir[5][1]
		out = []
		while off < end:
			page, blk = struct.unpack_from('<II', self.data, off)
			if blk < 8:
				break
			for i in range((blk - 8) // 2):
				e = struct.unpack_from('<H', self.data, off + 8 + i * 2)[0]
				if e >> 12 == 3:
					out.append(page + (e & 0xfff))
			off += blk
		return sorted(out)

	def version_info(self):
		"""The fixed block and the StringFileInfo values."""
		d = self.data
		f = d.find(b'\xbd\x04\xef\xfe')
		if f < 0:
			return {}
		fv = struct.unpack_from('<HHHH', d, f + 8)
		flags = struct.unpack_from('<IIIII', d, f + 24)
		out = {
			'FILEVERSION': '%d,%d,%d,%d' % (fv[1], fv[0], fv[3], fv[2]),
			'FILEFLAGS': flags[1],
		}
		for key in ('CompanyName', 'FileDescription', 'FileVersion',
			    'InternalName', 'LegalCopyright', 'LegalTrademarks',
			    'OriginalFilename', 'ProductName', 'ProductVersion'):
			k = key.encode('utf-16-le')
			i = d.find(k)
			if i < 0:
				continue
			v = d[i + len(k):i + len(k) + 200].lstrip(b'\0')
			e = v.find(b'\0\0')
			if e >= 0:
				v = v[:e + 1]
			out[key] = v.decode('utf-16-le', 'replace').strip()
		return out


def tsv_header(subject, note):
	"""The comment block every generated table starts with."""
	return [
		'# Generated from %s by tools/analysis/, which reads the binary.' % subject,
		'# Do not edit by hand: regenerate it.',
		'#',
	] + ['# ' + line for line in note.strip().split('\n')] + ['#']
