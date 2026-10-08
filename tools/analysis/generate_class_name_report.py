#!/usr/bin/env python3
"""Build docs/llm/CLASSMAP.md, this tree's class names against Aureal's.

NOT PART OF THE ORIGINAL.  Project tooling.

The 2.02 subject had no instrumented build of its own version, so its class
names were inferred from a later generation by matching method sets, and the
old CLASSMAP carried a similarity score per class.  3.3.677 ships instrumented
builds of the same version, so a name is either read out of one of them or it
is this project's, and this document says which for every class.

Run it from the repository root:

    python tools/analysis/generate_class_name_report.py
"""

import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Generate class name report.\npython tools/analysis/generate_class_name_report.py')
    raise SystemExit(0)

import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GT = os.path.join(ROOT, "docs", "llm", "groundtruth")
OUT = os.path.join(ROOT, "docs", "llm", "CLASSMAP.md")

SRC = (
    os.path.join(ROOT, "src", "a3dapi"),
    os.path.join(ROOT, "src", "a3d"),
)

# Types named in a 3.3 image that are not Aureal classes: the Windows SDK's
# structures and interfaces, which an ASSERT names only because it takes their
# sizeof or queries them.
NOT_AUREAL = {
    "DSBCAPS",
    "DSBUFFERDESC",
    "DSCAPS",
    "DSDRIVERDESC",
    "WAVEFORMATEX",
    "IDirectSound",
    "IDirectSoundBuffer",
    "IKsPropertySet",
    "IEnumPins",
    "IPin",
    "REGFILTER",
    "I3DL2_LISTENERPROPERTIES",
}


def read_tsv(name):
    rows = []
    with open(os.path.join(GT, name), encoding="utf-8") as fp:
        for line in fp:
            if line.startswith("#") or not line.strip():
                continue
            rows.append(line.rstrip("\n").split("\t"))
    return rows


CLASS_DECL = re.compile(r"^\s*class\s+([A-Za-z_]\w*)\s*(?::[^{;]*)?\{", re.M)
METHOD = re.compile(
    r"^\s*(?:STDMETHOD(?:_)?\s*\([^)]*\)\s*)?"
    r"(?:virtual\s+|static\s+|inline\s+)*"
    r"[A-Za-z_][\w:*&<>, \t]*?"
    r"\b([A-Za-z_~]\w*)\s*\([^;{]*\)\s*(?:const\s*)?[;{=]"
)


COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def strip(text):
    """Comments and literals out, newlines kept, so brace counting is sound."""

    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    return STRING.sub(blank, COMMENT.sub(blank, text))


def our_classes():
    """Every class this tree declares, with its file and its method names."""
    out = {}
    for d in SRC:
        module = os.path.basename(d)
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".h"):
                continue
            with open(os.path.join(d, fn), encoding="utf-8", errors="replace") as fp:
                text = strip(fp.read())
            spans = []
            for m in CLASS_DECL.finditer(text):
                name = m.group(1)
                body, end = brace_body(text, m.end() - 1)

                # A class declared inside another class's body is nested, and
                # `CA3dStdList::iterator` is not a class of its own.
                for outer, lo, hi in spans:
                    if lo < m.start() < hi:
                        name = "%s::%s" % (outer, name)
                        break
                spans.append((name, m.start(), end))

                methods = set()
                for line in body.split("\n"):
                    mm = METHOD.match(line)
                    if mm and mm.group(1) not in ("if", "for", "while", "return"):
                        methods.add(mm.group(1))
                key = (module, name)
                if key in out:
                    out[key][1].update(methods)
                else:
                    out[key] = (fn, methods)
    return out


def brace_body(text, start):
    """The text between the brace at `start` and its match."""
    depth = 0
    i = start
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1 : i], i
        i += 1
    return text[start + 1 :], len(text)


