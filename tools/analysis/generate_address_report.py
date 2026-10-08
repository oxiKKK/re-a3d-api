#!/usr/bin/env python3
"""Build docs/llm/OFFSETS.md from the source banners and the groundtruth tables.

NOT PART OF THE ORIGINAL.  Project tooling.

Every body in src/a3dapi and src/a3d carries a banner naming the addresses it
was read at and the build each was read in (CLAUDE.md).  This
walks those banners, joins them against docs/llm/groundtruth/, and writes the
address database.  The source is the authority for the name; the groundtruth
tables are the authority for the size, the argument bytes and the vtable
slots.

Run it from the repository root:

    python tools/analysis/generate_address_report.py
"""

import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Generate address report.\npython tools/analysis/generate_address_report.py')
    raise SystemExit(0)

import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GT = os.path.join(ROOT, "docs", "llm", "groundtruth")
OUT = os.path.join(ROOT, "docs", "llm", "OFFSETS.md")

BUILDS = ("rtl", "dbg", "dbgv", "678")

# A banner is the block CLAUDE.md describes: a rule of `=`
# opening a C comment, `//` continuation lines, and a rule closing it.
BANNER = re.compile(r"/\* =+\n(.*?)^// =+\*/[ \t]*\n", re.S | re.M)

# `rtl:0x1000CC30`, and the optional `0x0A bytes, 0 args` that may follow it
# on the same line.
ADDR = re.compile(r"\b(rtl|dbg|dbgv|678):0x([0-9A-Fa-f]+)")

# Accept module/build tags and the older inline form in src/a3d.
A3D_ADDR = re.compile(r"(?:a3d\.dll rtl:|\(RE )0x([0-9A-Fa-f]+)(?=\b|\))")

# The first line of a banner, `GetWaveSize()\tReturn the size...`.
TITLE = re.compile(r"^//\s*([^\t]*?)\s*(?:\t+(.*))?$")

TRACE = re.compile(r"^//\s*TRACE\s+(\S+)", re.M)
SYM = re.compile(r"^//\s*sym\s+(\S+)", re.M)


def read_tsv(name):
    """Rows of a groundtruth table, comments and blanks dropped."""
    rows = []
    with open(os.path.join(GT, name), encoding="utf-8") as fp:
        for line in fp:
            if line.startswith("#") or not line.strip():
                continue
            rows.append(line.rstrip("\n").split("\t"))
    return rows


def functions(name):
    """addr -> (size, cbargs, corroboration, file) from a functions table."""
    out = {}
    for r in read_tsv(name):
        out[int(r[0], 16)] = (int(r[1], 16), r[3], r[6], r[7] if len(r) > 7 else "-")
    return out


def parse_banners(path, inline=False):
    """Every banner in one file, as dicts.

    `inline` reads a3d.dll module/build tags and legacy `(RE 0x…)` records.
    """
    with open(path, encoding="utf-8", errors="replace") as fp:
        text = fp.read()

    out = []
    for m in BANNER.finditer(text):
        body = m.group(1)
        if re.match(r"//\s*Class:", body):
            continue

        pattern = A3D_ADDR if inline else ADDR

        # Addresses are only trusted from the head of the banner, above the
        # first blank comment line that follows them.  Prose below cites other
        # functions' addresses and those are not this body's.
        head = []
        for line in body.split("\n"):
            head.append(line)
            if len(head) > 1 and line.strip() in ("//", ""):
                if any(pattern.search(h) for h in head):
                    break
        head = "\n".join(head)

        addrs = {}
        if inline:
            found = A3D_ADDR.findall(head)
            if found:
                addrs["a3d"] = int(found[0], 16)
        else:
            for build, hexaddr in ADDR.findall(head):
                addrs.setdefault(build, int(hexaddr, 16))

        title = TITLE.match(body.split("\n")[0])
        name = title.group(1) if title else ""
        desc = (title.group(2) or "") if title else ""

        tr = TRACE.search(head)
        sy = SYM.search(head)

        out.append(
            {
                "addrs": addrs,
                "title": name,
                "desc": desc,
                "trace": tr.group(1) if tr else "",
                "sym": sy.group(1) if sy else "",
                "sig": definition_after(text, m.end()),
            }
        )
    out.extend(parse_adjacent(text, inline))
    return out


# A brief adjacent comment on a definition inside a class or struct, which
# carries the addresses a function banner would.  AGENTS.md forbids banners
# there, so these blocks are the only record for those bodies.
# Files in this tree use either line ending, so the record may end "*/\r".
ADJACENT = re.compile(
    r"^[ \t]+/\* (?:\(RE\)|a3d\.dll rtl:|\(RE ).*?\*/[ \t\r]*$",
    re.S | re.M)


