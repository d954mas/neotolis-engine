#!/usr/bin/env bash
# Gate: every relative link in the docs tree must resolve to an existing file
# (and, when present, an existing heading anchor). docs/** is in ci.yml's
# paths-ignore, so a docs-only PR runs no other CI — a broken spec link would
# otherwise ship unchecked. The .github/workflows/docs-link.yml workflow runs
# this on doc/md changes; scripts/check.sh --full runs it locally.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

python3 - <<'PY'
import os, re, sys

DOC_DIRS = ["docs"]
EXTRA_FILES = ["AGENTS.md", "README.md"]  # root docs that link into docs/
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")

def slugify(text):
    s = text.strip().lower()
    s = re.sub(r"[^\w\s-]", "", s)   # GitHub drops punctuation
    s = re.sub(r"\s+", "-", s)
    return s

_heading_cache = {}
def heading_slugs(path):
    if path not in _heading_cache:
        slugs = set()
        try:
            with open(path, encoding="utf-8") as fh:
                for line in fh:
                    m = re.match(r"#{1,6}\s+(.*)", line)
                    if m:
                        slugs.add(slugify(m.group(1)))
        except OSError:
            pass
        _heading_cache[path] = slugs
    return _heading_cache[path]

md_files = []
for d in DOC_DIRS:
    for dirpath, _, files in os.walk(d):
        md_files += [os.path.join(dirpath, f) for f in files if f.endswith(".md")]
md_files += [f for f in EXTRA_FILES if os.path.isfile(f)]

broken = []
for src in md_files:
    base = os.path.dirname(src)
    with open(src, encoding="utf-8") as fh:
        text = fh.read()
    for target in LINK_RE.findall(text):
        target = target.strip()
        if target.startswith(("http://", "https://", "mailto:", "#")):
            continue  # external or in-page-only
        path_part, _, anchor = target.partition("#")
        if not path_part:
            continue
        resolved = os.path.normpath(os.path.join(base, path_part))
        if not os.path.exists(resolved):
            broken.append((src, target, "missing file"))
        elif anchor and resolved.endswith(".md") and slugify(anchor) not in heading_slugs(resolved):
            broken.append((src, target, f"no heading anchor #{anchor}"))

if broken:
    print(f"check_doc_links: FAILED -- {len(broken)} broken link(s):")
    for src, target, why in broken:
        print(f"  {src} -> {target}  ({why})")
    sys.exit(1)
print(f"check_doc_links: ok ({len(md_files)} markdown files scanned, all relative links resolve)")
PY
