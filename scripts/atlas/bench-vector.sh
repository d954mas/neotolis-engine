#!/usr/bin/env bash
# Quick vector pack benchmark: build + run 3 times, report median pack time,
# texture fill, and candidate count. Reads the BENCH line emitted by
# nt_builder_atlas.c at end-of-pack (see PackStats / pipeline_tile_pack logs).
# Usage: bash scripts/atlas/bench-vector.sh [sprites=1000] [--no-build]
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
declare -a PACKS AREAS FILLS TESTS

for i in $(seq 1 $RUNS); do
    rm -rf "${OUT_DIR}/_cache"
    # build_atlas_packs args: <pack_dir> [max_size] [glob] [name] [r=rect] [max_sprites]
    LINE=$("$BUILDER" "$OUT_DIR" 4096 "assets/sprites/bigatlas/*.png" bench v "$SPRITES" 2>&1 | grep "^INFO \[builder\] BENCH " | tail -1)
    # Field names come from nt_builder_atlas.c BENCH log:
    #   pack=<ms>             total ms in pipeline_tile_pack (vector_pack call)
    #   used_area=<px>        final page area post-POT
    #   fill_texture=<0..1>   used_area / (page_w * page_h)
    #   test_ops=<count>      point-in-NFP tests during candidate scoring
    P=$(echo "$LINE" | sed 's/.*[[:space:]]pack=\([0-9.]*\).*/\1/')
    A=$(echo "$LINE" | sed 's/.*used_area=\([0-9]*\).*/\1/')
    F=$(echo "$LINE" | sed 's/.*fill_texture=\([0-9.]*\).*/\1/')
    T=$(echo "$LINE" | sed 's/.*test_ops=\([0-9]*\).*/\1/')
    PACKS+=("$P")
    AREAS+=("$A")
    FILLS+=("$F")
    TESTS+=("$T")
    printf "  run %d: pack=%-8s used_area=%-10s fill_texture=%-8s test_ops=%s\n" "$i" "$P" "$A" "$F" "$T"
done

# Sort and pick median (middle of 3)
SORTED_P=($(printf '%s\n' "${PACKS[@]}" | sort -n))
SORTED_A=($(printf '%s\n' "${AREAS[@]}" | sort -n))
SORTED_F=($(printf '%s\n' "${FILLS[@]}" | sort -n))
SORTED_T=($(printf '%s\n' "${TESTS[@]}" | sort -n))
MID=$((RUNS / 2))

echo ""
echo "=== Median (${RUNS} runs, ${SPRITES} sprites) ==="
echo "  pack:         ${SORTED_P[$MID]} ms"
echo "  used_area:    ${SORTED_A[$MID]}"
echo "  fill_texture: ${SORTED_F[$MID]}"
echo "  test_ops:     ${SORTED_T[$MID]}"