def their_classes():
    """Aureal's classes, with the evidence that names each."""
    rows = {}
    for r in read_tsv("classes.tsv"):
        name = r[0]
        if name in NOT_AUREAL:
            continue
        rows[name] = {
            "file": r[1],
            "evidence": r[2],
            "methods": int(r[3]),
            "interfaces": r[4] if len(r) > 4 else "",
        }

    # Method names per class from the TRACE labels, so a class can be compared
    # method by method and not only by count.
    labels = defaultdict(set)
    for r in read_tsv("labels-dbg.tsv"):
        labels[r[1]].add(r[2])
    for name, row in rows.items():
        row["labelled"] = labels.get(name, set())

    # The link table's decorated symbols name a class, its methods and their
    # signatures, which is the strongest evidence in the image.
    symed = defaultdict(set)
    for r in read_tsv("symbols.tsv"):
        if r[1] != "sym":
            continue
        m = re.match(r"\?\??[_0-9]?([A-Za-z_]\w*)@([A-Za-z_]\w*)@@", r[2])
        if m:
            symed[m.group(2)].add(m.group(1))
    for name, row in rows.items():
        row["symed"] = symed.get(name, set())
    return rows, symed


# What places each class no 3.3 image names.  A class missing from this table
# is reported as such rather than left out, so the document cannot drift away
# from the tree.
PLACED_BY = {
    "CA3dBuilderIface": "the one-slot abstract base every geometry builder derives",
    "CA3dChained": "the chained list node `CRefAudBin` derives; declared beside it",
    "CA3dClassFactory": "the region, and the exported `DllGetClassObject` that builds it",
    "CA3dFrame": "the link table names `A3dFrame.obj`; the class is what its constructor builds",
    "CA3dGeomIface": "the one-slot abstract base every geometry primitive derives",
    "CA3dLink": "the material list node `CA3dMaterial` derives; `MaterialLink.cpp`"
    " is a name this project chose",
    "CA3dList": "the region, and the boundary read that moved its start to `dbg:0x100173c0`",
    "CA3dListener": "the `Listener.cpp` region",
    "CA3dMaterial": "the `MaterialObject.cpp` region",
    "CA3dNamed": "the naming base at `dbg:0x1003d6f0`, the one span no 3.3 image names a file for",
    "CA3dOpening": "the link table names `A3dOpening.obj`",
    "CA3dOpeningBuilder": "the link table names `A3dOpeningBuilder.obj`",
    "CA3dPolygon": "the `Polygon.cpp` region",
    "CA3dPolygonBuilder": "the `PolygonBuilder.cpp` region",
    "CA3dPool": "the chunk allocator in `ChunkPage.cpp`, which has a region of its own",
    "CA3dPtrList": "a container template; the instantiations are in several regions",
    "CA3dReflection": "the `A3dReflection.cpp` region",
    "CA3dRoom": "the `A3dRoom.cpp` region",
    "CA3dRoomBuilder": "the `A3dRoomBuilder.cpp` region",
    "CA3dScene": "the `A3dScene.cpp` region, and the boundary read that settled its start",
    "CA3dStdList": "a container template; the instance at `dbg:0x10152A30` settled"
    " its shape",
    "CA3dStdList::iterator": "nested in the container template",
    "CA3dWall": "the `A3dWall.cpp` region",
    "CA3dWallBuilder": "the `A3dWallBuilder.cpp` region",
    "CA3dWallEdge": "the link table names `WallEdge.obj`",
    "CList": "the container around `CList::CNode`, which a `sizeof()` ASSERT names in `linklist.h`",
    "CList::CNode": "the `sizeof()` ASSERT in `linklist.h`",
    "CMinimp3Decoder": "project-added, behind the decoder seam.  Not Aureal's code",
    "CRefAudBin": "the region, and the boundary read that moved its start to `dbg:0x1003e550`",
    "CReflection": "the reflection helper `D3DBuffer` owns, declared beside it",
    "DAL_D2D": "the string `DAL_D2D`, 15 times in each instrumented build and 3"
    " times in Retail, which is the only `DAL_*` string Retail carries",
    "DAL_EMU": "the string `DAL_EMU`, 4 times in each instrumented build and not"
    " in Retail",
}


