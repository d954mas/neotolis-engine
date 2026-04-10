#!/usr/bin/env bash
# Atlas packing benchmark for autoresearch.
#
# Builds native-release, runs atlas packer with N sprites (no cache),
# parses stage timings, validates packing quality via used_area.
#
# Usage:
#   bash scripts/atlas/benchmark.sh [--sprites N] [--max-area N] [--max-pages N] [--no-build]
#
# Output (last line):
#   METRIC:<total_ms>
#
# Guard: --max-area N exits 1 if used_area exceeds N (packing degraded).
#        --max-pages N exits 1 if page count exceeds N.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# --- Defaults ---
SPRITES=10000
MAX_SIZE=4096
GLOB="assets/sprites/bigatlas/*.png"
ATLAS_NAME="bench"
MODE="poly"
MAX_AREA=""
MAX_PAGES=""
NO_BUILD=false
PRESET="native-release"
OUT_DIR="build/examples/atlas"
BUILDER="${OUT_DIR}/${PRESET}/build_atlas_packs.exe"
PACK_DIR="${OUT_DIR}"
RESULTS_FILE="${OUT_DIR}/bench-results.tsv"

# --- Parse args ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sprites)    SPRITES="$2"; shift 2 ;;
        --max-area)   MAX_AREA="$2"; shift 2 ;;
        --max-pages)  MAX_PAGES="$2"; shift 2 ;;
        --no-build)   NO_BUILD=true; shift ;;
        --max-size)   MAX_SIZE="$2"; shift 2 ;;
        --glob)       GLOB="$2"; shift 2 ;;
        --name)       ATLAS_NAME="$2"; shift 2 ;;
        --mode)       MODE="$2"; shift 2 ;;
        *)            echo "Unknown arg: $1"; exit 1 ;;
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
echo "=== Running atlas benchmark: ${SPRITES} sprites, max=${MAX_SIZE} ==="
OUTPUT=$("$BUILDER" "$PACK_DIR" "$MAX_SIZE" "$GLOB" "$ATLAS_NAME" "$MODE" "$SPRITES" 2>&1)
BUILD_EXIT=$?
if [[ $BUILD_EXIT -ne 0 ]]; then
    echo "ERROR: Builder exited with code $BUILD_EXIT"
    echo "$OUTPUT"
    exit 1
fi

# --- Step 4: Parse BENCH line ---
BENCH_LINE=$(echo "$OUTPUT" | grep "^INFO \[builder\] BENCH " | tail -1)
if [[ -z "$BENCH_LINE" ]]; then
    echo "ERROR: No BENCH line found in output"
    echo "$OUTPUT"
    exit 1
fi
# Extract values — portable (no grep -P)
extract_val() { echo "$1" | sed "s/.*$2=\([0-9.]*\).*/\1/"; }
ALPHA_TRIM=$(extract_val "$BENCH_LINE" "alpha_trim")
DEDUP=$(extract_val "$BENCH_LINE" "dedup")
GEOMETRY=$(extract_val "$BENCH_LINE" "geometry")
PACK=$(extract_val "$BENCH_LINE" "pack")
COMPOSE=$(extract_val "$BENCH_LINE" "compose")
DEBUG_PNG=$(extract_val "$BENCH_LINE" "debug_png")
SERIALIZE=$(extract_val "$BENCH_LINE" "serialize")
TOTAL=$(extract_val "$BENCH_LINE" "total")
PAGES=$(extract_val "$BENCH_LINE" "pages")
USED_AREA=$(extract_val "$BENCH_LINE" "used_area")
FRONTIER_AREA=$(extract_val "$BENCH_LINE" "frontier_area")
TRIM_AREA=$(extract_val "$BENCH_LINE" "trim_area")
POLY_AREA=$(extract_val "$BENCH_LINE" "poly_area")
POT_WASTE=$(extract_val "$BENCH_LINE" "pot_waste")
POLY_FRONTIER_FILL=$(extract_val "$BENCH_LINE" "fill_frontier")
POLY_TEXTURE_FILL=$(extract_val "$BENCH_LINE" "fill_texture")
OR_OPS=$(extract_val "$BENCH_LINE" "or_ops")
TEST_OPS=$(extract_val "$BENCH_LINE" "test_ops")
PAGE_SCANS=$(extract_val "$BENCH_LINE" "page_scans")
PAGE_EXISTING=$(extract_val "$BENCH_LINE" "page_existing")
PAGE_NEW=$(extract_val "$BENCH_LINE" "page_new")
NFP_CACHE_HITS=$(extract_val "$BENCH_LINE" "cache_hits")
NFP_CACHE_MISSES=$(extract_val "$BENCH_LINE" "cache_misses")