def strip_noncode(text):
    """`text` with comments and literals blanked, so braces can be counted."""
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    out = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    out = re.sub(r"//[^\n]*", blank, out)
    out = re.sub(r"'(?:\\.|[^'\\\n])*'|\"(?:\\.|[^\"\\\n])*\"", blank, out)
    return out


def enclosing_class(text, pos):
    """The innermost class or struct whose braces are open at `pos`, or ''."""
    stack = []
    depth = 0
    pending = None
    for m in re.finditer(r"\b(?:class|struct)\s+(\w+)|[{}]",
                         strip_noncode(text[:pos])):
        tok = m.group(0)
        if tok == "{":
            stack.append(pending)
            pending = None
            depth += 1
        elif tok == "}":
            if stack:
                stack.pop()
            depth = max(0, depth - 1)
        else:
            pending = m.group(1)
    names = [n for n in stack if n]
    return "::".join(names)


# Statements that can follow one of these comments without being the body it
# records; a `catch` funclet's address belongs to the enclosing function.
NOT_A_BODY = {"catch", "try", "do", "else"}

# Records that cite an address without being one body's entry: vtables, slot
# tables, accessors the tree does not declare, and stores into a field.
NOT_AN_ENTRY = re.compile(
    r"vtable|\bslots?\b|accessor|\btables?\b|\blabels?\b", re.I)

# `operator*`, which NAME_IN_SIG cannot spell.
OPERATOR_IN_SIG = re.compile(r"\b(operator\s*(?:[-+*/%^&|~!=<>]+|\(\)|\[\]))\s*\(")


def parse_adjacent(text, inline=False):
    """Address records held in brief comments beside in-class definitions."""
    out = []
    for m in ADJACENT.finditer(text):
        body = m.group(0)
        if NOT_AN_ENTRY.search(body):
            continue
        addrs = {}
        if inline:
            found = A3D_ADDR.findall(body)
            if found:
                addrs["a3d"] = int(found[0], 16)
        else:
            for build, hexaddr in ADDR.findall(body):
                addrs.setdefault(build, int(hexaddr, 16))
        if not addrs:
            continue
        # Only definitions inside a class or struct; a banner records the rest.
        cls = enclosing_class(text, m.start())
        if not cls:
            continue
        sig = definition_after(text, m.end())
        # A pure virtual declares no body, so it has no entry.
        if re.search(r"\)\s*(?:const\s*)?=\s*0\s*;", sig):
            continue
        title = ""
        op = OPERATOR_IN_SIG.search(sig)
        names = [op.group(1).replace(" ", "")] if op else [
            re.sub(r"\s*::\s*", "::", nm.group(1)) for nm in NAME_IN_SIG.finditer(sig)
        ]
        for name in names:
            if name in NOT_A_NAME or name in NOT_A_BODY:
                continue
            title = (cls + "::" + name + "()") if "::" not in name else name
            break
        # A comment that names no body records something else, such as an
        # accessor the tree does not declare.  Those are not rows.
        if not title:
            continue
        out.append(
            {
                "addrs": addrs,
                "title": title,
                "desc": "",
                "trace": "",
                "sym": "",
                "sig": sig,
            }
        )
    return out


def definition_after(text, pos):
    """The C++ definition that follows a banner, as one line, or ''."""
    buf = []
    for line in text[pos : pos + 800].split("\n"):
        s = line.strip()
        if not s or s.startswith("//") or s.startswith("/*") or s.startswith("*"):
            if buf:
                break
            if s.startswith("/*"):
                # The next banner, so this one has no body.
                return ""
            continue
        buf.append(s)
        joined = " ".join(buf)
        if "(" in joined:
            return joined
    return ""


# A definition's qualified name: an identifier, possibly class-qualified,
# followed by an argument list.
NAME_IN_SIG = re.compile(
    r"(~?[A-Za-z_][A-Za-z_0-9]*(?:\s*::\s*~?[A-Za-z_][A-Za-z_0-9]*)*)\s*\("
)

# Names that can precede the real one in a definition, because they take an
# argument list of their own.  `STDMETHODIMP_(ULONG) CA3dSource::AddRef()` is
# the common case.
NOT_A_NAME = {
    "STDMETHODIMP",
    "STDMETHODIMP_",
    "STDAPI",
    "STDAPI_",
    "EXTERN_C",
    "__declspec",
    "if",
    "for",
    "while",
    "switch",
    "return",
    "sizeof",
}