def main():
    ours = our_classes()
    theirs, symed = their_classes()

    api = {n: v for (m, n), v in ours.items() if m == "a3dapi"}
    a3d = {n: v for (m, n), v in ours.items() if m == "a3d"}

    matched = sorted(set(theirs) & set(api))
    only_theirs = sorted(set(theirs) - set(api))
    only_ours = sorted(set(api) - set(theirs))

    with open(OUT, "w", encoding="utf-8", newline="\n") as fp:
        fp.write(HEADER % (len(matched), len(api)))
        fp.write(SECTION_1)

        fp.write("## 2. The classes a 3.3 image names, and what this tree has\n\n")
        fp.write(
            "%d of this tree's %d `src/a3dapi` classes carry a name read out of\n"
            "a 3.3.677 image.  `evidence` is which of the three named it.\n"
            "`theirs` counts the methods that evidence gives: the link table's\n"
            "decorated symbols where there are any, otherwise the distinct TRACE\n"
            "labels.  `ours` counts the method declarations in this tree's class.\n"
            "The two are not expected to be equal.  A TRACE label only exists for\n"
            "a method Aureal chose to trace, and the link table names only what\n"
            "the incremental linker had a record for, so `theirs` is a floor.\n\n"
            % (len(matched), len(api))
        )
        fp.write("| class | file | evidence | theirs | ours | interfaces |\n")
        fp.write("| --- | --- | --- | --: | --: | --- |\n")
        for name in matched:
            t = theirs[name]
            f, methods = api[name]
            their_n = len(t["symed"]) or len(t["labelled"]) or t["methods"]
            fp.write(
                "| `%s` | `%s` | %s | %d | %d | %s |\n"
                % (
                    name,
                    f,
                    t["evidence"],
                    their_n,
                    len(methods),
                    ", ".join("`%s`" % i for i in t["interfaces"].split(",") if i),
                )
            )
        fp.write("\n")

        fp.write("### What each evidence class is worth\n\n")
        fp.write(EVIDENCE)

        fp.write("## 3. Named in a 3.3 image, not a class in this tree\n\n")
        if only_theirs:
            fp.write(
                "%d names.  Each is either a structure rather than a class, a\n"
                "spelling variant of one already in the table, or a namespace.\n\n"
                % len(only_theirs)
            )
            fp.write("| name | file | evidence | what it is |\n")
            fp.write("| --- | --- | --- | --- |\n")
            for name in only_theirs:
                t = theirs[name]
                fp.write(
                    "| `%s` | %s | %s | %s |\n"
                    % (name, t["file"], t["evidence"], WHAT.get(name, ""))
                )
            fp.write("\n")

        fp.write("## 4. The classes this tree names itself\n\n")
        fp.write(
            "%d classes.  No `__FILE__` site, TRACE label, `sizeof()` ASSERT or\n"
            "link-table symbol names any of them, so the name is this project's.\n"
            "The column says what places the class, which is a separate question\n"
            "from what Aureal called it and is answered for every row.\n\n"
            % len(only_ours)
        )
        fp.write("| class | file | what places it |\n")
        fp.write("| --- | --- | --- |\n")
        missing = []
        for name in only_ours:
            f, _ = api[name]
            note = PLACED_BY.get(name)
            if note is None:
                missing.append(name)
                note = "**not recorded here; add it to `PLACED_BY` in the script**"
            fp.write("| `%s` | `%s` | %s |\n" % (name, f, note))
        fp.write("\n")
        if missing:
            sys.stderr.write(
                "warning: no PLACED_BY note for %s\n" % ", ".join(missing)
            )

        fp.write("## 5. `src/a3d`, the A3D 1.x shim\n\n")
        fp.write(
            "%d classes.  `a3d.dll` ships one image with no asserts, no TRACE\n"
            "labels and no link table, so no name in it is read out of a binary.\n"
            "`docs/llm/FILEMAP-A3D.md` section 1 recovers the naming system from\n"
            "`a3dapi`'s 39 filenames and applies it here.\n\n" % len(a3d)
        )
        fp.write("| class | file | methods |\n")
        fp.write("| --- | --- | --: |\n")
        for name in sorted(a3d):
            f, methods = a3d[name]
            fp.write("| `%s` | `%s` | %d |\n" % (name, f, len(methods)))
        fp.write("\n")

    print(
        "wrote %s (%d matched, %d ours only, %d theirs only)"
        % (OUT, len(matched), len(only_ours), len(only_theirs))
    )


