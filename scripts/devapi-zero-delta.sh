#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# SC5 / PROTO-10 zero-delta gate. The PRIMARY structural guarantee is the
# engine/CMakeLists.txt subdir-exclusion at NT_DEVAPI_ENABLED=0 (D-10): a release
# build never compiles engine/devapi, so the dev surface is physically absent.
# This script is the explicit belt-and-suspenders assertion (D-11): it greps the
# WASM for nt_devapi_* symbols and FAILS if any leak through. Per D-11 there is NO
# build-twice / byte-identity baseline — the size-tracking CI catches a leak as a jump.
#
# IMPORTANT: nt_devapi_* are INTERNAL symbols — their names live only in the WASM
# "name" custom section, which a fully-stripped release build discards. Grepping a
# stripped binary for nt_devapi_* therefore "passes" vacuously regardless of leakage.
# So this gate (a) runs against a names-retaining preset by default and (b) enforces a
# POSITIVE CONTROL: it first proves engine nt_* names are visible, and errors out if
# not — converting a silent false-pass into a loud, actionable failure.

PRESET="${PRESET:-wasm-analysis-paired}"

usage() {
    echo "Usage: devapi-zero-delta.sh [target-name]"
    echo ""
    echo "Assert a release WASM build contains zero nt_devapi_* symbols (SC5/PROTO-10)."
    echo ""
    echo "Arguments:"
    echo "  target-name   Example target to inspect (default: hello)"
    echo ""
    echo "Environment:"
    echo "  PRESET        Build preset to inspect (default: wasm-analysis-paired)."
    echo "                MUST retain symbol names — a stripped release preset makes the"
    echo "                check vacuous and is rejected by the positive control."
    exit 1
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    usage
fi

TARGET="${1:-hello}"

# ---------------------------------------------------------------------------
# Dependency check (mirror size-report.sh)
# ---------------------------------------------------------------------------
if ! command -v wasm-objdump &>/dev/null; then
    echo "ERROR: wasm-objdump not found. Install wabt:"
    echo "  sudo apt install wabt   (Linux)"
    echo "  brew install wabt       (macOS)"
    exit 1
fi

# ---------------------------------------------------------------------------
# Path resolution (mirror size-report.sh: OUTPUT_NAME may be index.wasm)
# ---------------------------------------------------------------------------
OUTPUT_DIR="$ROOT_DIR/build/examples/$TARGET/$PRESET"

WASM_FILE=""
if [ -d "$OUTPUT_DIR" ]; then
    # Deterministic pick (index.wasm sorts before index_simd.wasm) — both are the same
    # engine build, so either proves absence; sort avoids a nondeterministic head -1.
    WASM_FILE=$(find "$OUTPUT_DIR" -maxdepth 1 -name '*.wasm' | sort | head -1)
fi

if [ -z "$WASM_FILE" ] || [ ! -f "$WASM_FILE" ]; then
    echo "ERROR: No .wasm binary found in: $OUTPUT_DIR"
    echo "Build the target first:"
    echo "  emcmake cmake --preset $PRESET && cmake --build --preset $PRESET"
    exit 1
fi

# ---------------------------------------------------------------------------
# Assertion: zero nt_devapi_* symbols, with a positive control
# ---------------------------------------------------------------------------
echo "Zero-delta check: $WASM_FILE ($PRESET)"

OBJDUMP=$(wasm-objdump -x "$WASM_FILE")

# Patterns are anchored to a token boundary: engine symbols start `nt_` at the start of
# an identifier (objdump prints them like `<nt_app_init>`). A bare `nt_` substring would
# also hit benign tokens (font_, count_, component_, current_) and mis-pass the positive
# control / mis-fail the negative one — the boundary class avoids both.
NT_SYMBOL='(^|[^A-Za-z0-9_])nt_[a-z]'
NT_DEVAPI_SYMBOL='(^|[^A-Za-z0-9_])nt_devapi_'

# Positive control: this check only means something if internal symbol names survive in
# this binary. If NO engine nt_* function names are visible, the name section was stripped
# and a grep for nt_devapi_* is vacuous — fail loudly instead of false-passing.
if ! printf '%s\n' "$OBJDUMP" | grep -Eq "$NT_SYMBOL"; then
    echo "ERROR: no engine nt_* function names visible in $WASM_FILE — the name section is"
    echo "  stripped, so a grep for nt_devapi_* would pass vacuously. Inspect a"
    echo "  names-retaining preset instead (default: PRESET=wasm-analysis-paired)."
    exit 1
fi

if printf '%s\n' "$OBJDUMP" | grep -Eq "$NT_DEVAPI_SYMBOL"; then
    echo "ZERO-DELTA FAIL: nt_devapi_* symbols present in $PRESET wasm"
    echo "  A devapi-OFF build must not contain the dev surface. Offending symbols:"
    printf '%s\n' "$OBJDUMP" | grep -E "$NT_DEVAPI_SYMBOL" || true
    exit 1
fi

echo "ZERO-DELTA OK: engine nt_* present, zero nt_devapi_* in $TARGET ($PRESET)"
exit 0
