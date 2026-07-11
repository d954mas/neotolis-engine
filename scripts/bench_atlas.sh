#!/usr/bin/env bash
# Atlas packer benchmark orchestrator (AUDIT-01).
#
# One command: build atlas_bench, then for each of the four fixture corpora
# wipe the atlas cache, assert the corpus glob is non-empty, and run the tool —
# emitting one machine-readable JSON per corpus (density / pages / pack_ms /
# hull-vertex). The committed baseline (tools/research/atlas_bench/baseline/)
# is produced from this script; Phase 83 re-runs it and diffs the numbers.
#
# Metrics come from the tool's JSON (it parses the produced .ntpack, D-02) — we
# deliberately do NOT scrape the builder's BENCH log line (it drifts).
#
# Usage:
#   bash scripts/bench_atlas.sh [--preset P] [--out DIR] [--corpus NAME] [--no-build]
#     --preset P     CMake preset dir under build/_cmake/ (default native-release)
#     --out DIR      output dir for the per-corpus JSON (default build/bench)
#     --corpus NAME  run a single corpus (anim_heavy|mixed_aa|rect_only|slice9)
#     --no-build     skip the cmake build step (use the existing exe)

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# --- Defaults ---
PRESET="native-release"
OUT_DIR="build/bench"
ONLY_CORPUS=""
NO_BUILD=false

# --- Parse args ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset)  PRESET="$2"; shift 2 ;;
        --out)     OUT_DIR="$2"; shift 2 ;;
        --corpus)  ONLY_CORPUS="$2"; shift 2 ;;
        --no-build) NO_BUILD=true; shift ;;
        *) echo "Unknown arg: $1" >&2; exit 1 ;;
    esac
done

# --- Corpus matrix: name|glob|shape|max_size|max_sprites (0 = all) ---
# mixed_aa reuses the existing bigatlas set (D-06); the other three are the
# committed LFS corpora under assets/bench/.
CORPORA=(
    "anim_heavy|assets/bench/anim_heavy/*.png|concave|2048|0"
    "mixed_aa|assets/sprites/bigatlas/*.png|concave|4096|0"
    "rect_only|assets/bench/rect_only/*.png|rect|2048|0"
    "slice9|assets/bench/slice9/*.png|concave|2048|0"
)

# --- Resolve the built exe (.exe on Windows) ---
BENCH_BASE="build/tools/research/${PRESET}/atlas_bench"
resolve_exe() {
    if [[ -x "${BENCH_BASE}.exe" ]]; then echo "${BENCH_BASE}.exe";
    elif [[ -x "${BENCH_BASE}" ]]; then echo "${BENCH_BASE}";
    else echo ""; fi
}

# --- Step 1: Build ---
if [[ "$NO_BUILD" == false ]]; then
    case "$PRESET" in
        native-*) ;;
        *) echo "ERROR: atlas_bench requires a native preset, got '${PRESET}'." >&2; exit 1 ;;
    esac
    if [[ ! -f "build/_cmake/${PRESET}/CMakeCache.txt" ]]; then
        cmake --preset "$PRESET"
    fi
    echo "=== Building atlas_bench (${PRESET}) ==="
    cmake --build "build/_cmake/${PRESET}" --target atlas_bench 2>&1 | tail -3
    echo ""
fi

BENCH_EXE="$(resolve_exe)"
if [[ -z "$BENCH_EXE" ]]; then
    echo "ERROR: atlas_bench exe not found under build/tools/research/${PRESET}/ (build it, or drop --no-build)" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# --- Step 2: Run each corpus ---
RAN=0
for spec in "${CORPORA[@]}"; do
    IFS='|' read -r name glob shape max_size max_sprites <<< "$spec"
    if [[ -n "$ONLY_CORPUS" && "$ONLY_CORPUS" != "$name" ]]; then
        continue
    fi

    shopt -s nullglob
    matches=( $glob )
    shopt -u nullglob
    if [[ ${#matches[@]} -eq 0 ]]; then
        echo "ERROR: corpus '${name}' glob '${glob}' matched 0 files." >&2
        echo "       If these are LFS-tracked corpora, run: git lfs pull" >&2
        exit 1
    fi
    for match in "${matches[@]}"; do
        if IFS= read -r first_line < "$match" && [[ "$first_line" == "version https://git-lfs.github.com/spec/v1" ]]; then
            echo "ERROR: corpus '${name}' contains an unsmudged Git LFS pointer: ${match}" >&2
            echo "       Run: git lfs pull" >&2
            exit 1
        fi
    done

    # Cache-wipe (Pitfall 2) — a warm atlas cache short-circuits packing and
    # zeroes pack_ms. Wipe before every run so timing is real.
    rm -rf "${OUT_DIR}/_cache"

    out_json="${OUT_DIR}/${name}.json"
    echo "=== ${name}: ${#matches[@]} files, shape=${shape}, max_size=${max_size} ==="
    # argv-array invocation (T-78-09): the glob is passed as ONE quoted argument;
    # the tool globs internally, so odd filenames can't inject shell tokens.
    "$BENCH_EXE" "$out_json" "$glob" "$name" "$shape" "$max_size" "$max_sprites"
    echo "    -> ${out_json}"
    echo ""
    RAN=$((RAN + 1))

    # --- Extension point (plan 78-07) ---
    # External MaxRects/TexturePacker oracles wrap here as argv-array
    # subprocesses over the SAME ${glob}, writing a sibling
    # ${OUT_DIR}/${name}.<oracle>.json. Do NOT invoke any oracle from this
    # script — it is the nt_builder-side harness only (D-10/D-11).
done

if [[ "$RAN" -eq 0 ]]; then
    echo "ERROR: no corpus ran (bad --corpus '${ONLY_CORPUS}'?)" >&2
    exit 1
fi

echo "=== Done: ${RAN} corpus JSON(s) under ${OUT_DIR}/ ==="
