
import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Generate interface stubs.\npython tools/codegen/generate_interface_stubs.py <class> <header> <source> <interface>...')
    raise SystemExit(0)
#
# generate_interface_stubs.py - declare and define the interface methods a class does not
# have yet, so the tree compiles while the bodies are still unwritten.
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Generate missing pure-virtual overrides from inc/ia3dapi.h so incomplete
# classes can be instantiated. Each generated banner marks behavior as
# unknown until verified against 3.3.677. Validate SDK signatures against
# the binary before reconstruction. Parameters remain unnamed because the
# header provides only types.
#
# Usage:
#	python tools/codegen/generate_interface_stubs.py <class> <header> <source> <interface>...
#

import sys
import os
import re

HDR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
		   'inc', 'ia3dapi.h')

# Placeholder return values by type.
ZERO = {
	'ULONG': '0', 'DWORD': '0', 'INT': '0', 'int': '0', 'LONG': '0',
	'A3DVAL': '0.0f', 'float': '0.0f', 'BOOL': 'FALSE', 'LPVOID': 'NULL',
}


def interface_methods(text, name):
	"""[(name, return type, parameter text)] in slot order."""
	m = re.search(r'DECLARE_INTERFACE_\(%s,\s*\w+\)(.*?)\n\};' % name, text, re.S)
	if not m:
		raise SystemExit('%s: no such interface in the header' % name)
	out = []
	for mm in re.finditer(
			r'STDMETHOD(_)?\s*\(([^)]*)\)\s*\(THIS(_)?([^)]*)\)', m.group(1)):
		if mm.group(1):
			ret, nm = [x.strip() for x in mm.group(2).rsplit(',', 1)]
		else:
			ret, nm = 'HRESULT', mm.group(2).strip()
		params = ' '.join(mm.group(4).split()) if mm.group(3) else ''
		out.append((nm, ret, params))
	return out


def main():
	if len(sys.argv) < 5:
		sys.stderr.write(__doc__ or 'usage: generate_interface_stubs.py <class> <header>'
				 ' <source> <interface>...\n')
		return 2

	cls, hpath, cpath = sys.argv[1], sys.argv[2], sys.argv[3]
	ifaces = sys.argv[4:]

	hdr = open(HDR, encoding='latin-1').read()
	decl = open(hpath, encoding='utf-8', errors='replace').read()
	have = set(re.findall(r'\b(\w+)\s*\(', decl))

	decls, bodies = [], []
	for iface in ifaces:
		missing = [m for m in interface_methods(hdr, iface) if m[0] not in have]
		if not missing:
			continue

		decls.append('')
		decls.append('\t/* %s has no established 3.3.677 contract. */' % iface)
		bodies.append('')
		bodies.append('/* -------------------------------------------------------------------------- */')
		bodies.append('')
		bodies.append('/*')
		bodies.append(' * %s has no established 3.3.677 contract. Generated bodies use' % iface)
		bodies.append(' * type-default return values and carry no binary addresses.')
		bodies.append('*/')

		for nm, ret, params in missing:
			if ret == 'HRESULT':
				decls.append('\tSTDMETHODIMP\t%s(%s);' % (nm, params))
				sig = 'STDMETHODIMP'
				val = 'E_NOTIMPL'
			else:
				decls.append('\tSTDMETHODIMP_(%s)\t%s(%s);' % (ret, nm, params))
				sig = 'STDMETHODIMP_(%s)' % ret
				val = ZERO.get(ret, '0')

			bodies.append('')
			bodies.append('%s' % sig)
			bodies.append('%s::%s(%s)' % (cls, nm, params))
			bodies.append('{')
			bodies.append('\treturn (%s);' % val)
			bodies.append('}')

		sys.stderr.write('%s: %d from %s\n' % (cls, len(missing), iface))

	sys.stdout.write('/* ==== DECLARATIONS, paste into %s ==== */\n'
			 % os.path.basename(hpath))
	sys.stdout.write('\n'.join(decls) + '\n\n')
	sys.stdout.write('/* ==== DEFINITIONS, paste into %s ==== */\n'
			 % os.path.basename(cpath))
	sys.stdout.write('\n'.join(bodies) + '\n')
	return 0


if __name__ == '__main__':
	sys.exit(main())
