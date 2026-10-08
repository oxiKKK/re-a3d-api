#!/usr/bin/env python3
#
# hw_a3d_cl_entities.py
#
# NOT PART OF THE ORIGINAL.  Project tooling.  It patches Valve's engine, not
# anything Aureal wrote.
#
# Patch three unchecked cl_entities reads that fault on A3D menu sounds.
# Preserve the original file as hw_original.dll.
#
# Drop it in the Half-Life directory and run it:
#
#	python hw_a3d_cl_entities.py
#	python hw_a3d_cl_entities.py --dry-run
#	python hw_a3d_cl_entities.py --restore
#	python hw_a3d_cl_entities.py path\to\hw.dll
#
# It needs nothing outside the standard library and nothing from this
# repository.
#
# Keep the call and argument pushes: the third site reuses their stack slots.
# Accept other builds only when all signatures have the expected match counts.

import os
import shutil
import struct
import sys

BACKUP_NAME = "hw_original.dll"

def parse(text):
	"""Turn a space-separated hex string with '??' wildcards into
	(bytes, mask).  The mask byte is 0xFF where the pattern byte must
	match and 0x00 where anything is accepted."""

	pattern = bytearray()
	mask = bytearray()

	for token in text.split():
		if token == "??":
			pattern.append(0)
			mask.append(0)
		else:
			pattern.append(int(token, 16))
			mask.append(0xFF)

	return bytes(pattern), bytes(mask)


# Half-Life 4554 hw.dll: S_StartDynamicSound 0x1DA2D4E; S_StartStaticSound
# 0x1DA30B8 (imagebase 0x1D00000). ECX holds entnum*3 on entry.
SIG_A = parse(
	"8B 1D ?? ?? ?? ??  33 D2  8D 0C 89  8D 0C 89  8D 0C 89"
	"  39 94 CB 08 03 00 00  0F 94 C2")

# Same-length replacement: return FALSE in DL when cl_entities is null.
FIX_A = parse(
	"33 D2  8B 1D ?? ?? ?? ??  85 DB  74 0D  6B C9 7D"
	"  39 94 CB 08 03 00 00  0F 94 C2  90 90")

# Offsets of the wildcarded cl_entities address in the signature and replacement.

ADDR_A_FROM = 2
ADDR_A_TO = 4

# Half-Life 4554 hw.dll: sub_1DA5980 at 0x1DA5AEB (imagebase 0x1D00000).
# ECX holds entnum on entry.
SIG_B = parse(
	"8B 2D ?? ?? ?? ??  8D 0C 49  8D 0C 89  8D 0C 89  8D 14 89  33 C9"
	"  39 8C D5 08 03 00 00  0F 94 C1")

# Same-length replacement: return FALSE in CL when cl_entities is null.
FIX_B = parse(
	"69 D1 77 01 00 00  33 C9  8B 2D ?? ?? ?? ??  85 ED  74 0A"
	"  39 8C D5 08 03 00 00  0F 94 C1  90 90")

ADDR_B_FROM = 2
ADDR_B_TO = 10

# Name, signature, replacement, address offsets, required match count.

SITES = (
	("S_StartDynamicSound / S_StartStaticSound",
	 SIG_A, FIX_A, ADDR_A_FROM, ADDR_A_TO, 2),
	("sub_1DA5980",
	 SIG_B, FIX_B, ADDR_B_FROM, ADDR_B_TO, 1),
)


def find_all(data, pattern, mask):
	"""Every offset in data where pattern matches under mask."""

	hits = []
	n = len(pattern)

	anchor = 0

	while anchor < n and mask[anchor] == 0:
		anchor += 1

	if anchor == n:
		return hits

	needle = pattern[anchor]
	start = 0

	while True:
		i = data.find(needle, start)

		if i < 0 or i < anchor:
			if i < 0:
				break

			start = i + 1

			continue

		base = i - anchor
		start = i + 1

		if base + n > len(data):
			break

		for j in range(n):
			if mask[j] and data[base + j] != pattern[j]:
				break
		else:
			hits.append(base)

	return hits


