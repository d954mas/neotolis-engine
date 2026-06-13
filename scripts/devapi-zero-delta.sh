#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# SC5 / PROTO-10 zero-delta gate. The PRIMARY structural guarantee is the
# engine/CMakeLists.txt subdir-exclusion at NT_DEVAPI_ENABLED=0 (D-10): a release
# build never compiles engine/devapi, so the dev surface is physically absent.
# This script is the explicit belt-and-suspenders assertion (D-11): it greps the
# release WASM for nt_devapi_* symbols and FAILS if any leak through. Per D-11
# there is NO build-twice / byte-identity baseline — the existing size-tracking CI
# already catches an accidental leak as a size jump.

PRESET="${PRESET:-wasm-release}"

usage() {
    echo "Usage: devapi-zero-delta.sh [target-name]"
    echo ""
    echo "Assert a release WASM build contains zero nt_devapi_* symbols (SC5/PROTO-10)."
    echo ""
    echo "Arguments:"
    echo "  target-name   Example target to inspect (default: hello)"
    echo ""
    echo "Environment:"
    echo "  PRESET        Build preset to inspect (default: wasm-release)"
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
    WASM_FILE=$(find "$OUTPUT_DIR" -maxdepth 1 -name '*.wasm' | head -1)
fi

if [ -z "$WASM_FILE" ] || [ ! -f "$WASM_FILE" ]; then
    echo "ERROR: No .wasm binary found in: $OUTPUT_DIR"
    echo "Build the target first:"
    echo "  emcmake cmake --preset $PRESET && cmake --build --preset $PRESET"
    exit 1
fi

# ---------------------------------------------------------------------------
# Assertion: zero nt_devapi_* symbols in the release WASM
# ---------------------------------------------------------------------------
echo "Zero-delta check: $WASM_FILE ($PRESET)"

if wasm-objdump -x "$WASM_FILE" | grep -q 'nt_devapi_'; then
    echo "ZERO-DELTA FAIL: nt_devapi_* symbols present in release wasm"
    echo "  A release build (NT_DEVAPI_ENABLED OFF) must not contain the dev surface."
    echo "  Offending symbols:"
    wasm-objdump -x "$WASM_FILE" | grep 'nt_devapi_' || true
    exit 1
fi

echo "ZERO-DELTA OK: no nt_devapi_* symbols in $TARGET ($PRESET)"
exit 0
