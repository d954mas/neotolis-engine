#!/usr/bin/env bash
# Formats (IN PLACE) every changed .c/.h outside vendored deps/ and generated/.
# The mutating counterpart to check.sh's read-only format gate.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=lib/changed_files.sh
source "$SCRIPT_DIR/lib/changed_files.sh"

FILES="$(compute_changed_files | grep -E '\.(c|h)$' | grep -v 'deps/\|/generated/' || true)"
if [ -z "$FILES" ]; then
    echo "fmt: nothing to format"
    exit 0
fi
# shellcheck disable=SC2086 — newline-split into file args; repo paths have no spaces
clang-format -i $FILES
echo "fmt: formatted"
printf '  %s\n' $FILES