def section_map(data):
	"""(imagebase, [(file_offset, raw_size, rva)]) from the PE headers, so a
	file offset can be reported as the address the disassembler shows.
	Returns None if the file is not a PE32 image."""

	try:
		if data[:2] != b"MZ":
			return None

		lfanew = struct.unpack_from("<I", data, 0x3C)[0]

		if data[lfanew:lfanew + 4] != b"PE\0\0":
			return None

		coff = lfanew + 4
		sections, opt_size = struct.unpack_from("<H", data, coff + 2)[0], \
				     struct.unpack_from("<H", data, coff + 16)[0]

		opt = coff + 20

		if struct.unpack_from("<H", data, opt)[0] != 0x10B:
			return None

		imagebase = struct.unpack_from("<I", data, opt + 28)[0]

		table = opt + opt_size
		entries = []

		for i in range(sections):
			s = table + i * 40
			rva, raw_size, raw_ptr = struct.unpack_from("<III", data,
								   s + 12)

			entries.append((raw_ptr, raw_size, rva))

		return imagebase, entries
	except (struct.error, IndexError):
		return None


def to_va(pe, offset):
	"""The virtual address a file offset maps to, or None."""

	if pe is None:
		return None

	imagebase, entries = pe

	for raw_ptr, raw_size, rva in entries:
		if raw_ptr <= offset < raw_ptr + raw_size:
			return imagebase + rva + (offset - raw_ptr)

	return None


# where(): Return the file offset and, when mapped, its virtual address as text.
def where(pe, offset):
	va = to_va(pe, offset)

	if va is None:
		return "file 0x%08X" % offset

	return "file 0x%08X  va 0x%08X" % (offset, va)


# restore(): Restore the backup; return 0 on success or 1 if it is missing.
def restore(target, backup):
	if not os.path.exists(backup):
		print("no %s beside %s, nothing to restore" %
		      (os.path.basename(backup), os.path.basename(target)))

		return 1

	shutil.copyfile(backup, target)
	print("restored %s from %s" % (os.path.basename(target),
				       os.path.basename(backup)))

	return 0


# main(): Apply, preview or restore the patch; return 0 on success or 1 on failure.
def main(argv):
	dry_run = "--dry-run" in argv
	do_restore = "--restore" in argv

	rest = [a for a in argv[1:] if not a.startswith("--")]

	if rest:
		target = os.path.abspath(rest[0])
	else:
		target = os.path.join(os.path.dirname(os.path.abspath(argv[0])),
				      "hw.dll")

	backup = os.path.join(os.path.dirname(target), BACKUP_NAME)

	if do_restore:
		return restore(target, backup)

	if not os.path.exists(target):
		print("no hw.dll at %s" % target)
		print("put this script in the Half-Life directory, or name the "
		      "file on the command line")

		return 1

	with open(target, "rb") as f:
		data = bytearray(f.read())

	pe = section_map(bytes(data))

	print("target   %s  (%d bytes)" % (target, len(data)))

	if pe is not None:
		print("imagebase 0x%08X" % pe[0])

	print("")

	done = 0

	for site in SITES:
		fix = site[2]
		done += len(find_all(data, fix[0], fix[1]))

	if done:
		print("already patched: %d site(s) carry the correction" % done)
		print("nothing to do.  --restore puts the original back.")

		return 0

	plan = []
	ok = True

	for name, sig, fix, addr_from, addr_to, expect in SITES:
		hits = find_all(data, sig[0], sig[1])

		print("%-42s %d found, %d expected" % (name, len(hits), expect))

		for offset in hits:
			print("    %s" % where(pe, offset))

		if len(hits) != expect:
			ok = False

		for offset in hits:
			plan.append((offset, sig, fix, addr_from, addr_to))

	print("")

	if not ok:
		print("refusing to write: the file does not carry the sites this")
		print("script knows.  It is built for a GoldSrc hw.dll with the")
		print("A3D sound code; Half-Life 4554 is what it was read in.")

		return 1

	for offset, sig, fix, addr_from, addr_to in plan:
		patched = bytearray(fix[0])
		patched[addr_to:addr_to + 4] = data[offset + addr_from:
						    offset + addr_from + 4]

		assert len(patched) == len(sig[0])

		if not dry_run:
			data[offset:offset + len(patched)] = patched

	if dry_run:
		print("--dry-run: %d site(s) would be patched, nothing written"
		      % len(plan))

		return 0

	if os.path.exists(backup):
		print("%s is already there, keeping it" % BACKUP_NAME)
	else:
		shutil.copyfile(target, backup)
		print("kept the original as %s" % BACKUP_NAME)

	with open(target, "wb") as f:
		f.write(data)

	print("patched %d site(s) in %s" % (len(plan),
					    os.path.basename(target)))
	print("")
	print("A3D can be enabled again.  --restore puts the original back.")

	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
