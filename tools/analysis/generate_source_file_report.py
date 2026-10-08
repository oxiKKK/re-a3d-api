#!/usr/bin/env python3
"""Build docs/llm/FILEMAP.md, the 3.3 file map.

NOT PART OF THE ORIGINAL.  Project tooling.

Where each of Aureal's 3.3 source files is in each build, how its name was
established, and what this tree carries for it.  The tables come from
`docs/llm/groundtruth/`; the prose sections are written into this script so the
document regenerates whole.

Run it from the repository root:

    python tools/analysis/generate_source_file_report.py
"""

import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Generate source file report.\npython tools/analysis/generate_source_file_report.py')
    raise SystemExit(0)

import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GT = os.path.join(ROOT, "docs", "llm", "groundtruth")
SRC = os.path.join(ROOT, "src", "a3dapi")
OUT = os.path.join(ROOT, "docs", "llm", "FILEMAP.md")


def read_tsv(name):
    rows = []
    with open(os.path.join(GT, name), encoding="utf-8") as fp:
        for line in fp:
            if line.startswith("#") or not line.strip():
                continue
            rows.append(line.rstrip("\n").split("\t"))
    return rows


def is_crt(name):
    """tools/analysis/infer_source_file_regions.py's test."""
    return name.endswith(".c") or name == "dbgdel.cpp"


def source_list():
    """The link runs, read out of src/a3dapi/CMakeLists.txt."""
    runs = []
    current = None
    with open(os.path.join(SRC, "CMakeLists.txt"), encoding="utf-8") as fp:
        inside = False
        for line in fp:
            if line.startswith("set(A3DAPI_SOURCES"):
                inside = True
                continue
            if inside and line.startswith(")"):
                break
            if not inside:
                continue
            m = re.match(r"\s*#\s*(Run \d: .*?)\.?$", line)
            if m:
                current = (m.group(1), [])
                runs.append(current)
                continue
            if re.match(r"\s*#\s*3\.3 names no file", line):
                current = ("No run: 3.3 names no file for this code", [])
                runs.append(current)
                continue
            m = re.match(r"\s*(\S+\.cpp)\s*(?:#\s*(.*?))?\s*$", line)
            if m and current is not None:
                current[1].append((m.group(1), (m.group(2) or "").strip()))
    return runs


def tree_stats():
    """Lines and banner count per source file in this tree."""
    stats = {}
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith((".cpp", ".h")):
            continue
        with open(os.path.join(SRC, fn), encoding="utf-8", errors="replace") as fp:
            text = fp.read()
        stats[fn] = (
            text.count("\n") + 1,
            len(re.findall(r"^/\* =+\n(?!//\s*Class:)", text, re.M)),
        )
    return stats