WHAT = {
    "A3CSource": "a TRACE label inside `ac3fgraph.cpp`; one push, and the spelling suggests `Ac3Source`",
    "A3D_CAPS": "a structure, `sizeof()` in `A3d3.cpp` and `resman.cpp`",
    "A3DCAPS_HARDWARE": "a structure",
    "A3DCAPS_SOFTWARE": "a structure",
    "A3DCTRL_SRC_SUPER": "a structure, the source control block the DAL takes",
    "A3DREVERB_PROPERTIES": "a structure",
    "A3dGeom": "the label class for `CA3dRoot`'s `IA3dGeom2` methods, which are in `A3dGeom.cpp`",
    "A3dRoot": "a label `CA3dRoot` also carries; one push",
    "Ca3dSource": "a one-push spelling variant of `CA3dSource`",
    "CNode": "`CList::CNode`, a nested class, named by a `sizeof()` in `linklist.h`",
    "CPlex": "MFC's `CPlex`, which `Plex.h` declares as a struct rather than a"
    " class, so the scan over this tree does not see it",
    "DalInfo": "in the tree as `DalInfo`; the row is the `sizeof()` one",
    "fmath": "a namespace, not a class.  `fmath::Init` is the label",
    "IA3d2": "an interface `dsound.h`-style, declared in `src/a3dapi/a3d33.h`",
    "IA3dDal": "an interface, declared in `src/ia3ddal.h`",
    "IA3dDalBuffer": "an interface, declared in `src/ia3ddal.h`",
    "IA3dGeom2": "the published interface, declared in `inc/ia3dapi.h`",
    "IA3dPropertySet": "the published interface, declared in `inc/ia3dapi.h`",
    "REFLECTIONSORT": "a structure, the reflection priority sort record",
    "ResManBuffer": "in the tree as `ResManBuffer`; the row is the `sizeof()` one",
    "ResManStreamBuffer": "in the tree as `ResManStreamBuffer`; the row is the `sizeof()` one",
    "EMUBuffer": "in the tree as `EMUBuffer`; the row is the `sizeof()` one",
    "CWaveForm": "in the tree as `CWaveForm`; the row is the `sizeof()` one",
    "CHrtfMgr": "in the tree as `CHrtfMgr`; the row is the `sizeof()` one",
    "D2DBuffer": "in the tree as `D2DBuffer`; the row is the link table's",
    "DSBCAPS": "the Windows SDK's",
}

HEADER = r"""# Class-name evidence

NOT PART OF THE ORIGINAL.  Project tooling.  Generated by
`tools/analysis/generate_class_name_report.py` from `docs/llm/groundtruth/` and
declarations in `src/`. Regenerate and review changes; do not edit by hand.

%d of %d `src/a3dapi` class names have evidence in a 3.3.677 image.

"""

SECTION_1 = r"""## 1. Evidence types

All three use `(RE)` evidence from the instrumented 677 builds:

| Type | Establishes | Recorded scope |
| --- | --- | --- |
| `sym` | Class/method names, signatures and vtable bases | Debug link table: 54 object names, 150 symbols, 13 classes |
| `label` | TRACE method names; push locations establish file attribution | 394 labels, 21 classes |
| `sizeof` | Type name in an ASSERT; no method information | 57 types |

The 677 images have no RTTI class names.

"""

EVIDENCE = r"""Symbols establish signatures, TRACE labels establish only named methods,
and `sizeof` assertions establish type names. Multiple evidence types provide
independent checks; they do not establish implementation correctness.

"""


if __name__ == "__main__":
    sys.exit(main())
