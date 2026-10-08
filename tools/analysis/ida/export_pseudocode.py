
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Export pseudocode.\npython tools/analysis/ida/export_pseudocode.py <idb> <out-dir>')
    raise SystemExit(0)
#
# export_pseudocode.py - Hex-Rays pseudocode for every Aureal function in the
#	3.3.677 Debug build, one artifact per source file.
#
# NOT PART OF THE ORIGINAL.  Project tooling.  Its output is not committed:
# regenerating it needs idalib and a Hex-Rays licence, and the pseudocode
# runs to several hundred thousand lines, which does not belong in the
# tree.
#
# Read addresses, sizes and argument sizes from functions-dbg.tsv, and
# Retail mappings from funcmap.tsv. IDA supplies the pseudocode.
#
# A function is exported when its `file` column names an Aureal source
# file.  The CRT is skipped, by the same rule infer_source_file_regions.py uses: a file
# ending in .c, or dbgdel.cpp, is the CRT and not Aureal's.  A function
# whose `file` column is `-` belongs to no known region and is skipped too.
#
# Functions are written to <out-dir>/<file>.txt in ascending address order,
# each preceded by a one-line header:
#
#	// dbg:0x1001eb0c  size=0x1a1  cbargs=0  rtl:0x1000d123
#
# `rtl:` is omitted where funcmap.tsv gives no counterpart.  A function
# Hex-Rays could not decompile still gets its header, followed by one line
# recording the error, rather than being dropped from the artifact.
#
# <out-dir>/index.tsv then carries one row per exported function: its
# Debug address, its source file, the artifact that holds it, and the line
# inside that artifact where its header starts. Boundaries in regions-dbg.tsv
# are inferred from sparse source-location evidence. When a
# boundary moves, look up the affected addresses in the index, find their
# new file in the corrected regions-dbg.tsv, move just those functions
# between artifacts, and rewrite the index rows for them.  Nothing else
# needs to be re-decompiled.
#
# Use a private database copy under <out-dir>/_idb/ because idalib locks
# id0/id1/nam files exclusively. Retain it between runs; --clean-idb
# forces a fresh unpack.
#
# Disable COLLAPSE_LVARS in a private hexrays.cfg so headless output includes
# local declarations. Set IDAUSR to that config and IDADIR to the install
# directory before importing idapro; idalib reads both during initialization.
#
# Usage:
#	python tools/analysis/ida/export_pseudocode.py <idb> <out-dir>
#	    [--groundtruth DIR]   default docs/llm/groundtruth
#	    [--limit N]           stop after N functions, for a dry run
#	    [--clean-idb]         wipe <out-dir>/_idb and re-unpack first
#
#	<idb> is the analysed Debug database, for example:
#	  ref/a3dapi_33_dbg.dll.i64
#

import sys
import os
import json
import shutil
import time
from collections import defaultdict, Counter

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from pe_image import tsv_header


def is_crt(name):
	"""Same rule as infer_source_file_regions.py: the CRT is *.c, plus dbgdel.cpp."""
	return name.endswith('.c') or name == 'dbgdel.cpp'


def read_functions(path):
	"""addr -> (size, cbargs, sha1, corroboration, file)."""
	out = {}
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		addr, size, term, cbargs, entry, sha1, corrob, fname = line.rstrip('\n').split('\t')
		out[int(addr, 16)] = (int(size, 16), int(cbargs), sha1, corrob, fname)
	return out


def read_funcmap(path):
	"""dbg addr -> rtl addr string, only where a Retail counterpart exists."""
	out = {}
	for line in open(path, encoding='utf-8'):
		if line.startswith('#') or not line.strip():
			continue
		cols = line.rstrip('\n').split('\t')
		dbg, rtl = cols[0], cols[1]
		if dbg == '-' or rtl == '-':
			continue
		out[int(dbg, 16)] = rtl
	return out


def parse_args(argv):
	if len(argv) < 2:
		sys.stderr.write(
			'usage: export_pseudocode.py <idb> <out-dir> [--groundtruth DIR] '
			'[--limit N] [--clean-idb]\n')
		return None
	args, opt, rest = [], {'groundtruth': 'docs/llm/groundtruth', 'limit': None,
			       'clean-idb': False}, list(argv[1:])
	while rest:
		a = rest.pop(0)
		if a == '--clean-idb':
			opt['clean-idb'] = True
		elif a.startswith('--') and rest:
			opt[a[2:]] = rest.pop(0)
		else:
			args.append(a)
	if len(args) != 2:
		sys.stderr.write('usage: export_pseudocode.py <idb> <out-dir> ...\n')
		return None
	opt['limit'] = int(opt['limit']) if opt['limit'] is not None else None
	return args[0], args[1], opt


