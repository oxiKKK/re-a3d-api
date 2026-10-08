#!/usr/bin/env python3
"""Check repository documentation file links, Markdown anchors, and source lines.

Project-written tooling. Checks docs/ plus project README/convention pages.
External URLs are not fetched. Code fences are excluded. No dependencies.
"""
from collections import Counter
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]


def prose(text):
    return re.sub(r"(?ms)^\s*(`{3,}|~{3,})[^\n]*\n.*?^\s*\1\s*$", "", text)


def anchors(text):
    seen = Counter()
    result = set()
    for match in re.finditer(r"(?m)^#{1,6}\s+(.+?)\s*#*\s*$", prose(text)):
        title = re.sub(r"\[([^]]+)\]\([^)]*\)", r"\1", match[1])
        slug = re.sub(r"[^\w\- ]", "", title.lower()).replace(" ", "-")
        index = seen[slug]
        seen[slug] += 1
        result.add(slug if index == 0 else f"{slug}-{index}")
    result.update(re.findall(r'<a\s+(?:name|id)=["\']([^"\']+)', text))
    return result


def main():
    paths = sorted((ROOT / "docs").rglob("*.md"))
    paths += [ROOT / name for name in (
        "README.md", "tests/README.md", "tools/README.md",
        "samples/README.md", "ref/README.md", "src/a3d/README.md",
        "third_party/README.md")]
    cache = {}
    failures = []
    count = 0
    for path in paths:
        text = prose(path.read_text(encoding="utf-8-sig"))
        for match in re.finditer(r"\[[^\]\n]+\]\((<[^>]+>|[^\s)]+)(?:\s+\"[^\"]*\")?\)", text):
            href = match[1].strip('<>')
            split = urlsplit(href)
            if split.scheme or split.netloc:
                continue
            count += 1
            target = (path.parent / unquote(split.path)).resolve() if split.path else path.resolve()
            reason = None
            if not target.exists():
                reason = "missing file"
            elif split.fragment and target.is_file():
                if target not in cache:
                    cache[target] = target.read_text(encoding="utf-8-sig")
                fragment = unquote(split.fragment)
                if target.suffix.lower() == '.md':
                    if fragment not in anchors(cache[target]):
                        reason = "missing heading"
                elif re.fullmatch(r"L\d+(?:-L\d+)?", fragment):
                    numbers = [int(n) for n in re.findall(r"\d+", fragment)]
                    if min(numbers) < 1 or max(numbers) > len(cache[target].splitlines()):
                        reason = "invalid source line"
            if reason:
                line = text.count('\n', 0, match.start()) + 1
                failures.append(f"{path.relative_to(ROOT)}:{line}: {reason}: {href}")
    print(f"Checked {count} local links in {len(paths)} Markdown files")
    for failure in failures:
        print(failure)
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
