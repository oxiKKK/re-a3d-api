#!/usr/bin/env python3
"""Find every `catch` in Aureal's 3.3.677 Debug build, and what it catches.

NOT PART OF THE ORIGINAL.  Project tooling.

Hex-Rays does not render a catch block in this image: a catch compiles to a
funclet outside the function body, and the decompiler shows the function as if
it had none.  A body reconstructed from the pseudocode alone therefore loses
the catch, and a throw the original swallowed escapes the DLL instead.

The catches are in the compiler's exception tables rather than in the code, so
they are read from there.  MSVC 6 gives a function with EH a prologue of

    push ebp / mov ebp, esp / push -1 / push offset __ehhandler

and the `__ehhandler` thunk is `mov eax, offset FuncInfo; jmp
__CxxFrameHandler`.  `FuncInfo` carries the try-block count and a map of try
blocks, each with its handler array; a handler names the caught type through a
`TypeDescriptor` and the funclet that runs it.

Run it from the repository root:

    python tools/analysis/extract_exception_handlers.py
"""

import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Extract exception handlers.\npython tools/analysis/extract_exception_handlers.py')
    raise SystemExit(0)

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pe_image import PE

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GT = os.path.join(ROOT, "docs", "llm", "groundtruth")
IMAGE = os.path.join(ROOT, "ref", "a3dapi_33_dbg.dll")

EH_MAGIC = (0x19930520, 0x19930521, 0x19930522)

# push ebp / mov ebp, esp / push -1 / push imm32
PROLOGUE = b"\x55\x8b\xec\x6a\xff\x68"


def is_crt(name):
    """tools/analysis/infer_source_file_regions.py's test."""
    return name.endswith(".c") or name == "dbgdel.cpp"


class Image(object):
    def __init__(self, path):
        self.pe = PE(path)

    def read(self, va, n):
        off = self.pe.rva_to_off(va - self.pe.imagebase)
        return self.pe.data[off : off + n] if off is not None else None

    def dword(self, va):
        b = self.read(va, 4)
        return struct.unpack("<I", b)[0] if b else None

    def sdword(self, va):
        b = self.read(va, 4)
        return struct.unpack("<i", b)[0] if b else None

    def cstring(self, va):
        off = self.pe.rva_to_off(va - self.pe.imagebase)
        if off is None:
            return ""
        end = self.pe.data.index(b"\0", off)
        return self.pe.data[off:end].decode("ascii", "replace")


def functions():
    rows = []
    with open(os.path.join(GT, "functions-dbg.tsv"), encoding="utf-8") as fp:
        for line in fp:
            if line.startswith("#") or not line.strip():
                continue
            f = line.rstrip("\n").split("\t")
            rows.append((int(f[0], 16), int(f[1], 16), f[7] if len(f) > 7 else "-"))
    return rows


def func_info(img, addr):
    """The FuncInfo a function's EH prologue names, or None."""
    head = img.read(addr, 10)
    if not head or head[:6] != PROLOGUE:
        return None
    handler = struct.unpack_from("<I", head, 6)[0]
    thunk = img.read(handler, 10)
    if not thunk or thunk[0] != 0xB8:  # mov eax, imm32
        return None
    fi = struct.unpack_from("<I", thunk, 1)[0]
    if img.dword(fi) not in EH_MAGIC:
        return None
    return fi


def catches(img, fi):
    """Every catch in one FuncInfo, as (type, catch-object offset, funclet)."""
    out = []
    for lo, hi, cs in tries(img, fi):
        out.extend(cs)
    return out


def tries(img, fi):
    """Every try block, as (tryLow, tryHigh, [(type, disp, funclet), ...]).

    The two state numbers are what settles which span of a body a try covers:
    the body writes the state into its frame at `[ebp-4]` as it enters and
    leaves each one, so the addresses of those stores are the try's extent.
    """
    out = []
    count = img.dword(fi + 12)
    table = img.dword(fi + 16)
    for i in range(count or 0):
        entry = table + i * 20
        ncatch = img.dword(entry + 12)
        array = img.dword(entry + 16)
        cs = []
        for j in range(ncatch or 0):
            h = array + j * 16
            ptype = img.dword(h + 4)
            cs.append(
                (
                    img.cstring(ptype + 8) if ptype else "...",
                    img.sdword(h + 8),
                    img.dword(h + 12),
                )
            )
        out.append((img.sdword(entry), img.sdword(entry + 4), cs))
    return out


# `mov dword ptr [ebp-4], imm32` and `mov byte ptr [ebp-4], imm8`, the two
# forms MSVC 6 writes the EH state with.
STATE_D = b"\xc7\x45\xfc"
STATE_B = b"\xc6\x45\xfc"


def state_stores(img, addr, size):
    """(address, state) for every EH state store in a run of code."""
    out = []
    body = img.read(addr, size)
    if not body:
        return out
    for i in range(len(body) - 6):
        if body[i : i + 3] == STATE_D:
            out.append((addr + i, struct.unpack_from("<i", body, i + 3)[0]))
        elif body[i : i + 3] == STATE_B:
            b = body[i + 3]
            out.append((addr + i, b if b < 128 else b - 256))
    return out


def main():
    img = Image(IMAGE)

    found = []
    for addr, size, fname in functions():
        if fname == "-" or is_crt(fname):
            continue
        fi = func_info(img, addr)
        if not fi:
            continue
        cs = catches(img, fi)
        if cs:
            found.append((fname, addr, size, fi, cs))

    found.sort()
    print("%d functions in Aureal regions carry a catch." % len(found))
    print()
    for fname, addr, size, fi, cs in found:
        for typ, disp, funclet in cs:
            print(
                "%-20s dbg:0x%08x  size 0x%-5x FuncInfo dbg:0x%08x  "
                "catch(%s) at ebp%+d, funclet dbg:0x%08x"
                % (fname, addr, size, fi, typ, disp, funclet)
            )

    print()
    print("`.PAD` is `char const *`, which is what every throw in this image")
    print("throws.  A funclet address inside the function's own extent is the")
    print("catch body, which Hex-Rays does not show.")

    # Record try ranges as well as handlers. Function extents can stop at the
    # first catch funclet; scan subsequent contiguous entries for the rest.
    nested = [f for f in found if len(tries(img, f[3])) > 1]
    if nested:
        byaddr = dict((a, s) for a, s, _ in functions())
        print()
        print(
            "%d function%s carries more than one try block:"
            % (len(nested), "" if len(nested) == 1 else "s")
        )
        for fname, addr, size, fi, cs in nested:
            end = addr + size
            while end in byaddr:
                end += byaddr[end]
            print()
            print("%s  dbg:0x%08x" % (fname, addr))
            for lo, hi, tcs in tries(img, fi):
                for typ, disp, funclet in tcs:
                    print(
                        "    states %d..%-2d  catch(%s) at ebp%+d, "
                        "funclet dbg:0x%08x" % (lo, hi, typ, disp, funclet)
                    )
            for a, st in state_stores(img, addr, end - addr):
                print("    dbg:0x%08x  state = %d" % (a, st))


if __name__ == "__main__":
    sys.exit(main())
