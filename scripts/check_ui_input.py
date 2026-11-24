#!/usr/bin/env python3
"""Fail when an engine/ui widget reads raw input instead of the nt_ui router.

The router (nt_ui.c) fixes the frame's pointer, gesture and keyboard owners in
nt_ui_begin; widgets read keys and chars through nt_ui_key_pressed,
nt_ui_key_down and nt_ui_pop_char with their own id, and the pointer through
nt_ui_step and the router queries. A raw nt_input key/char read, g_nt_input or
ctx->frame_pointers in a widget would bypass the owner rules
(docs/spec/ui/nt-ui.md, "Input ownership"). nt_ui_scroll.c is the router's
gesture core and reads the pointer copy itself.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI_DIR = ROOT / "engine" / "ui"
ROUTER = {UI_DIR / "nt_ui.c", UI_DIR / "nt_ui_scroll.c"}
RAW_READ = re.compile(r"\bnt_input_key_is_(?:pressed|down)\s*\(|\bnt_input_pop_char\s*\(|\bg_nt_input\b|\bframe_pointers\b")
ROUTER_READ = re.compile(r"\bnt_ui_(?:key_pressed|key_down|pop_char)\s*\(")


def strip_comments_and_strings(text: str) -> str:
    """Blank comments and literals, keeping line numbers."""
    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
    return pattern.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def main() -> int:
    failures = []
    router_reads = 0
    for path in sorted(UI_DIR.glob("*.c")):
        if path in ROUTER:
            continue
        code = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        router_reads += len(ROUTER_READ.findall(code))
        for match in RAW_READ.finditer(code):
            line = code.count("\n", 0, match.start()) + 1
            failures.append(f"{path.relative_to(ROOT).as_posix()}:{line}: raw input read '{match.group(0).rstrip('( ')}' bypasses the nt_ui router")
    # Positive control: the scan must see widgets reading through the router, or it proves nothing.
    if router_reads == 0:
        print("check_ui_input: no nt_ui_key_*/nt_ui_pop_char reads found; the scan is not looking at the widgets", file=sys.stderr)
        return 1
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print(f"check_ui_input: ok ({router_reads} router reads, no raw input reads)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
