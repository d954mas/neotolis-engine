#!/usr/bin/env bash
# Verify atlas builder is deterministic — same inputs produce bit-identical
# blob and texture PNG across rebuilds.
#
# Usage: bash scripts/check_atlas_deterministic.sh [sprite_count]
#   sprite_count: max sprites to pack (default 2000, full=0)
#
# Returns exit 0 if both runs produce identical output, exit 1 if not.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

SPRITE_COUNT="${1:-2000}"
BUILDER="build/examples/atlas/native-release/build_atlas_packs.exe"
PACK_DIR="build/examples/atlas"
BLOB="$PACK_DIR/atlas_demo.ntpack"
PAGE0="$PACK_DIR/bigatlas_page0.png"

if [[ ! -x "$BUILDER" ]]; then
    echo "ERROR: builder not found at $BUILDER"
    echo "Run: cmake --build build/_cmake/native-release"
    exit 2
fi

run_build() {
    rm -rf "$PACK_DIR/_cache" 2>/dev/null || true
    "$BUILDER" "$PACK_DIR" 4096 2 "assets/sprites/bigatlas/*.png" bigatlas v "$SPRITE_COUNT" \
        > /dev/null 2>&1
}

echo "=== Run 1 (sprites=$SPRITE_COUNT) ==="
run_build
HASH1_BLOB=$(md5sum "$BLOB" | awk '{print $1}')
HASH1_PNG=$(md5sum "$PAGE0" | awk '{print $1}')
echo "  blob md5: $HASH1_BLOB"
echo "  png  md5: $HASH1_PNG"

echo "=== Run 2 (sprites=$SPRITE_COUNT) ==="
run_build
HASH2_BLOB=$(md5sum "$BLOB" | awk '{print $1}')
HASH2_PNG=$(md5sum "$PAGE0" | awk '{print $1}')
echo "  blob md5: $HASH2_BLOB"
echo "  png  md5: $HASH2_PNG"

if [[ "$HASH1_BLOB" == "$HASH2_BLOB" && "$HASH1_PNG" == "$HASH2_PNG" ]]; then
    echo ""
    echo "PASS ✅ atlas builder is deterministic"
    exit 0
fi

echo ""
echo "FAIL ❌ atlas builder is NON-deterministic"
[[ "$HASH1_BLOB" != "$HASH2_BLOB" ]] && echo "  blob differs: $HASH1_BLOB vs $HASH2_BLOB"
[[ "$HASH1_PNG" != "$HASH2_PNG" ]] && echo "  png  differs: $HASH1_PNG vs $HASH2_PNG"
exit 1