USED_AREA=${USED_AREA:-0}
FRONTIER_AREA=${FRONTIER_AREA:-0}
TRIM_AREA=${TRIM_AREA:-0}
POLY_AREA=${POLY_AREA:-0}
POT_WASTE=${POT_WASTE:-0}
POLY_FRONTIER_FILL=${POLY_FRONTIER_FILL:-0}
POLY_TEXTURE_FILL=${POLY_TEXTURE_FILL:-0}
OR_OPS=${OR_OPS:-0}
TEST_OPS=${TEST_OPS:-0}
PAGE_SCANS=${PAGE_SCANS:-0}
PAGE_EXISTING=${PAGE_EXISTING:-0}
PAGE_NEW=${PAGE_NEW:-0}
NFP_CACHE_HITS=${NFP_CACHE_HITS:-0}
NFP_CACHE_MISSES=${NFP_CACHE_MISSES:-0}

# Extract unique count
PACKED_LINE=$(echo "$OUTPUT" | grep "Atlas packed" | tail -1)
UNIQUE=$(echo "$PACKED_LINE" | sed 's/.*(\([0-9]*\) unique).*/\1/')

echo ""
echo "=== Benchmark Results ==="
echo "Mode: ${MODE}, Sprites: ${SPRITES} (${UNIQUE} unique), Pages: ${PAGES}"
echo "Area: alloc=${USED_AREA}px, frontier=${FRONTIER_AREA}px, trim=${TRIM_AREA}px, poly=${POLY_AREA}px, pot_waste=${POT_WASTE}px"
echo "Fill: poly/frontier=${POLY_FRONTIER_FILL}, poly/alloc=${POLY_TEXTURE_FILL}"
echo "Ops: OR=${OR_OPS}, Test=${TEST_OPS}"
echo "Pages: scans=${PAGE_SCANS}, existing=${PAGE_EXISTING}, new=${PAGE_NEW}"
echo "Cache: hits=${NFP_CACHE_HITS}, misses=${NFP_CACHE_MISSES}"
echo ""
printf "%-15s %10s\n" "Stage" "Time (ms)"
printf "%-15s %10s\n" "───────────────" "──────────"
printf "%-15s %10s\n" "alpha_trim" "$ALPHA_TRIM"
printf "%-15s %10s\n" "dedup" "$DEDUP"
printf "%-15s %10s\n" "geometry" "$GEOMETRY"
printf "%-15s %10s\n" "pack" "$PACK"
printf "%-15s %10s\n" "compose" "$COMPOSE"
printf "%-15s %10s\n" "debug_png" "$DEBUG_PNG"
printf "%-15s %10s\n" "serialize" "$SERIALIZE"
printf "%-15s %10s\n" "───────────────" "──────────"
printf "%-15s %10s\n" "TOTAL" "$TOTAL"
echo ""

# --- Step 5: Guard checks ---
GUARD_PASS=true

if [[ -n "$MAX_PAGES" ]]; then
    if [[ "$PAGES" -gt "$MAX_PAGES" ]]; then
        echo "GUARD: FAIL — pages=${PAGES} exceeds max=${MAX_PAGES}"
        GUARD_PASS=false
    else
        echo "GUARD: PASS — pages=${PAGES} <= ${MAX_PAGES}"
    fi
fi

if [[ -n "$MAX_AREA" ]]; then
    if [[ "$USED_AREA" -gt "$MAX_AREA" ]]; then
        echo "GUARD: FAIL — used_area=${USED_AREA} exceeds max=${MAX_AREA}"
        GUARD_PASS=false
    else
        echo "GUARD: PASS — used_area=${USED_AREA} <= ${MAX_AREA}"
    fi
fi

if [[ "$GUARD_PASS" == false ]]; then
    exit 1
fi

# --- Step 6: Append to results TSV ---
TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
if [[ ! -f "$RESULTS_FILE" ]]; then
    printf "timestamp\tcommit\tmode\tsprites\tunique\tpages\tused_area\tfrontier_area\ttrim_area\tpoly_area\tpot_waste\tpoly_frontier_fill\tpoly_texture_fill\tor_ops\ttest_ops\tpage_scans\tpage_existing\tpage_new\tnfp_cache_hits\tnfp_cache_misses\talpha_trim\tdedup\tgeometry\tpack\tcompose\tdebug_png\tserialize\ttotal\n" > "$RESULTS_FILE"
fi
printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$TIMESTAMP" "$COMMIT" "$MODE" "$SPRITES" "$UNIQUE" "$PAGES" "$USED_AREA" "$FRONTIER_AREA" "$TRIM_AREA" "$POLY_AREA" "$POT_WASTE" "$POLY_FRONTIER_FILL" "$POLY_TEXTURE_FILL" "$OR_OPS" "$TEST_OPS" \
    "$PAGE_SCANS" "$PAGE_EXISTING" "$PAGE_NEW" "$NFP_CACHE_HITS" "$NFP_CACHE_MISSES" \
    "$ALPHA_TRIM" "$DEDUP" "$GEOMETRY" "$PACK" "$COMPOSE" "$DEBUG_PNG" "$SERIALIZE" "$TOTAL" \
    >> "$RESULTS_FILE"

echo ""
echo "Results appended to: ${RESULTS_FILE}"

# --- Final output: metric for autoresearch ---
echo ""
echo "METRIC:${TOTAL}"
