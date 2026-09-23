#!/usr/bin/env python3
"""Fail when the GL backend issues a GL call outside the NT_GL* funnel.

Every GL call in engine/graphics/gl must go through the macros in
nt_gfx_gl_calls.h, which count it, require an open tick and record it. A bare
`glFoo(` or `glad_glFoo(` call would escape all three.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GL_DIR = ROOT / "engine" / "graphics" / "gl"
FUNNEL = GL_DIR / "nt_gfx_gl_calls.h"
BARE_CALL = re.compile(r"\b(?:glad_)?gl[A-Z]\w*\s*\(")
FUNNEL_CALL = re.compile(r"\bNT_GL\w*\(")


def strip_comments_and_strings(text: str) -> str:
    """Blank comments and literals, keeping line numbers."""
    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
    return pattern.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def main() -> int:
    if not FUNNEL.is_file():
        print(f"check_gl_calls: missing funnel header {FUNNEL}", file=sys.stderr)
        return 1
    failures = []
    funnel_calls = 0
    for path in sorted(GL_DIR.glob("*.[ch]")):
        if path == FUNNEL:
            continue
        code = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        funnel_calls += len(FUNNEL_CALL.findall(code))
        for match in BARE_CALL.finditer(code):
            line = code.count("\n", 0, match.start()) + 1
            failures.append(f"{path.relative_to(ROOT).as_posix()}:{line}: bare GL call '{match.group(0).rstrip('( ')}' bypasses the NT_GL funnel")
    # Positive control: the scan must see the funnel in use, or it proves nothing.
    if funnel_calls == 0:
        print("check_gl_calls: no NT_GL* calls found; the scan is not looking at the backend", file=sys.stderr)
        return 1
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print(f"check_gl_calls: ok ({funnel_calls} funnel calls, no bare GL calls)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
