"""Summarize an OpenCppCoverage Cobertura report for src/a3dapi.

NOT PART OF THE ORIGINAL.  Project tooling.

OpenCppCoverage lists a source file once per module that compiled it, so a
line is counted once and treated as covered if any module executed it.

Usage: python tools/coverage/summarize_coverage.py <coverage.xml> [rows]
"""

import os
import sys
import xml.etree.ElementTree as ET


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2

    rows = int(argv[2]) if len(argv) > 2 else 0
    tree = ET.parse(argv[1])
    lines = {}

    for cls in tree.iter('class'):
        name = os.path.basename(cls.get('filename').replace('\\', '/'))
        for line in cls.find('lines').iter('line'):
            key = (name, int(line.get('number')))
            lines[key] = lines.get(key, False) or int(line.get('hits')) > 0

    files = {}
    for (name, _), hit in lines.items():
        covered, total = files.get(name, (0, 0))
        files[name] = (covered + (1 if hit else 0), total + 1)

    covered = sum(c for c, _ in files.values())
    total = sum(t for _, t in files.values())
    untouched = [n for n, (c, _) in files.items() if c == 0]

    print('TOTAL %d / %d lines = %.1f%%  (%d files, %d with no coverage)'
          % (covered, total, 100.0 * covered / total, len(files), len(untouched)))

    ordered = sorted(files.items(), key=lambda kv: -(kv[1][1] - kv[1][0]))
    if rows:
        ordered = ordered[:rows]

    print('%-24s %6s %6s %7s %6s' % ('file', 'hit', 'lines', 'pct', 'miss'))
    for name, (c, t) in ordered:
        print('%-24s %6d %6d %6.1f%% %6d' % (name, c, t, 100.0 * c / t, t - c))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