def symbol_of(entry):
    """The name to print for a banner."""
    for m in NAME_IN_SIG.finditer(entry["sig"]):
        name = re.sub(r"\s*::\s*", "::", m.group(1))
        if name not in NOT_A_NAME:
            title = entry["title"].removesuffix("()")
            if "::" not in name and title.endswith("::" + name):
                return title
            return name
    # No body: fall back to the banner's own title, which is what a
    # declared-but-unwritten function has.
    return entry["title"].rstrip("()") if entry["title"] else ""


def collect(subdir, inline=False):
    """Every banner under one source directory, keyed by build address."""
    entries = []
    d = os.path.join(ROOT, "src", subdir)
    for fn in sorted(os.listdir(d)):
        if not fn.endswith((".cpp", ".h")):
            continue
        for e in parse_banners(os.path.join(d, fn), inline):
            e["file"] = fn
            entries.append(e)
    return entries


def merge(entries, builds=BUILDS):
    """One row per distinct address pair, folding the duplicate banners.

    A body reached through several files (an inline in a header cited from the
    .cpp that uses it) has one address and several banners.  /OPT:ICF folds
    several Debug bodies onto one Retail address, so a Retail address can carry
    several names legitimately.
    """
    rows = defaultdict(lambda: {"names": [], "files": set(), "addrs": {}})
    for e in entries:
        if not e["addrs"]:
            continue
        key = tuple(e["addrs"].get(b) for b in builds)
        row = rows[key]
        row["addrs"] = e["addrs"]
        name = symbol_of(e)
        if name and name not in row["names"]:
            row["names"].append(name)
        row["files"].add(e["file"])
        if e["trace"]:
            row["trace"] = e["trace"]
        if e["sym"]:
            row["sym"] = e["sym"]
    return rows


def sort_key(key):
    """Retail order where there is a Retail address, Debug order after it."""
    rtl, dbg = key[0], key[1]
    return (0, rtl) if rtl is not None else (1, dbg or 0)


def fmt(a):
    return "`0x%08x`" % a if a is not None else ""


def module_section(fp, title, entries, rtl_funcs, dbg_funcs, note):
    rows = merge(entries)
    fp.write("# %s\n\n" % title)
    fp.write("## Functions\n\n")
    fp.write(note)
    fp.write("\n%d rows.\n\n" % len(rows))
    fp.write("| rtl | dbg | size | args | symbol | file |\n")
    fp.write("| --- | --- | --- | --- | --- | --- |\n")

    for key in sorted(rows, key=sort_key):
        row = rows[key]
        rtl = row["addrs"].get("rtl")
        dbg = row["addrs"].get("dbg")
        # The size and the argument bytes come from whichever build the row
        # names, Retail first, because Retail is the subject.
        size = args = ""
        if rtl is not None and rtl in rtl_funcs:
            size, args = "0x%x" % rtl_funcs[rtl][0], rtl_funcs[rtl][1]
        elif dbg is not None and dbg in dbg_funcs:
            size, args = "0x%x" % dbg_funcs[dbg][0], dbg_funcs[dbg][1]
        names = ", ".join("`%s`" % n for n in row["names"]) or "-"
        files = ", ".join(sorted(row["files"]))
        fp.write(
            "| %s | %s | %s | %s | %s | %s |\n"
            % (fmt(rtl), fmt(dbg), size, args, names, files)
        )
    fp.write("\n")
    return rows


def a3d_section(fp, entries, note):
    """`a3d.dll`, which has one image and so one address column."""
    rows = merge(entries, ("a3d",))
    fp.write("# a3d.dll\n\n")
    fp.write("## Functions\n\n")
    fp.write(note)
    fp.write("\n%d rows.\n\n" % len(rows))
    fp.write("| address | symbol | file |\n")
    fp.write("| --- | --- | --- |\n")
    for key in sorted(rows, key=lambda k: k[0] or 0):
        row = rows[key]
        names = ", ".join("`%s`" % n for n in row["names"]) or "-"
        fp.write(
            "| %s | %s | %s |\n"
            % (fmt(row["addrs"]["a3d"]), names, ", ".join(sorted(row["files"])))
        )
    fp.write("\n")


