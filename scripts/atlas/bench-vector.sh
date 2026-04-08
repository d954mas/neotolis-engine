#!/usr/bin/env bash
# Quick vector pack benchmark: build + run 3 times, report median tile_pack and used_area.
# Usage: bash scripts/bench-vector.sh [sprites=1000] [--no-build]
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

SPRITES="${1:-1000}"
NO_BUILD=false
if [[ "${2:-}" == "--no-build" ]]; then NO_BUILD=true; fi

BUILDER="build/examples/atlas/native-release/build_atlas_packs.exe"
OUT_DIR="build/examples/atlas"

if [[ "$NO_BUILD" == false ]]; then
    echo "=== Building ==="
    cmake --build build/_cmake/native-release --target build_atlas_packs 2>&1 | tail -3
fi

RUNS=3
declare -a TIMES AREAS FILLS CANDS

for i in $(seq 1 $RUNS); do
    rm -rf "${OUT_DIR}/_cache"
    LINE=$("$BUILDER" "$OUT_DIR" 4096 2 "assets/sprites/bigatlas/*.png" bench v "$SPRITES" 2>&1 | grep "^INFO \[builder\] BENCH " | tail -1)
    T=$(echo "$LINE" | sed 's/.*tile_pack=\([0-9.]*\).*/\1/')
    A=$(echo "$LINE" | sed 's/.*used_area=\([0-9]*\).*/\1/')
    F=$(echo "$LINE" | sed 's/.*poly_texture_fill=\([0-9.]*\).*/\1/')
    C=$(echo "$LINE" | sed 's/.*candidates=\([0-9]*\).*/\1/')
    TIMES+=("$T")
    AREAS+=("$A")
    FILLS+=("$F")
    CANDS+=("$C")
    printf "  run %d: tile_pack=%-8s used_area=%-10s fill=%-8s candidates=%s\n" "$i" "$T" "$A" "$F" "$C"
done

# Sort and pick median (middle of 3)
SORTED_T=($(printf '%s\n' "${TIMES[@]}" | sort -n))
SORTED_A=($(printf '%s\n' "${AREAS[@]}" | sort -n))
SORTED_F=($(printf '%s\n' "${FILLS[@]}" | sort -n))
SORTED_C=($(printf '%s\n' "${CANDS[@]}" | sort -n))
MID=$((RUNS / 2))

echo ""
echo "=== Median (${RUNS} runs, ${SPRITES} sprites) ==="
echo "  tile_pack:  ${SORTED_T[$MID]} ms"
echo "  used_area:  ${SORTED_A[$MID]}"
echo "  fill:       ${SORTED_F[$MID]}"
echo "  candidates: ${SORTED_C[$MID]}"
