#!/usr/bin/env bash
# Gate (D-01): omitting a swappable impl must be a LOUD unresolved-symbol link
# error, never a silent no-op. Build a throwaway TU that calls nt_log_write but
# links NO nt_log impl (interface only -- header, no symbols), and assert the
# linker reports an undefined reference to nt_log_write.
#
# This script EXPECTS the link to fail: exit 0 when the expected error string is
# observed, exit 1 otherwise. It must never be registered as a normal ctest target.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

CC="${CC:-clang}"
if ! command -v "$CC" > /dev/null 2>&1; then
    if command -v cc > /dev/null 2>&1; then CC=cc; else
        echo "check_link_failure_loud: no C compiler found (set CC)"; exit 1
    fi
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

SRC="$TMP/no_impl.c"
OUT="$TMP/no_impl_exe"
cat > "$SRC" <<'EOF'
#include "log/nt_log.h"
int main(void) {
    /* References nt_log_write -- resolved only by a linked nt_log impl. */
    nt_log_write(NT_LOG_LEVEL_INFO, "loud_link_check", "must not link without an impl");
    return 0;
}
EOF

# Compile + link with ONLY the engine include root. No nt_log / nt_log_stub object
# or archive on the link line -> the linker cannot resolve nt_log_write.
LINK_LOG="$TMP/link.log"
set +e
"$CC" -I "$ROOT_DIR/engine" "$SRC" -o "$OUT" > "$LINK_LOG" 2>&1
RC=$?
set -e 2>/dev/null || true

if [ "$RC" -eq 0 ]; then
    echo "check_link_failure_loud: FAILED -- impl-less target linked successfully (D-01 broken)"
    exit 1
fi

# The failure must be the EXPECTED unresolved nt_log_write, not an unrelated error.
# Match locale-independent markers too: MSVC link.exe emits LNK2019 (unresolved
# external symbol) with a localized message, and lld/ld use undefined/unresolved.
if grep -Eqi 'nt_log_write' "$LINK_LOG" \
    && grep -Eqi 'undefined|unresolved|cannot find|LNK2019|LNK1120' "$LINK_LOG"; then
    echo "check_link_failure_loud: passed (omitting nt_log impl is a loud unresolved-symbol error for nt_log_write)"
    exit 0
fi

echo "check_link_failure_loud: FAILED -- link failed, but not with the expected nt_log_write unresolved-symbol error:"
cat "$LINK_LOG"
exit 1