def unnamed_section(fp, rows, rtl_funcs):
    """Retail entries the tree names nowhere, split by whose code they are."""
    named = set()
    named_dbg = {}
    for row in rows.values():
        if "rtl" in row["addrs"]:
            named.add(row["addrs"]["rtl"])
        if "dbg" in row["addrs"] and row["names"]:
            named_dbg.setdefault(row["addrs"]["dbg"], row["names"][0])

    # Aureal's own source files, from the Debug region table.  The CRT test is
    # tools/analysis/infer_source_file_regions.py's: a `.c` file or `dbgdel.cpp` is the CRT's.
    aureal = {
        r[4]
        for r in read_tsv("regions-dbg.tsv")
        if not (r[4].endswith(".c") or r[4] == "dbgdel.cpp")
    }

    # A Retail address takes its file from the Debug function it pairs with,
    # because only the Debug build carries the __FILE__ sites the regions are
    # built from.
    fmap = {}
    pair = {}
    for r in read_tsv("funcmap.tsv"):
        if r[0] == "-" or r[1] == "-":
            continue
        fmap.setdefault(int(r[1], 16), r[5])
        pair.setdefault(int(r[1], 16), int(r[0], 16))

    ours = []
    other = defaultdict(int)
    by_dbg_only = 0
    for a in sorted(rtl_funcs):
        if a in named:
            continue
        if rtl_funcs[a][2] == "eh":
            other["compiler exception-handling plumbing"] += 1
            continue
        f = fmap.get(a) or rtl_funcs[a][3]
        if f in aureal:
            d = pair.get(a)
            if d in named_dbg:
                by_dbg_only += 1
            else:
                ours.append((a, f))
        elif f and f != "-":
            other["the static CRT and the bundled decoders"] += 1
        else:
            other["no Debug counterpart, so no file"] += 1

    fp.write("### Retail entries this tree does not name\n\n")
    fp.write(
        "Entries in `docs/llm/groundtruth/functions-rtl.tsv` that no banner cites,\n"
        "%d of %d.  A Retail address takes its file from the Debug function\n"
        "`funcmap.tsv` pairs it with, because only the Debug build carries the\n"
        "`__FILE__` sites the regions are built from.\n\n"
        % (len(rtl_funcs) - len(named), len(rtl_funcs))
    )
    for reason in sorted(other):
        fp.write("- %d: %s.\n" % (other[reason], reason))
    fp.write(
        "- %d: in an Aureal region, and the tree names the Debug function this\n"
        "  address pairs with, so the body is reconstructed and the banner\n"
        "  cites only its Debug address.\n" % by_dbg_only
    )
    fp.write(
        "\nThat leaves %d, listed below: Retail entries in an Aureal source\n"
        "region that nothing in the tree names at either address.  Three things\n"
        "put an address here.  A fold `/OPT:ICF` made, where the citing banner\n"
        "records the other copy's address.  A template member emitted once per\n"
        "instantiation, where the banner sits on the template in a header and\n"
        "cites one of them.  A function nothing has read.\n\n"
        "The `file` column is `funcmap.tsv`'s, which that table's own header\n"
        "calls informational and says carries the region table's known boundary\n"
        "errors.  The `ChunkPage.cpp` rows below are one: they are\n"
        "`CA3dMapperSecBuffer`'s `IDirectSoundBuffer` methods, which\n"
        "`src/a3dapi/ChunkPage.cpp`'s own banner places in `apimapper.cpp`.\n\n"
        % len(ours)
    )
    fp.write("| address | size | args | corroboration | file |\n")
    fp.write("| --- | --- | --- | --- | --- |\n")
    for a, f in ours:
        size, args, corr, _ = rtl_funcs[a]
        fp.write("| %s | 0x%x | %s | %s | %s |\n" % (fmt(a), size, args, corr, f))
    fp.write("\n")


