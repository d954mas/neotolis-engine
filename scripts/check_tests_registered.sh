#!/usr/bin/env bash
# Gate: every tests/unit/test_*.c must be BUILT (present in the compile DB) and
# RUN (its target appears in a generated CTestTestfile add_test) — a file that
# is merely mentioned in tests/CMakeLists.txt, or compiled but never add_test'ed,
# silently never runs. Uses a configured build dir as the authority, not text
# matching. Pass a build dir with devapi groups ON (tidy-ci locally, the CI
# lint configure in CI) so devapi-gated tests are visible.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build/_cmake/tidy-ci}"
if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "ERROR: $BUILD_DIR/compile_commands.json not found — configure it first"
    echo "  (check.sh configures build/_cmake/tidy-ci automatically)"
    exit 1
fi

python scripts/check_tests_registered.py "$BUILD_DIR"