def open_private_copy(idb_path, out_dir, clean):
	"""Unpack a private copy of the database under out-dir/_idb so this run
	cannot collide with a session already holding the original open."""
	workdir = os.path.join(out_dir, '_idb')
	if clean and os.path.isdir(workdir):
		shutil.rmtree(workdir)
	os.makedirs(workdir, exist_ok=True)
	copy_path = os.path.join(workdir, os.path.basename(idb_path))
	if not os.path.exists(copy_path):
		shutil.copy2(idb_path, copy_path)
	return copy_path


def ida_install_dir():
	"""Same lookup idapro.config.get_ida_install_dir() does, read directly so
	it can run before IDAUSR is pointed elsewhere (see setup_idalib_env)."""
	idadir = os.environ.get('IDADIR')
	if idadir:
		return idadir
	path = os.path.join(os.environ['APPDATA'], 'Hex-Rays', 'IDA Pro', 'ida-config.json')
	with open(path, encoding='utf-8') as f:
		return json.load(f)['Paths']['ida-install-dir']


def setup_idalib_env(out_dir):
	"""Point IDAUSR at a private cfg directory carrying the installed
	hexrays.cfg with COLLAPSE_LVARS flipped to NO, so a decompiled function's
	local declarations come out in full rather than behind Hex-Rays' usual
	"[COLLAPSED LOCAL DECLARATIONS]" placeholder, which has no headless way
	to expand.  cfg lookup falls back to the install directory for every
	other file, so this is the only override needed.  Must run before
	`import idapro`: idalib reads IDADIR and IDAUSR at library init, and
	IDAUSR's own resolution of the install directory (idapro/config.py)
	stops working once IDAUSR points here instead of the real user config,
	which is why IDADIR is set explicitly alongside it."""
	install_dir = ida_install_dir()
	override_dir = os.path.join(out_dir, '_idausr')
	os.makedirs(os.path.join(override_dir, 'cfg'), exist_ok=True)
	src = os.path.join(install_dir, 'cfg', 'hexrays.cfg')
	text = open(src, encoding='utf-8').read()
	patched = text.replace('COLLAPSE_LVARS            = YES',
			       'COLLAPSE_LVARS            = NO')
	if patched == text:
		sys.stderr.write('warning: COLLAPSE_LVARS not found in %s as expected; '
				 'local declarations may render collapsed\n' % src)
	with open(os.path.join(override_dir, 'cfg', 'hexrays.cfg'), 'w', encoding='utf-8') as f:
		f.write(patched)
	os.environ['IDADIR'] = install_dir
	os.environ['IDAUSR'] = override_dir


def init_hexrays():
	"""idalib does not load the decompiler plugin on its own; a script that
	wants Hex-Rays has to load it itself.  Same route as IDA's own
	python/examples/decompiler/decompile_entry_points.py."""
	import ida_idp
	import ida_loader
	import ida_hexrays
	ALL_DECOMPILERS = {
		ida_idp.PLFM_386: 'hexx64',
		ida_idp.PLFM_ARM: 'hexarm',
		ida_idp.PLFM_PPC: 'hexppc',
		ida_idp.PLFM_MIPS: 'hexmips',
		ida_idp.PLFM_RISCV: 'hexrv',
	}
	decompiler = ALL_DECOMPILERS.get(ida_idp.ph.id)
	if not decompiler:
		raise RuntimeError('no known decompiler for architecture id %d' % ida_idp.ph.id)
	if ida_hexrays.init_hexrays_plugin():
		return ida_hexrays
	if not ida_loader.load_plugin(decompiler):
		raise RuntimeError('could not load decompiler plugin %r' % decompiler)
	if not ida_hexrays.init_hexrays_plugin():
		raise RuntimeError('could not initialise Hex-Rays after loading %r' % decompiler)
	return ida_hexrays


