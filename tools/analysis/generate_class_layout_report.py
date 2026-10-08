#!/usr/bin/env python3
"""Build docs/llm/LAYOUT.md from MSVC's own class layout report.

NOT PART OF THE ORIGINAL.  Project tooling.

The layouts in this tree are asserted at compile time with `sizeof` and
`offsetof`, so a green build already proves them.  This writes them out so a
reader can check an offset in a source banner without compiling anything, and
so that a field moving shows up as a diff.

Every source file is compiled with `/d1reportAllClassLayout`, which makes the
compiler print the layout of every class the translation unit lays out, and
the reports are merged.  It is done per file rather than over one unit that
includes every header because two headers declare the same private interface:
`IResManBuffer` is declared in both `d2dbuffer.h` and `rmbuffer.h`, and
`src/a3d` declares its own `CA3dSource` and `CA3dClassFactory` beside
`src/a3dapi`'s.  No translation unit in the build sees either pair, so the
pairs are not a defect, and a single unit that included everything would not
compile.

The compiler is the authority.  Nothing here reads a declaration.

Run it from the repository root:

    python tools/analysis/generate_class_layout_report.py
"""

import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Generate class layout report.\npython tools/analysis/generate_class_layout_report.py')
    raise SystemExit(0)

import os
import re
import subprocess
import sys
from collections import OrderedDict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "docs", "llm", "LAYOUT.md")
WORK = os.path.join(ROOT, "build", "layout")

# The definitions src/a3dapi/CMakeLists.txt gives the DLL.  /arch:IA32 does not
# change a layout; a definition can, because a header may declare a member
# under one.
DEFINES = [
    "WIN32",
    "_WINDOWS",
    "_USRDLL",
    "STRICT",
    "WIN32_LEAN_AND_MEAN",
    "_CRT_SECURE_NO_WARNINGS",
]

VCVARS = (
    r"C:\Program Files\Microsoft Visual Studio\2022\Community"
    r"\VC\Auxiliary\Build\vcvars32.bat"
)

# Read type ownership from definitions in src/a3dapi, src/a3d and inc;
# the compiler layout report omits declaration paths. Include definitions
# and DECLARE_INTERFACE_ declarations, excluding forward declarations.
DECLARED = re.compile(
    r"^\s*(?:class|struct|union)\s+([A-Za-z_]\w*)\s*(?:[:{]|$)"
    r"|^\s*DECLARE_INTERFACE_?\(\s*([A-Za-z_]\w*)",
    re.M,
)

# `} A3DDALCAPS, *LPA3DDALCAPS;` closing a typedef'd struct.
TYPEDEF_TAIL = re.compile(r"^\s*\}\s*([A-Za-z_]\w*)\s*(?:,|;)", re.M)


def declared_names():
    """Every type name this tree's own headers declare."""
    names = set()
    roots = [
        os.path.join(ROOT, "src", "a3dapi"),
        os.path.join(ROOT, "src", "a3d"),
        os.path.join(ROOT, "inc"),
    ]
    for d in roots:
        for f in sorted(os.listdir(d)):
            if not f.endswith((".h", ".cpp")):
                continue
            with open(os.path.join(d, f), encoding="utf-8", errors="replace") as fp:
                text = fp.read()
            for m in DECLARED.finditer(text):
                for g in m.groups():
                    if g:
                        names.add(g)
            for m in TYPEDEF_TAIL.finditer(text):
                names.add(m.group(1))
    return names


def sources():
    """The reconstruction's translation units, both modules."""
    out = []
    for sub in ("a3dapi", "a3d"):
        d = os.path.join(ROOT, "src", sub)
        for f in sorted(os.listdir(d)):
            if f.endswith(".cpp"):
                out.append((sub, os.path.join(d, f)))
    return out


def run_cl(units):
    """Compile each unit for its layout report; return the concatenated text."""
    os.makedirs(WORK, exist_ok=True)
    bat = os.path.join(WORK, "run-cl.bat")

    lines = ["@echo off", 'call "%s" >nul' % VCVARS]
    for sub, path in units:
        args = [
            "cl",
            "/nologo",
            "/c",
            "/EHsc",
            "/d1reportAllClassLayout",
            "/I",
            os.path.join(ROOT, "src", sub),
            "/I",
            os.path.join(ROOT, "inc"),
            "/I",
            os.path.join(ROOT, "third_party", "minimp3"),
            "/I",
            os.path.join(ROOT, "third_party", "liba52"),
            "/Fo:" + os.path.join(WORK, "layout.obj"),
        ]
        args += ["/D" + d for d in DEFINES]
        args += [path]
        lines.append(subprocess.list2cmdline(args))

    with open(bat, "w", encoding="utf-8", newline="\r\n") as fp:
        fp.write("\n".join(lines) + "\n")

    proc = subprocess.run(
        [bat], cwd=WORK, capture_output=True, text=True, errors="replace"
    )
    text = proc.stdout + proc.stderr
    with open(os.path.join(WORK, "layout.txt"), "w", encoding="utf-8") as fp:
        fp.write(text)

    errors = [l for l in text.split("\n") if ": error " in l or ": fatal error" in l]
    if errors:
        sys.stderr.write("\n".join(errors[:40]) + "\n")
        raise SystemExit("cl reported %d errors; see %s" % (len(errors), WORK))
    return text


