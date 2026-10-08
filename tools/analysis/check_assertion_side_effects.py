#!/usr/bin/env python3
"""Check discarded diagnostic arguments for calls and mutations.

NOT PART OF THE ORIGINAL. Project tooling.
Run from any directory; unknown calls require review, even inside SUCCEEDED.
This is a conservative source check, not a C++ semantic analyzer.
"""
import collections
import pathlib
import re
import sys

# Reviewed predicates and borrowed-pointer accessors. Keep this list narrow.
PURE = {
    'sizeof', 'SUCCEEDED', 'FAILED', 'IsBadReadPtr', 'IsBadWritePtr',
    'IsBadCodePtr', 'IsBadStringPtrA', 'IsBadStringPtr', 'IsEqualGUID',
    'IsEqualIID', 'GetIDal', 'GetIDalBuffer', 'GetDSBuffer',
    'LIST_HAS', 'IsHrSucceeded',
}
DIAGNOSTIC_ONLY = {'Mp3SscErrorString'}
MACRO = re.compile(r'\b(ASSERT|VERIFY|TRACE|DBGSTR|_ASSERT|_ASSERTE|assert)\s*\(')
LITERAL = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|R"([^\s()\\]{0,16})\([\s\S]*?\)\1"'
    r'|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
MUTATION = re.compile(r'\+\+|--|(?<![=!<>])=(?!=)|\b(?:new|delete|throw)\b')


def blank(match):
    return ''.join('\n' if c == '\n' else ' ' for c in match[0])


def invocations(source):
    clean = LITERAL.sub(blank, source)
    # Macro definitions are checked separately by their runtime contract.
    clean = re.sub(r'(?m)^[ \t]*#\s*define(?:[^\n]*\\\n)*[^\n]*', blank, clean)
    for match in MACRO.finditer(clean):
        start = end = match.end()
        depth = 1
        while end < len(clean) and depth:
            depth += (clean[end] == '(') - (clean[end] == ')')
            end += 1
        if depth:
            raise ValueError('unclosed diagnostic macro')
        yield (source.count('\n', 0, match.start()) + 1,
               match[1], clean[start:end - 1])


def suspicious(macro, argument):
    if macro == 'VERIFY':
        return []
    allowed = PURE | (DIAGNOSTIC_ONLY if macro in {'TRACE', 'DBGSTR'} else set())
    issues = set(CALL.findall(argument)) - allowed
    issues.update(MUTATION.findall(argument))
    # Indirect calls such as (*callback)() and callbacks[index]().
    without_casts = re.sub(r'\(\s*(?:int|long|short|float|double|unsigned|signed|DWORD|LONG)\s*\)', '', argument)
    if re.search(r'[)\]]\s*\(', without_casts):
        issues.add('indirect call or cast requires review')
    return sorted(issues)


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    counts = collections.Counter()
    failures = []
    for path in sorted((root / 'src').rglob('*')):
        if path.suffix not in {'.cpp', '.h', '.c', '.hpp', '.inl'}:
            continue
        for line, macro, argument in invocations(path.read_text(encoding='utf-8')):
            counts[macro] += 1
            issues = suspicious(macro, argument)
            if issues:
                failures.append(f'{path.relative_to(root)}:{line}: {macro}: {", ".join(issues)}')
    print(', '.join(f'{name}: {count}' for name, count in sorted(counts.items())))
    print('\n'.join(failures) if failures else 'No unreviewed calls or mutations in discarded diagnostic arguments.')
    return bool(failures)


if __name__ == '__main__':
    if '--help' in sys.argv or '-h' in sys.argv:
        print(__doc__)
        sys.exit(0)
    sys.exit(main())
