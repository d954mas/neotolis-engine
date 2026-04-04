#!/usr/bin/env bash
# Atlas packing benchmark for autoresearch.
#
# Builds native-release, runs atlas packer with N sprites (no cache),
# parses stage timings, validates output hash.
#
# Usage:
#   bash scripts/atlas-benchmark.sh [--sprites N] [--baseline-hash HASH] [--verify-only] [--no-build]
#
# Output (last line):
#   METRIC:<total_ms>
#
# If --baseline-hash is set, validates output PNG hash matches. Exits 1 on mismatch.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# --- Defaults ---
SPRITES=1288
MAX_SIZE=4096
TILE_SIZE=2
GLOB="assets/sprites/bigatlas/*.png"
ATLAS_NAME="bench"
MODE="poly"
BASELINE_HASH=""
VERIFY_ONLY=false
NO_BUILD=false
PRESET="native-release"
OUT_DIR="build/examples/atlas"
BUILDER="${OUT_DIR}/${PRESET}/build_atlas_packs.exe"
PACK_DIR="${OUT_DIR}"
RESULTS_FILE="${OUT_DIR}/bench-results.tsv"

# --- Parse args ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sprites)      SPRITES="$2"; shift 2 ;;
        --baseline-hash) BASELINE_HASH="$2"; shift 2 ;;
        --verify-only)  VERIFY_ONLY=true; shift ;;
        --no-build)     NO_BUILD=true; shift ;;
        --max-size)     MAX_SIZE="$2"; shift 2 ;;
        --tile-size)    TILE_SIZE="$2"; shift 2 ;;
        *)              echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# --- Step 1: Build ---
if [[ "$NO_BUILD" == false ]]; then
    echo "=== Building ${PRESET} ==="
    cmake --build "build/_cmake/${PRESET}" --target build_atlas_packs 2>&1 | tail -3
    echo ""
fi

if [[ ! -x "$BUILDER" ]]; then
    echo "ERROR: Builder not found: $BUILDER"
    exit 1
fi

# --- Step 2: Clear cache ---
echo "=== Clearing cache ==="
rm -rf "${OUT_DIR}/_cache"

# --- Step 3: Run benchmark ---
echo "=== Running atlas benchmark: ${SPRITES} sprites, max=${MAX_SIZE}, ts=${TILE_SIZE} ==="
OUTPUT=$("$BUILDER" "$PACK_DIR" "$MAX_SIZE" "$TILE_SIZE" "$GLOB" "$ATLAS_NAME" "$MODE" "$SPRITES" 2>&1)

# --- Step 4: Parse BENCH line ---
BENCH_LINE=$(echo "$OUTPUT" | grep "^INFO \[builder\] BENCH " | tail -1)
if [[ -z "$BENCH_LINE" ]]; then
    echo "ERROR: No BENCH line found in output"
    echo "$OUTPUT"
    exit 1
fi

# Extract stage values (ms) — portable (no grep -P)
extract_val() { echo "$1" | sed "s/.*$2=\([0-9.]*\).*/\1/"; }
ALPHA_TRIM=$(extract_val "$BENCH_LINE" "alpha_trim")
DEDUP=$(extract_val "$BENCH_LINE" "dedup")
GEOMETRY=$(extract_val "$BENCH_LINE" "geometry")
TILE_PACK=$(extract_val "$BENCH_LINE" "tile_pack")
COMPOSE=$(extract_val "$BENCH_LINE" "compose")
SERIALIZE=$(extract_val "$BENCH_LINE" "serialize")
TOTAL=$(extract_val "$BENCH_LINE" "total")

# Extract stats from Atlas packed line
PACKED_LINE=$(echo "$OUTPUT" | grep "Atlas packed" | tail -1)
UNIQUE=$(echo "$PACKED_LINE" | sed 's/.*(\([0-9]*\) unique).*/\1/')
PAGES=$(echo "$PACKED_LINE" | sed 's/.* \([0-9]*\) pages.*/\1/')

echo ""
echo "=== Benchmark Results ==="
echo "Sprites: ${SPRITES} (${UNIQUE} unique), Pages: ${PAGES}"
echo ""
printf "%-15s %10s\n" "Stage" "Time (ms)"
printf "%-15s %10s\n" "───────────────" "──────────"
printf "%-15s %10s\n" "alpha_trim" "$ALPHA_TRIM"
printf "%-15s %10s\n" "dedup" "$DEDUP"
printf "%-15s %10s\n" "geometry" "$GEOMETRY"
printf "%-15s %10s\n" "tile_pack" "$TILE_PACK"
printf "%-15s %10s\n" "compose" "$COMPOSE"
printf "%-15s %10s\n" "serialize" "$SERIALIZE"
printf "%-15s %10s\n" "───────────────" "──────────"
printf "%-15s %10s\n" "TOTAL" "$TOTAL"
echo ""

# --- Step 5: Compute output hash ---
DEBUG_PNG="${PACK_DIR}/${ATLAS_NAME}_page0.png"
if [[ -f "$DEBUG_PNG" ]]; then
    HASH=$(sha256sum "$DEBUG_PNG" | awk '{print $1}')
    echo "Output hash: ${HASH}"
else
    echo "WARNING: Debug PNG not found: $DEBUG_PNG"
    HASH="MISSING"
fi

# --- Step 6: Validate hash (guard) ---
if [[ -n "$BASELINE_HASH" ]]; then
    if [[ "$HASH" == "$BASELINE_HASH" ]]; then
        echo "GUARD: PASS (hash matches baseline)"
    else
        echo "GUARD: FAIL (hash mismatch!)"
        echo "  Expected: $BASELINE_HASH"
        echo "  Got:      $HASH"
        exit 1
    fi
fi

# --- Step 7: Append to results TSV ---
TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
if [[ ! -f "$RESULTS_FILE" ]]; then
    printf "timestamp\tcommit\tsprites\tunique\tpages\talpha_trim\tdedup\tgeometry\ttile_pack\tcompose\tserialize\ttotal\thash\n" > "$RESULTS_FILE"
fi
printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$TIMESTAMP" "$COMMIT" "$SPRITES" "$UNIQUE" "$PAGES" \
    "$ALPHA_TRIM" "$DEDUP" "$GEOMETRY" "$TILE_PACK" "$COMPOSE" "$SERIALIZE" "$TOTAL" "$HASH" \
    >> "$RESULTS_FILE"

echo ""
echo "Results appended to: ${RESULTS_FILE}"

# --- Final output: metric for autoresearch ---
echo ""
echo "METRIC:${TOTAL}"
