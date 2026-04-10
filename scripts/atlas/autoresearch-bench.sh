#!/usr/bin/env bash
# Autoresearch benchmark for atlas vector packing optimization.
#
# Builds native-release, runs bigatlas packing, verifies bit-for-bit
# identical output, reports pack time as METRIC.
#
# Usage: bash scripts/atlas/autoresearch-bench.sh [--no-build]
#
# Exit codes:
#   0 — success, METRIC line printed
#   1 — build failure, verification failure, or runtime error
#
# The reference hash is computed from the CURRENT code at baseline.
# Any change that alters the .ntpack output (even 1 bit) will fail.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

NO_BUILD=false
if [[ "${1:-}" == "--no-build" ]]; then NO_BUILD=true; fi

BUILDER="build/examples/atlas/native-release/build_atlas_packs.exe"
OUT_DIR="build/examples/atlas"
REFERENCE_HASH="849ff8f3ef95c6ae0f0885aa00a15bf2b3819fba3295e080792d365466f30584"
NTPACK="${OUT_DIR}/atlas_demo.ntpack"

# --- Step 1: Build ---
if [[ "$NO_BUILD" == false ]]; then
    echo "=== Building native-release ==="
    if ! cmake --build build/_cmake/native-release --target build_atlas_packs 2>&1 | tail -3; then
        echo "FATAL: build failed"
        exit 1
    fi
fi

# --- Step 2: Run bigatlas (no cache, full pack) ---
echo "=== Running bigatlas (4812 sprites, 4096px) ==="
rm -rf "${OUT_DIR}/_cache"
OUTPUT=$("$BUILDER" "$OUT_DIR" 4096 "assets/sprites/bigatlas/*.png" bigatlas v 0 2>&1)
LINE=$(echo "$OUTPUT" | grep "^INFO \[builder\] BENCH " | tail -1)

if [[ -z "$LINE" ]]; then
    echo "FATAL: no BENCH line in output"
    echo "$OUTPUT"
    exit 1
fi

# Extract pack time (ms)
PACK_MS=$(echo "$LINE" | sed 's/.*[[:space:]]pack=\([0-9.]*\).*/\1/')
TOTAL_MS=$(echo "$LINE" | sed 's/.*[[:space:]]total=\([0-9.]*\).*/\1/')
FILL=$(echo "$LINE" | sed 's/.*fill_texture=\([0-9.]*\).*/\1/')
TEST_OPS=$(echo "$LINE" | sed 's/.*test_ops=\([0-9]*\).*/\1/')

echo "  pack=${PACK_MS}ms total=${TOTAL_MS}ms fill=${FILL} test_ops=${TEST_OPS}"

# --- Step 3: Verify bit-for-bit identical output ---
echo "=== Verifying bit-for-bit identical output ==="
ACTUAL_HASH=$(sha256sum "$NTPACK" | awk '{print $1}')

if [[ "$ACTUAL_HASH" != "$REFERENCE_HASH" ]]; then
    echo "FATAL: output differs from reference!"
    echo "  expected: ${REFERENCE_HASH}"
    echo "  actual:   ${ACTUAL_HASH}"
    echo "VERIFY:FAIL"
    exit 1
fi
echo "  hash: ${ACTUAL_HASH} ✓"
echo "VERIFY:PASS"

# --- Step 4: Report metric ---
echo "METRIC:${PACK_MS}"
