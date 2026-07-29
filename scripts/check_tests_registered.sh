#!/usr/bin/env bash
# Gate: every tests/unit/test_*.c must be referenced in tests/CMakeLists.txt —
# an unreferenced test file compiles nowhere and silently never runs.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/.." && pwd)"

MISSING=""
for f in tests/unit/test_*.c; do
    base="${f##*/}"
    stem="${base%.c}"
    # Registered either by explicit source path or by target name in a foreach
    # list (the UI block derives unit/${name}.c from the bare name).
    if ! grep -qE "unit/$base|\b$stem\b" tests/CMakeLists.txt; then
        MISSING="$MISSING  $f"$'\n'
    fi
done

if [ -n "$MISSING" ]; then
    echo "ERROR: test source(s) not referenced by tests/CMakeLists.txt (never built, never run):"
    printf '%s' "$MISSING"
    echo "check_tests_registered: FAILED"
    exit 1
fi
echo "check_tests_registered: passed (every tests/unit/test_*.c is registered)"