def vtable_section(fp, rows):
    """Every vtable in both builds, its slots, and the class the link table names."""
    by_rtl = {}
    by_dbg = {}
    for row in rows.values():
        name = row["names"][0] if row["names"] else ""
        if not name:
            continue
        if "rtl" in row["addrs"]:
            by_rtl.setdefault(row["addrs"]["rtl"], name)
        if "dbg" in row["addrs"]:
            by_dbg.setdefault(row["addrs"]["dbg"], name)

    # A banner citing one build only still names the other build's body, so
    # carry each name across the Debug-to-Retail map before the slots are
    # looked up.  funcmap.tsv's `low` rows are positional pairs nothing
    # corroborates, so they are not used to carry a name.
    for r in read_tsv("funcmap.tsv"):
        if r[1] == "-" or r[0] == "-" or r[3] == "low":
            continue
        dbg, rtl = int(r[0], 16), int(r[1], 16)
        if dbg in by_dbg:
            by_rtl.setdefault(rtl, by_dbg[dbg])
        if rtl in by_rtl:
            by_dbg.setdefault(dbg, by_rtl[rtl])

    for build, table, index in (
        ("rtl", "vtables-rtl.tsv", by_rtl),
        ("dbg", "vtables-dbg.tsv", by_dbg),
    ):
        vts = defaultdict(list)
        cls = {}
        src = {}
        for r in read_tsv(table):
            vt = int(r[0], 16)
            vts[vt].append((int(r[1]), int(r[4], 16) if r[4] else 0, r[3]))
            if len(r) > 5 and r[5]:
                cls[vt] = r[5]
            if len(r) > 6 and r[6]:
                src[vt] = r[6]

        total = sum(len(v) for v in vts.values())
        pure = sum(1 for v in vts.values() for s in v if s[2] == "pure")
        named = sum(
            1 for v in vts.values() for s in v if s[2] != "pure" and s[1] in index
        )

        fp.write("## Vtables, %s\n\n" % build)
        fp.write(
            "%d tables, %d slots, %d of them `__purecall` in an abstract base's\n"
            "table.  This tree names %d of the %d that are not.\n\n"
            "`class` is the decorated vtable symbol from the Debug link table\n"
            "where it names one; there is no RTTI in these images, so a table\n"
            "the link table misses is identified only by the constructor that\n"
            "installs it.  `named` and `slots` below exclude the pure ones.\n\n"
            % (len(vts), total, pure, named, total - pure)
        )
        fp.write("| vtable | slots | named | class | file |\n")
        fp.write("| --- | --: | --: | --- | --- |\n")
        for vt in sorted(vts):
            slots = [s for s in vts[vt] if s[2] != "pure"]
            hit = sum(1 for _, target, _ in slots if target in index)
            fp.write(
                "| %s | %d | %d | %s | %s |\n"
                % (
                    fmt(vt),
                    len(slots),
                    hit,
                    "`%s`" % cls[vt] if vt in cls else "",
                    src.get(vt, ""),
                )
            )
        fp.write("\n")

        fp.write("### Slot order, %s\n\n" % build)
        for vt in sorted(vts):
            head = cls.get(vt) or src.get(vt) or "no named class"
            fp.write("#### %s - %d slots, %s\n\n" % (fmt(vt), len(vts[vt]), head))
            for slot, target, kind in sorted(vts[vt]):
                name = index.get(target, "")
                fp.write(
                    "    %3d  0x%08x  %-8s %s\n" % (slot, target, kind or "", name)
                )
            fp.write("\n")


def main():
    rtl_funcs = functions("functions-rtl.tsv")
    dbg_funcs = functions("functions-dbg.tsv")

    api = collect("a3dapi")
    a3d = collect("a3d", inline=True)

    with open(OUT, "w", encoding="utf-8", newline="\n") as fp:
        fp.write(HEADER)
        rows = module_section(
            fp,
            "a3dapi.dll",
            api,
            rtl_funcs,
            dbg_funcs,
            API_NOTE,
        )
        unnamed_section(fp, rows, rtl_funcs)
        vtable_section(fp, rows)
        fp.write("\n---\n\n")
        a3d_section(fp, a3d, A3D_NOTE)

    print("wrote %s" % OUT)


HEADER = r"""# Address index

NOT PART OF THE ORIGINAL.  Project tooling.  Generated by
`tools/analysis/generate_address_report.py` from source banners and
`docs/llm/groundtruth/`. Regenerate and review changes; do not edit by hand.

Addresses use imagebase `0x10000000`; select both module and build:

    rtl     3.3.677 Retail                     ref/a3dapi_33_rtl.dll
    dbg     3.3.677 Debug                      ref/a3dapi_33_dbg.dll
    dbgv    3.3.677 DebugViewer                ref/a3dapi_33_dbgv.dll
    678     3.3.678 driver, cross-check only   ref/a3dapi_33_678.dll

Slot names such as `Scene07` / `Prv1_05` mark functions awaiting investigation.
Address names such as `Unknown_0x10024260` retain an unresolved original name.

"""

API_NOTE = r"""`size` includes the entry's terminating instruction. `args` is the byte count
popped by `retn` (0 for plain `ret` or a tail jump). Both come from
`functions-{rtl,dbg}.tsv`, preferring Retail when available. Multiple names can
share a folded `/OPT:ICF` address; inline bodies can have multiple source files.

"""

A3D_NOTE = r"""The A3D 1.x driver shim is `ref/a3d_orig.dll` (98,304 bytes, linked
19 November 1999). It activates `CLSID_A3dApi` through COM. Its addresses
refer to this module at imagebase `0x10000000`; only one reference build exists.

"""


if __name__ == "__main__":
    sys.exit(main())