HEAD = re.compile(r"^(class|struct|union) (\S+)\s+size\((\d+)\):$")
VFT = re.compile(r"^(\S+?)::\$vftable@(?:(\S+)@)?:$")
ROW = re.compile(r"^\s*(\d+)\s*\|\s*(.*?)\s*$")


def parse(text):
    """Split the report into class layouts and vtables, first copy wins."""
    classes = OrderedDict()
    vtables = OrderedDict()

    lines = text.replace("\r\n", "\n").split("\n")
    i = 0
    while i < len(lines):
        m = HEAD.match(lines[i])
        if m:
            kind, name, size = m.group(1), m.group(2), int(m.group(3))
            rows = []
            i += 1
            # The body runs to the blank line that closes the drawing.  Rows
            # of a base subobject are indented inside `|` columns; the offset
            # printed is always from the start of the most derived object, so
            # every row is taken and the drawing characters dropped.
            while i < len(lines) and lines[i].strip():
                r = ROW.match(lines[i].replace("|", " ", 0))
                if r:
                    rows.append((int(r.group(1)), r.group(2).strip("| ")))
                i += 1
            classes.setdefault(name, (kind, size, rows))
            continue

        m = VFT.match(lines[i])
        if m:
            key = "%s@%s" % (m.group(1), m.group(2)) if m.group(2) else m.group(1)
            slots = []
            i += 1
            while i < len(lines) and lines[i].strip():
                r = ROW.match(lines[i])
                if r:
                    slots.append((int(r.group(1)), r.group(2).strip("&| ")))
                i += 1
            vtables.setdefault(key, slots)
            continue

        i += 1
    return classes, vtables


def main():
    names = declared_names()

    def ours(name):
        # A local type reported as `A3dFastSqrt::__l2::<unnamed-tag>` belongs
        # to the function it is declared in.
        return name.split("::")[0] in names

    text = run_cl(sources())
    classes, vtables = parse(text)

    classes = OrderedDict((k, v) for k, v in classes.items() if ours(k))
    vtables = OrderedDict((k, v) for k, v in vtables.items() if ours(k.split("@")[0]))

    with open(OUT, "w", encoding="utf-8", newline="\n") as fp:
        fp.write(HEADER % (len(classes), len(vtables)))

        fp.write("## Fields\n\n")
        for name in sorted(classes, key=str.lower):
            kind, size, rows = classes[name]
            fp.write("### %s - 0x%X bytes\n\n" % (name, size))
            for off, field in rows:
                fp.write("    0x%03X  %s\n" % (off, field))
            fp.write("\n")

        fp.write("## Vtables\n\n")
        fp.write(VTABLE_NOTE)
        for name in sorted(vtables, key=str.lower):
            slots = vtables[name]
            fp.write("### %s - %d slots\n\n" % (name, len(slots)))
            for slot, target in slots:
                fp.write("    %3d  %s\n" % (slot, target))
            fp.write("\n")

    print("wrote %s (%d classes, %d vtables)" % (OUT, len(classes), len(vtables)))


HEADER = r"""# Class layouts

NOT PART OF THE ORIGINAL.  Project tooling.  Generated by
`tools/analysis/generate_class_layout_report.py` using MSVC's
`/d1reportAllClassLayout`. Regenerate and review changes; do not edit by hand.

%d classes and %d vtables from `src/a3dapi` and `src/a3d`, excluding SDK,
CRT and decoder types. Hex field offsets are measured from the most-derived
object, including fields of base subobjects. These are compiler layouts;
source `sizeof`/`offsetof` assertions check the recorded constraints.

"""

VTABLE_NOTE = r"""`Class@IFace` identifies a base-interface vtable. `Class` alone denotes the
first base at offset 0. Multiple inheritance requires the corresponding
`this` adjustment when calling through another interface.

"""


if __name__ == "__main__":
    sys.exit(main())