def main():
    regions = {r[4]: r for r in read_tsv("regions-dbg.tsv")}
    regions_v = {r[4]: r for r in read_tsv("regions-dbgv.tsv")}

    dbg_count = defaultdict(int)
    for r in read_tsv("functions-dbg.tsv"):
        if len(r) > 7:
            dbg_count[r[7]] += 1

    # Where a file's code lands in Retail.  The link order is not the Debug
    # one and `/OPT:ICF` folds a body onto another file's copy, so the extreme
    # addresses are outliers; the 10th to 90th percentile is the run.
    rtl_addrs = defaultdict(list)
    for r in read_tsv("funcmap.tsv"):
        if r[0] == "-" or r[1] == "-" or r[3] == "low":
            continue
        if int(r[4]) > 1:
            # An `/OPT:ICF` fold, so this Retail address is shared with a body
            # from some other file and says nothing about where this file is.
            continue
        rtl_addrs[r[5]].append(int(r[1], 16))

    stats = tree_stats()
    runs = source_list()

    with open(OUT, "w", encoding="utf-8", newline="\n") as fp:
        fp.write(HEADER)
        fp.write(SECTION_1)

        fp.write("## 2. The map\n\n")
        fp.write(MAP_NOTE)
        for title, files in runs:
            fp.write("### %s\n\n" % title)
            fp.write(
                "| file | Debug region | funcs | Retail run | ours: lines | bodies |\n"
            )
            fp.write("| --- | --- | --: | --- | --: | --: |\n")
            for name, note in files:
                r = regions.get(name)
                if r:
                    region = "`0x%s`..`0x%s`" % (r[0][2:], r[1][2:])
                else:
                    region = re.sub(r"\s+", " ", note) or "no region"
                a = sorted(rtl_addrs.get(name, []))
                if len(a) >= 20:
                    run = "`0x%08x`..`0x%08x`" % (a[len(a) // 10], a[-1 - len(a) // 10])
                elif a:
                    run = "`0x%08x`..`0x%08x`" % (a[0], a[-1])
                else:
                    run = ""
                lines, bodies = stats.get(name, (0, 0))
                fp.write(
                    "| `%s` | %s | %s | %s | %d | %d |\n"
                    % (
                        name,
                        region,
                        dbg_count.get(name, "") or "",
                        run,
                        lines,
                        bodies,
                    )
                )
            fp.write("\n")

        aureal = [f for f in regions if not is_crt(f)]
        crt = [f for f in regions if is_crt(f)]
        fp.write("## 3. The regions, in Debug address order\n\n")
        fp.write(
            "%d regions, %d of them Aureal's and %d the static CRT's.  `sites`\n"
            "is how many `__FILE__` sites name the file, `lines` the highest line\n"
            "number any of them carries, which is a lower bound on the length of\n"
            "the file.  `start` and `end` are widened to the midpoint of the gap\n"
            "to the neighbouring file, so the part of a region between the\n"
            "outermost site and the boundary is inferred.  `strays` counts sites\n"
            "of another file inside the region, which is inlining or a boundary\n"
            "error; every one of these is 0.\n\n" % (len(regions), len(aureal), len(crt))
        )
        fp.write("| region | file | sites | lines | funcs | strays |\n")
        fp.write("| --- | --- | --: | --: | --: | --: |\n")
        for r in read_tsv("regions-dbg.tsv"):
            fp.write(
                "| `0x%s`..`0x%s` | `%s` | %s | %s | %d | %s |\n"
                % (r[0][2:], r[1][2:], r[4], r[5], r[6], dbg_count.get(r[4], 0), r[7])
            )
        fp.write("\n")

        agree = sum(
            1
            for f, r in regions.items()
            if f in regions_v and regions_v[f][6] == r[6]
        )
        fp.write(
            "The DebugViewer build is a separate compilation of the same source.\n"
            "It gives %d regions against the Debug build's %d, and the two agree\n"
            "on the highest line number for %d of the %d files they share.\n\n"
            % (len(regions_v), len(regions), agree, len(set(regions) & set(regions_v)))
        )

        fp.write(SECTION_4)
        fp.write(boundaries())
        fp.write(SECTION_6)

    print("wrote %s" % OUT)


def boundaries():
    """The contested boundaries, from the hand-read table."""
    rows = read_tsv("boundaries.tsv")
    out = [
        "## 5. The boundaries that had to be read\n\n",
        "A region boundary sits at the midpoint of the gap between the last\n"
        "witnessed site of one file and the first of the next, so a run of\n"
        "functions with no `__FILE__` site of its own falls on whichever side\n"
        "the midpoint lands.  These are the ones settled by reading the\n"
        "constructor at the low end of the span and seeing which class it\n"
        "builds.  `docs/llm/groundtruth/boundaries.tsv` carries the evidence per\n"
        "row; all addresses are `dbg:`.\n\n",
        "| before | after | span | class | belongs to | new start |\n",
        "| --- | --- | --- | --- | --- | --- |\n",
    ]
    for r in rows:
        out.append(
            "| `%s` | `%s` | `%s` | `%s` | `%s` | `%s` |\n"
            % (r[0], r[1], r[2], r[3], r[4], r[5])
        )
    out.append("\n")
    return "".join(out)


HEADER = r"""# Source-file map

NOT PART OF THE ORIGINAL.  Project tooling.  Generated by
`tools/analysis/generate_source_file_report.py` from `docs/llm/groundtruth/` and
`src/a3dapi/CMakeLists.txt`. Regenerate and review changes; do not edit by hand.

`src/a3dapi` follows the recovered filenames and ascending function-address
order. [Shim file attribution](FILEMAP-A3D.md) covers `src/a3d`, whose binary
has no filename records.

"""

SECTION_1 = r"""## 1. Filename evidence

- ASSERT `__FILE__` arguments: 1,028 sites in each instrumented 677 build,
  1,027 shared, identifying 39 Aureal files. Extraction uses
  `tools/analysis/extract_filelines.py`; see
  [debug formats](DEBUG-GENERATIONS.md#extraction-method) for `/ZI` decoding.
- Debug link table: 54 object names and 150 decorated symbols. Six filenames
  occur only here: `A3dFrame`, `A3dOpening`, `A3dOpeningBuilder`, `WallEdge`,
  `Waveform` and `softmix`. Table order places them within the three link runs.
- A COFF `.file` record. [Original filenames](ORIGINAL_FILENAMES.md) preserves
  extracted paths under `C:\Anzio 3.0\SRC\A3D\apidll\`.

Files without an established original name use project-chosen names below.

"""

MAP_NOTE = r"""Files follow the three alphabetical link runs in `src/a3dapi/CMakeLists.txt`.
`Retail run` is the 10th–90th percentile of mapped Retail addresses, excluding
low-confidence pairs and `/OPT:ICF` folds. With fewer than 20 pairs, it shows
the full range; with none, it is blank. Folded addresses cannot locate a unique
source file. `bodies` counts source banners.

Retail attribution is inferred through the Debug-to-Retail map; Retail has no
`__FILE__` records. Most mapped runs follow Debug order without overlap.

"""

SECTION_4 = """## 4. The files carrying a name this project chose

3.3 names no file for this code.  Each keeps a project name and has no place in
a link run.

| file | what it holds | why the name |
| --- | --- | --- |
| `A3dMatrix.cpp` | the 4x4 matrix and vector arithmetic | no 3.3 file is named for it; the code is called from every geometry file |
| `A3dPrivate.cpp` | the private helpers `A3dPrivate.h` declares | same |
| `Corners.cpp` | `CA3dCorners`, the bounding-volume corner set | same |
| `hrtfmgr.cpp` | `CHrtfMgr`, the HRTF coefficient tables | same |
| `MaterialLink.cpp` | `CA3dLink` and `CA3dChained`, the material list nodes | same |
| `outqueue.cpp` | `A3DQUEUE` and the output queue | same |
| `NamedObject.cpp` | the naming base at `dbg:0x1003d6f0` | the span no 3.3 image names.  Section 5's last row is its boundary, and `docs/llm/groundtruth/boundaries.tsv` records what is proven and what is not |
| `fmath.cpp` | `fmath::Init` and `A3dFastSqrt` | the Debug region folds into `ChunkPage.cpp`'s tail; the name is from the `fmath::` qualifier the link table carries |
| `splash.cpp` | the splash screen | no region of its own; `dbg:0x1003f340` is its one witnessed function.  Debug and DebugViewer only: neither the retail image nor its import table has `SHELL32` |
| `a3dguid.cpp` | the GUID definitions | project-added.  Data, no `.text` of its own |

"""

SECTION_6 = r"""## 6. Attribution limits

- Boundaries extend to gap midpoints where no filename record locates them.
  `regions-dbg.tsv` retains the first and last observed sites.
- `linklist.h` has no separate region: its 74 inline sites occur in 11 files.
- Link-table-only filenames have an order but no established address range;
  their code is included in neighboring inferred regions.
- Retail attribution inherits the Debug-to-Retail map's confidence limits.

"""


if __name__ == "__main__":
    sys.exit(main())
