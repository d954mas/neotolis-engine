#!/usr/bin/env bash
# Measures the emitted hull frontier without replacing the default baselines.
# Usage: bench_hull_tolerance.sh [--preset P] [--corpus NAME] [--out DIR] [--samples CSV]

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

PRESET="native-release"
CORPUS="mixed_aa"
OUT_DIR="build/bench/hull-tolerance"
SAMPLE_CSV="0,0.5,1.0,1.25,1.5,1.75,2.0,3.0"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset) PRESET="$2"; shift 2 ;;
        --corpus) CORPUS="$2"; shift 2 ;;
        --out) OUT_DIR="$2"; shift 2 ;;
        --samples) SAMPLE_CSV="$2"; shift 2 ;;
        *) echo "Unknown arg: $1" >&2; exit 1 ;;
    esac
done

case "$PRESET" in
    native-*) ;;
    *) echo "ERROR: atlas_bench requires a native preset, got '${PRESET}'." >&2; exit 1 ;;
esac

IFS=',' read -r -a SAMPLES <<< "$SAMPLE_CSV"
if [[ ${#SAMPLES[@]} -eq 0 ]]; then
    echo "ERROR: --samples must contain at least one tolerance." >&2
    exit 1
fi
for sample in "${SAMPLES[@]}"; do
    if [[ ! "$sample" =~ ^([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?$ ]]; then
        echo "ERROR: invalid non-negative finite tolerance '${sample}'." >&2
        exit 1
    fi
done

CORPORA=(
    "anim_heavy|assets/bench/anim_heavy/*.png|concave|2048|0"
    "mixed_aa|assets/sprites/bigatlas/*.png|concave|4096|0"
    "rect_only|assets/bench/rect_only/*.png|rect|2048|0"
    "slice9|assets/bench/slice9/*.png|concave|2048|0"
)

CORPUS_SPEC=""
for spec in "${CORPORA[@]}"; do
    IFS='|' read -r name glob shape max_size max_sprites <<< "$spec"
    if [[ "$name" == "$CORPUS" ]]; then
        CORPUS_SPEC="$spec"
        break
    fi
done
if [[ -z "$CORPUS_SPEC" ]]; then
    echo "ERROR: unknown corpus '${CORPUS}'." >&2
    exit 1
fi
IFS='|' read -r name glob shape max_size max_sprites <<< "$CORPUS_SPEC"

BASELINE_DIR="$(realpath -m tools/research/atlas_bench/baseline)"
RESOLVED_OUT="$(realpath -m "$OUT_DIR")"
case "$RESOLVED_OUT" in
    "$BASELINE_DIR"|"$BASELINE_DIR"/*)
        echo "ERROR: sweep output must not be inside the Phase 78 baseline directory." >&2
        exit 1
        ;;
esac
if [[ -d "$OUT_DIR" ]] && [[ -n "$(find "$OUT_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
    echo "ERROR: output directory is not empty: ${OUT_DIR}" >&2
    exit 1
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

if [[ ! -f "build/_cmake/${PRESET}/CMakeCache.txt" ]]; then
    cmake --preset "$PRESET"
fi
echo "=== Building atlas_bench and focused acceptance (${PRESET}) ==="
cmake --build "build/_cmake/${PRESET}" --target atlas_bench test_builder
ctest --test-dir "build/_cmake/${PRESET}" -R '^test_builder$' --output-on-failure

BENCH_BASE="build/tools/research/${PRESET}/atlas_bench"
if [[ -x "${BENCH_BASE}.exe" ]]; then
    BENCH_EXE="${BENCH_BASE}.exe"
elif [[ -x "$BENCH_BASE" ]]; then
    BENCH_EXE="$BENCH_BASE"
else
    echo "ERROR: atlas_bench exe not found under build/tools/research/${PRESET}/." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
echo "=== ${name}: ${#matches[@]} files, shape=${shape}, max_size=${max_size} ==="
index=0
for sample in "${SAMPLES[@]}"; do
    safe_sample="${sample//./p}"
    safe_sample="${safe_sample//+/_plus_}"
    sample_name="$(printf '%02d-tolerance-%s' "$index" "$safe_sample")"
    out_json="${OUT_DIR}/${sample_name}.json"
    repeat_json="${OUT_DIR}/.${sample_name}-repeat.json"
    command=("$BENCH_EXE" "$out_json" "$glob" "$name" "$shape" "$max_size" "$max_sprites" --tracer-tolerance "$sample")
    repeat_command=("$BENCH_EXE" "$repeat_json" "$glob" "$name" "$shape" "$max_size" "$max_sprites" --tracer-tolerance "$sample")

    echo "--- tolerance=${sample} ---"
    "${command[@]}"
    "${repeat_command[@]}"

    pack_path="${out_json}.ntpack"
    repeat_pack_path="${repeat_json}.ntpack"
    pack_hash="$(sha256sum "$pack_path" | awk '{print $1}')"
    repeat_hash="$(sha256sum "$repeat_pack_path" | awk '{print $1}')"
    if [[ "$pack_hash" != "$repeat_hash" ]]; then
        echo "ERROR: repeated pack hash mismatch at tolerance ${sample}." >&2
        exit 1
    fi
    pack_bytes="$(wc -c < "$pack_path")"
    echo "    deterministic_sha256=${pack_hash} pack_bytes=${pack_bytes}"
    rm -f "$repeat_json" "$repeat_pack_path" "${repeat_json}.h"
    index=$((index + 1))
done

echo "=== Done: ${#SAMPLES[@]} tolerance JSON(s) under ${OUT_DIR}/ ==="