def main():
	parsed = parse_args(sys.argv)
	if parsed is None:
		return 2
	idb_path, out_dir, opt = parsed

	funcs = read_functions(os.path.join(opt['groundtruth'], 'functions-dbg.tsv'))
	funcmap = read_funcmap(os.path.join(opt['groundtruth'], 'funcmap.tsv'))

	buckets = defaultdict(list)
	for addr, (size, cbargs, sha1, corrob, fname) in funcs.items():
		if fname == '-' or is_crt(fname):
			continue
		buckets[fname].append(addr)
	for fname in buckets:
		buckets[fname].sort()

	# Files in the order their addresses start, matching regions-dbg.tsv.
	files = sorted(buckets, key=lambda f: buckets[f][0])

	total = sum(len(v) for v in buckets.values())
	if opt['limit'] is not None:
		sys.stderr.write('--limit %d: exporting only the first %d of %d functions\n'
				 % (opt['limit'], opt['limit'], total))

	os.makedirs(out_dir, exist_ok=True)
	copy_path = open_private_copy(idb_path, out_dir, opt['clean-idb'])
	setup_idalib_env(out_dir)

	sys.stderr.write('opening private copy %s ...\n' % copy_path)
	import idapro
	idapro.open_database(copy_path, False)
	try:
		ida_hexrays = init_hexrays()

		index_rows = []
		ok, failed = 0, 0
		fail_reasons = Counter()
		done = 0
		t0 = time.time()

		for fname in files:
			addrs = buckets[fname]
			artifact = fname + '.txt'
			out_path = os.path.join(out_dir, artifact)
			with open(out_path, 'w', encoding='utf-8', errors='replace', newline='\n') as f:
				banner = (
					'// %s -- Hex-Rays pseudocode, 3.3.677 Debug build.\n'
					'// NOT PART OF THE ORIGINAL.  Generated by\n'
					'// tools/analysis/ida/export_pseudocode.py from %s.\n'
					'// %d functions, ascending address order.\n'
					'//\n'
					'// Header line before each function:\n'
					'//   // dbg:<addr>  size=<bytes>  cbargs=<bytes popped '
					'by retn>  rtl:<addr>\n'
					'// rtl: is omitted where funcmap.tsv gives no Retail '
					'counterpart.\n'
					'// A function Hex-Rays could not decompile carries its\n'
					'// header and one FAILED line in place of a body.\n'
					'//\n'
					'// File boundaries are inferred; see docs/llm/FILEMAP.md.\n'
					'\n' % (artifact, idb_path, len(addrs)))
				f.write(banner)
				line_no = banner.count('\n') + 1

				for addr in addrs:
					if opt['limit'] is not None and done >= opt['limit']:
						break
					size, cbargs, sha1, corrob, _ = funcs[addr]
					rtl = funcmap.get(addr)
					header = '// dbg:0x%08x  size=0x%x  cbargs=%d' % (addr, size, cbargs)
					if rtl:
						header += '  rtl:%s' % rtl
					index_rows.append((addr, fname, artifact, line_no))
					f.write(header + '\n')
					line_no += 1

					try:
						# Invalidate cached ctrees so COLLAPSE_LVARS applies to earlier decompilations.
						ida_hexrays.mark_cfunc_dirty(addr)
						cfunc = ida_hexrays.decompile(addr)
					except Exception as e:
						cfunc = None
						reason = '%s: %s' % (type(e).__name__, e)
					else:
						reason = 'decompile() returned None' if cfunc is None else None

					if cfunc is None:
						failed += 1
						fail_reasons[reason] += 1
						f.write('// DECOMPILATION FAILED: %s\n\n' % reason)
						line_no += 2
					else:
						ok += 1
						text = str(cfunc)
						f.write(text)
						if not text.endswith('\n'):
							f.write('\n')
						f.write('\n')
						line_no += text.count('\n') + 1

					done += 1
					if done % 200 == 0:
						dt = time.time() - t0
						sys.stderr.write('  %d/%d (%d ok, %d failed) %.1fs\n'
								 % (done, total, ok, failed, dt))

			if opt['limit'] is not None and done >= opt['limit']:
				break

		note = ('One row per function exported by export_pseudocode.py.  `addr` is\n'
			'the Debug entry point, `file` its owning source file, `artifact`\n'
			'the .txt file that holds it under this directory, and `line` the\n'
			'1-based line where its header starts inside that artifact.\n'
			'\n'
			'File boundaries in regions-dbg.tsv are inferred. When one changes,\n'
			'look up the addresses it affects\n'
			'here, find their corrected file, move just those functions to the\n'
			'other artifact, and rewrite these rows for them; nothing else\n'
			'needs to be re-decompiled.')
		index_out = tsv_header(os.path.basename(idb_path), note)
		index_out.append('# addr\tfile\tartifact\tline')
		for addr, fname, artifact, line_no in index_rows:
			index_out.append('0x%08x\t%s\t%s\t%d' % (addr, fname, artifact, line_no))
		with open(os.path.join(out_dir, 'index.tsv'), 'w', encoding='utf-8', newline='\n') as f:
			f.write('\n'.join(index_out) + '\n')

	finally:
		import idapro
		idapro.close_database(False)

	sys.stderr.write('%d files, %d functions, %d ok, %d failed\n'
			 % (len(files), done, ok, failed))
	for reason, n in fail_reasons.most_common(10):
		sys.stderr.write('  failed x%d: %s\n' % (n, reason))
	return 0


if __name__ == '__main__':
	sys.exit(main())
