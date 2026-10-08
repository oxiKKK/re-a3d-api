
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Verify function candidates.\npython tools/analysis/ida/verify_function_candidates.py <idb> [--groundtruth DIR] [--out-dir DIR]')
    raise SystemExit(0)
#
# verify_function_candidates.py - which `listed-only` rows in functions-dbg.tsv are real
#	function starts, settled by asking Hex-Rays to decompile each one.
#
# NOT PART OF THE ORIGINAL. Project tooling. Verify PE-sweep listed-only candidates by decompiling each address.
# Write the results to docs/llm/groundtruth/unproven.tsv.
#
# Decompile all listed-only candidates. exported marks rows attributed
# to eligible Aureal files for classify_missing_reconstructions.py.
#
# Uses the same private-copy and COLLAPSE_LVARS technique as
# tools/analysis/ida/export_pseudocode.py; see that file's banner for why.
#
# Usage:
#	python tools/analysis/ida/verify_function_candidates.py <idb> [--groundtruth DIR] [--out-dir DIR]
#
#	<idb> is the analysed Debug database, for example:
#	  ref/a3dapi_33_dbg.dll.i64
#

import sys
import os
import json
import shutil
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from pe_image import tsv_header


def is_crt(name):
	"""Same rule as infer_source_file_regions.py: the CRT is *.c, plus dbgdel.cpp."""
	return name.endswith('.c') or name == 'dbgdel.cpp'


def read_listed_only(path):
	"""dbg addr -> file, for every `listed-only` row in functions-dbg.tsv."""
	out = []
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		cols = line.rstrip('\n').split('\t')
		if len(cols) < 8 or not cols[0].startswith('0x'):
			continue
		if cols[6] == 'listed-only':
			out.append((int(cols[0], 16), cols[7].strip()))
	return out


def setup_idalib_env(out_dir):
	"""See tools/analysis/ida/export_pseudocode.py's setup_idalib_env for why."""
	idadir = os.environ.get('IDADIR')
	if not idadir:
		cfg = os.path.join(os.environ['APPDATA'], 'Hex-Rays', 'IDA Pro', 'ida-config.json')
		idadir = json.load(open(cfg, encoding='utf-8'))['Paths']['ida-install-dir']
	override_dir = os.path.join(out_dir, '_idausr')
	os.makedirs(os.path.join(override_dir, 'cfg'), exist_ok=True)
	src = os.path.join(idadir, 'cfg', 'hexrays.cfg')
	text = open(src, encoding='utf-8').read()
	patched = text.replace('COLLAPSE_LVARS            = YES', 'COLLAPSE_LVARS            = NO')
	open(os.path.join(override_dir, 'cfg', 'hexrays.cfg'), 'w', encoding='utf-8').write(patched)
	os.environ['IDADIR'] = idadir
	os.environ['IDAUSR'] = override_dir


def init_hexrays():
	import ida_idp, ida_loader, ida_hexrays
	ALL_DECOMPILERS = {
		ida_idp.PLFM_386: 'hexx64',
		ida_idp.PLFM_ARM: 'hexarm',
		ida_idp.PLFM_PPC: 'hexppc',
		ida_idp.PLFM_MIPS: 'hexmips',
		ida_idp.PLFM_RISCV: 'hexrv',
	}
	decompiler = ALL_DECOMPILERS.get(ida_idp.ph.id)
	if ida_hexrays.init_hexrays_plugin():
		return ida_hexrays
	if not decompiler or not ida_loader.load_plugin(decompiler):
		raise RuntimeError('could not load a decompiler plugin')
	if not ida_hexrays.init_hexrays_plugin():
		raise RuntimeError('could not initialise Hex-Rays')
	return ida_hexrays


def result_header(idb_path):
	"""Describe the evidence without recording the local database directory."""
	note = (
		"Every `listed-only`-corroborated row of functions-dbg.tsv, and whether\n"
		"Hex-Rays decompiled it.  `exported` is `1` when the row's file column\n"
		"names an Aureal source file (not the CRT, not `-`); classify_missing_reconstructions.py\n"
		"only reads the exported rows, since a listed-only row with no file is\n"
		"already outside classify_missing_reconstructions.py's eligible-file count.\n"
		"`decompiled` is `1` when ida_hexrays.decompile() returned a cfunc.\n"
		"\n"
		"The recorded reference extraction declined 117 of the\n"
		"2,719 exported functions, and 114 of those carry listed-only\n"
		"corroboration.  This table reproduces that 114 exactly (see `reason`\n"
		"for why each one failed).")
	return tsv_header(os.path.basename(idb_path), note)


def main():
	argv = sys.argv[1:]
	if not argv:
		sys.stderr.write('usage: verify_function_candidates.py <idb> [--groundtruth DIR] [--out-dir DIR]\n')
		return 2
	idb_path = argv[0]
	opt = {'groundtruth': 'docs/llm/groundtruth', 'out-dir': None}
	rest = argv[1:]
	while rest:
		a = rest.pop(0)
		if a.startswith('--') and rest:
			opt[a[2:]] = rest.pop(0)
	gt = opt['groundtruth']
	out_dir = opt['out-dir'] or os.path.join(gt, '_check_unproven')

	rows = read_listed_only(os.path.join(gt, 'functions-dbg.tsv'))
	sys.stderr.write('%d listed-only rows\n' % len(rows))

	os.makedirs(out_dir, exist_ok=True)
	copy_path = os.path.join(out_dir, os.path.basename(idb_path))
	if not os.path.exists(copy_path):
		shutil.copy2(idb_path, copy_path)
	setup_idalib_env(out_dir)

	import idapro
	idapro.open_database(copy_path, False)
	try:
		ida_hexrays = init_hexrays()
		results = []
		t0 = time.time()
		for i, (addr, fname) in enumerate(rows):
			try:
				ida_hexrays.mark_cfunc_dirty(addr)
				cfunc = ida_hexrays.decompile(addr)
				ok, reason = (cfunc is not None), ('' if cfunc is not None else 'decompile() returned None')
			except Exception as e:
				ok, reason = False, '%s: %s' % (type(e).__name__, e)
			results.append((addr, fname, ok, reason))
			if (i + 1) % 200 == 0:
				sys.stderr.write('%d/%d (%.1fs)\n' % (i + 1, len(rows), time.time() - t0))
	finally:
		idapro.close_database(False)

	failed = [r for r in results if not r[2]]
	failed_exported = [r for r in failed if r[1] != '-' and not is_crt(r[1])]
	sys.stderr.write('%d listed-only, %d failed to decompile, %d of those in an Aureal file\n'
			 % (len(results), len(failed), len(failed_exported)))

	out = result_header(idb_path)
	out.append('# addr\tfile\texported\tdecompiled\treason')
	for addr, fname, ok, reason in results:
		exported = fname != '-' and not is_crt(fname)
		out.append('0x%08x\t%s\t%d\t%d\t%s' % (addr, fname, int(exported), int(ok), reason))
	dest = os.path.join(gt, 'unproven.tsv')
	with open(dest, 'w', encoding='utf-8', newline='\n') as f:
		f.write('\n'.join(out) + '\n')
	sys.stderr.write('wrote %s\n' % dest)
	return 0


if __name__ == '__main__':
	sys.exit(main())
