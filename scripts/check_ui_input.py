#!/usr/bin/env python3
"""Fail when an engine/ui widget reads raw input instead of the nt_ui router.

The router (nt_ui.c) fixes the frame's pointer, gesture and keyboard owners in
nt_ui_begin; widgets read keys and chars through nt_ui_key_pressed,
nt_ui_key_down and nt_ui_pop_char with their own id, and the pointer through
nt_ui_step and the router queries. A raw nt_input key/char read, g_nt_input or
ctx->frame_pointers in a widget would bypass the owner rules
(docs/spec/ui/nt-ui.md, "Input ownership"); so would a widget reading the
router's own pointer, capture, key-owner or inert state. nt_ui_scroll.c is the
router's gesture core; popup-core marks the inert subtree.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI_DIR = ROOT / "engine" / "ui"
# The router and the headers that define its state; popup-core marks the inert subtree.
ROUTER = {UI_DIR / "nt_ui.c", UI_DIR / "nt_ui_scroll.c", UI_DIR / "nt_ui_internal.h"}
RAW_READ = re.compile(r"\bnt_input_(?:key|any_key|mouse|pop_char)\w*\s*\(|\bg_nt_input\b|\bframe_pointers\b|\bpointer_hot\b|->captures\b|\bkeys_owner\b|\boverlay_top\b")
INERT = re.compile(r"\binert_from\b")
INERT_OWNERS = ROUTER | {UI_DIR / "nt_ui_popup.c"}


def strip_comments_and_strings(text: str) -> str:
    """Blank comments and literals, keeping line numbers."""
    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
    return pattern.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def main() -> int:
    paths = sorted([*UI_DIR.glob("*.c"), *UI_DIR.glob("*.h")])
    if not paths:
        print("check_ui_input: no engine/ui sources; the scan is not looking at the widgets", file=sys.stderr)
        return 1
    failures = []
    for path in paths:
        code = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        matches = [] if path in ROUTER else list(RAW_READ.finditer(code))
        matches += [] if path in INERT_OWNERS else list(INERT.finditer(code))
        for match in matches:
            line = code.count("\n", 0, match.start()) + 1
            failures.append(f"{path.relative_to(ROOT).as_posix()}:{line}: raw input read '{match.group(0).rstrip('( ')}' bypasses the nt_ui router")
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print("check_ui_input: ok (no raw input reads)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
