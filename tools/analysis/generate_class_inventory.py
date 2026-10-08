
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Generate class inventory.\npython tools/analysis/generate_class_inventory.py > docs/llm/groundtruth/classes.tsv')
    raise SystemExit(0)
#
# generate_class_inventory.py - combine class names, layouts and vtable evidence.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Join binary evidence into class rows:
#   symbols.tsv: decorated class/base names (??_7Class@@6BInterface@@@).
#   labels-dbg.tsv: TRACE class names and source regions.
#   vtables-dbg.tsv: slots and installer source files.
#   types-dbg.tsv: sizeof operands from ASSERT expressions.
#   regions-dbg.tsv: source attribution.
# Keep unnamed classes keyed by vtable address.
#
# Usage:
#	python tools/analysis/generate_class_inventory.py > docs/llm/groundtruth/classes.tsv
#

import sys
import os
import re
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe_image import tsv_header

#
# Measured by a sizeof() and not a class this tree declares: the C and Win32
# primitives, and the four operands that are not types at all, which
# types-dbg.tsv marks in its own note column.
#
PRIMITIVE = set('''
	BYTE char DWORD float FLOAT HANDLE HRESULT int LONG LPVOID ULONG
	A3DVAL LPDIRECTSOUND LPDIRECTSOUNDBUFFER
	props m_RefImageFilled pResManBuffer IID_IA3d2
'''.split())

GT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
		  'docs', 'llm', 'groundtruth')


def rows(name):
	out = []
	path = os.path.join(GT, name)
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		out.append(line.rstrip('\n').split('\t'))
	return out


def main():
	# How many slots each vtable has, for the count reported at the end.
	vt_slots = defaultdict(int)
	for r in rows('vtables-dbg.tsv'):
		vt_slots[r[0]] += 1

	# Vtable symbols identify class/base pairs; method symbols identify classes.
	vt_name = {}
	sym_methods = defaultdict(set)
	for r in rows('symbols.tsv'):
		if len(r) < 3 or r[1] != 'sym':
			continue
		m = re.match(r'\?\?_7(\w+)@@6B(\w+)@@@', r[2])
		if m:
			vt_name.setdefault(m.group(1), []).append(m.group(2))
			continue
		m = re.match(r'\?\??[_0-9]?([A-Za-z_]\w*)@([A-Za-z_]\w*)@@', r[2])
		if m:
			sym_methods[m.group(2)].add(m.group(1))

	# A class's labels, and the file they are pushed from.
	lab_file = defaultdict(lambda: defaultdict(int))
	lab_methods = defaultdict(set)
	for r in rows('labels-dbg.tsv'):
		if len(r) < 6:
			continue
		lab_methods[r[1]].add(r[2])
		if r[4] != '0' and r[5]:
			lab_file[r[1]][r[5]] += 1

	# Type names spelled by a sizeof().  The primitives and the Win32 types
	# are measured too and are not classes of Aureal's, so they are set
	# aside; types-dbg.tsv keeps them all.
	sized = {}
	for r in rows('types-dbg.tsv'):
		if len(r) > 3 and r[1] in ('sizeof', 'both') and r[0] not in PRIMITIVE:
			sized[r[0]] = r[3]

	# One row per named class.
	names = set(lab_file) | set(vt_name) | set(sized) | set(sym_methods)
	out = tsv_header('the other groundtruth tables',
			 'One row per class, joined from the vtable, label, symbol,\n'
			 'type and region tables.  `evidence` says which of them\n'
			 'named it: `sym` is the link table and is the strongest,\n'
			 '`label` is a TRACE push placed by region, `sizeof` is an\n'
			 'ASSERT naming the type.\n'
			 '\n'
			 '`file` is where the class lives.  For a label-named class\n'
			 'that is where its pushes fall; the count beside it is how\n'
			 'many of them agreed.\n'
			 '\n'
			 '`interfaces` are the base interfaces the link table gives,\n'
			 'which is only available for the classes it names.\n'
			 '\n'
			 'A class nothing names is not in this table.  Those are\n'
			 'reached through vtables-dbg.tsv, whose every row carries\n'
			 'the file of the constructor that installs it.')
	out.append('# class\tfile\tevidence\tmethods\tinterfaces')

	for cls in sorted(names, key=str.lower):
		ev = []
		if cls in vt_name or cls in sym_methods:
			ev.append('sym')
		if cls in lab_file:
			ev.append('label')
		if cls in sized:
			ev.append('sizeof')

		where = ''
		if cls in lab_file and lab_file[cls]:
			best = sorted(lab_file[cls].items(), key=lambda kv: -kv[1])
			where = '%s (%d)' % (best[0][0], best[0][1])
		elif cls in sized:
			where = sized[cls]

		out.append('%s\t%s\t%s\t%d\t%s'
			   % (cls, where, ','.join(ev),
			      len(lab_methods.get(cls, set())
				  | sym_methods.get(cls, set())),
			      ','.join(sorted(vt_name.get(cls, ())))))

	sys.stdout.write('\n'.join(out) + '\n')

	sys.stderr.write('%d classes named: %d by the link table, %d by labels, '
			 '%d by a sizeof\n'
			 % (len(names), len(set(vt_name) | set(sym_methods)),
			    len(lab_file), len(sized)))
	sys.stderr.write('%d vtables in the Debug build; %d of them are named by '
			 'the link table\n'
			 % (len(vt_slots), sum(len(v) for v in vt_name.values())))
	return 0


if __name__ == '__main__':
	sys.exit(main())
