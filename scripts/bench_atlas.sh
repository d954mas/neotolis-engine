#!/usr/bin/env bash
# Builds atlas_bench and emits one metrics JSON per selected fixture corpus.
# Usage: bench_atlas.sh [--preset P] [--out DIR] [--corpus NAME] [--no-build]

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
# mixed_aa reuses bigatlas; the other corpora are dedicated LFS fixtures.
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

    # Remove stale cache data even though the current tool leaves caching disabled.
    rm -rf "${OUT_DIR}/_cache"

    out_json="${OUT_DIR}/${name}.json"
    echo "=== ${name}: ${#matches[@]} files, shape=${shape}, max_size=${max_size} ==="
    # The tool receives the glob as one argument and expands it internally.
    "$BENCH_EXE" "$out_json" "$glob" "$name" "$shape" "$max_size" "$max_sprites"
    echo "    -> ${out_json}"
    echo ""
    RAN=$((RAN + 1))

    # External packer oracles may write sibling JSON files here.
done

if [[ "$RAN" -eq 0 ]]; then
    echo "ERROR: no corpus ran (bad --corpus '${ONLY_CORPUS}'?)" >&2
    exit 1
fi

echo "=== Done: ${RAN} corpus JSON(s) under ${OUT_DIR}/ ==="
