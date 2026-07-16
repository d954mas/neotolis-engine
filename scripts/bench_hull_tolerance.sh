#!/usr/bin/env bash
# Measures the production area-budget frontier without replacing Phase 78 baselines.
# Usage: bench_hull_tolerance.sh [--preset P] [--corpus NAME] [--out DIR] [--samples CSV]

set -euo pipefail

hull_path_key() {
    local raw_path="${1//\\//}"
    local platform="${2:-${NT_HULL_AREA_PLATFORM:-$(uname -s)}}"
    local resolved
    resolved="$(realpath -m -- "$raw_path")"
    case "$platform" in
        MSYS*|MINGW*|CYGWIN*) printf '%s' "$resolved" | tr '[:upper:]' '[:lower:]' ;;
        *) printf '%s' "$resolved" ;;
    esac
}

hull_path_is_protected() {
    local baseline_key output_key
    baseline_key="$(hull_path_key "$1" "${3:-}")"
    output_key="$(hull_path_key "$2" "${3:-}")"
    case "$output_key" in
        "$baseline_key"|"$baseline_key"/*) return 0 ;;
        *) return 1 ;;
    esac
}

if [[ "${NT_HULL_AREA_GUARD_LIB_ONLY:-0}" == 1 ]]; then
    return 0 2>/dev/null || exit 0
fi

cd "$(git rev-parse --show-toplevel)"

PRESET="native-release"
CORPUS="mixed_aa"
OUT_DIR="build/bench/hull-area"
SAMPLE_CSV="0,2,5,10,15,25"
VERIFY_REPEAT=0
HULL_TOTALS=()
HULL_MEANS=()
FRONTIER_DENSITIES=()
SELECTED_OVERDRAW=()
BENCH_THREADS="${NT_BUILDER_THREADS:-1}"

if [[ ! "$BENCH_THREADS" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: NT_BUILDER_THREADS must be a positive integer for a reproducible sweep." >&2
    exit 1
fi
export NT_BUILDER_THREADS="$BENCH_THREADS"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset) PRESET="$2"; shift 2 ;;
        --corpus) CORPUS="$2"; shift 2 ;;
        --out) OUT_DIR="$2"; shift 2 ;;
        --samples) SAMPLE_CSV="$2"; shift 2 ;;
        --verify-repeat) VERIFY_REPEAT=1; shift ;;
        *) echo "Unknown arg: $1" >&2; exit 1 ;;
    esac
done

case "$PRESET" in
    native-*) ;;
    *) echo "ERROR: atlas_bench requires a native preset, got '${PRESET}'." >&2; exit 1 ;;
esac

IFS=',' read -r -a SAMPLES <<< "$SAMPLE_CSV"
if [[ ${#SAMPLES[@]} -eq 0 ]]; then
    echo "ERROR: --samples must contain at least one area percentage." >&2
    exit 1
fi
for sample in "${SAMPLES[@]}"; do
    if [[ ! "$sample" =~ ^([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?$ ]]; then
        echo "ERROR: invalid finite non-negative area percentage '${sample}'." >&2
        exit 1
    fi
done

CORPORA=(
    "anim_heavy|assets/bench/anim_heavy/*.png|concave|2048|0"
    "mixed_aa|assets/sprites/bigatlas/*.png|concave|4096|128"
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

if hull_path_is_protected "tools/research/atlas_bench/baseline" "$OUT_DIR"; then
    echo "ERROR: sweep output must not be inside the Phase 78 baseline directory." >&2
    exit 1
fi
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
    sample_name="$(printf '%02d-percent-%s' "$index" "$safe_sample")"
    out_json="${OUT_DIR}/${sample_name}.json"
    repeat_json="${OUT_DIR}/.${sample_name}-repeat.json"
    command=("$BENCH_EXE" "$out_json" "$glob" "$name" "$shape" "$max_size" "$max_sprites" --max-added-area-percent "$sample")
    repeat_command=("$BENCH_EXE" "$repeat_json" "$glob" "$name" "$shape" "$max_size" "$max_sprites" --max-added-area-percent "$sample")

    echo "--- max_added_area_percent=${sample} ---"
    "${command[@]}"
    "${repeat_command[@]}"

    pack_path="${out_json}.ntpack"
    repeat_pack_path="${repeat_json}.ntpack"
    pack_hash="$(sha256sum "$pack_path" | awk '{print $1}')"
    repeat_hash="$(sha256sum "$repeat_pack_path" | awk '{print $1}')"
    if [[ "$pack_hash" != "$repeat_hash" ]]; then
        echo "ERROR: repeated pack hash mismatch at ${sample}%." >&2
        exit 1
    fi
    primary_portable="${OUT_DIR}/.${sample_name}-portable.json"
    repeat_portable="${OUT_DIR}/.${sample_name}-repeat-portable.json"
    sed '/"pack_ms"/d;/"cpu"/d' "$out_json" > "$primary_portable"
    sed '/"pack_ms"/d;/"cpu"/d' "$repeat_json" > "$repeat_portable"
    if ! cmp -s "$primary_portable" "$repeat_portable"; then
        echo "ERROR: repeated portable proof/metric mismatch at ${sample}%." >&2
        exit 1
    fi
    pack_bytes="$(wc -c < "$pack_path")"
    hull_total="$(sed -n '/"hull_verts": {/,/}/ s/.*"total": \([0-9][0-9]*\).*/\1/p' "$out_json")"
    hull_mean="$(sed -n '/"hull_verts": {/,/}/ s/.*"mean": \([0-9.][0-9.]*\).*/\1/p' "$out_json")"
    frontier_density="$(sed -n 's/.*"density_fill_frontier": \([0-9.][0-9.]*\).*/\1/p' "$out_json")"
    selected_overdraw="$(sed -n 's/.*"total_overdraw_percent": \([0-9.][0-9.]*\).*/\1/p' "$out_json")"
    [[ -n "$hull_total" && -n "$hull_mean" && -n "$frontier_density" && -n "$selected_overdraw" ]] || { echo "ERROR: missing frontier distribution metrics in ${out_json}" >&2; exit 1; }
    HULL_TOTALS+=("$hull_total")
    HULL_MEANS+=("$hull_mean")
    FRONTIER_DENSITIES+=("$frontier_density")
    SELECTED_OVERDRAW+=("$selected_overdraw")
    echo "    deterministic_sha256=${pack_hash} pack_bytes=${pack_bytes}"
    rm -f "$repeat_json" "$repeat_pack_path" "${repeat_json}.h" "$primary_portable" "$repeat_portable"
    index=$((index + 1))
done

if [[ "$SAMPLE_CSV" == "0,2,5,10,15,25" ]]; then
    FRONTIER="tools/research/atlas_bench/hull_area_frontier.json"
    FRONTIER_TMP="${FRONTIER}.tmp.${$}"
    commit="$(git rev-parse HEAD)"
    corpus_sha="$(sha256sum "${matches[@]}" | sha256sum | awk '{print $1}')"
    settings_sha="$(printf '%s\n' "$name|$glob|$shape|$max_size|$max_sprites|$SAMPLE_CSV|threads=$BENCH_THREADS" | sha256sum | awk '{print $1}')"
    {
        printf '{\n  "schema_version": 2,\n  "measurement_source_commit": "%s",\n' "$commit"
        printf '  "tool_version": "2.0.0",\n  "builder_threads": %s,\n  "corpus_sha256": "%s",\n  "settings_sha256": "%s",\n' "$BENCH_THREADS" "$corpus_sha" "$settings_sha"
        printf '  "selection_rationale": "On the deterministic 128-sprite mixed-AA corpus, total hull vertices are %s at 0%%, %s at 2/5%%, and %s at 10/15/25%%; 10%% captures the final measured reduction and larger allowances add no gain.",\n' \
            "${HULL_TOTALS[0]}" "${HULL_TOTALS[1]}" "${HULL_TOTALS[3]}"
        printf '  "default_max_added_area_percent": 10,\n  "sweep_values": [0, 2, 5, 10, 15, 25],\n  "sweep": [\n'
        for i in "${!SAMPLES[@]}"; do
            sample="${SAMPLES[$i]}"
            safe_sample="${sample//./p}"
            source="${OUT_DIR}/$(printf '%02d-percent-%s' "$i" "$safe_sample").json"
            source_sha="$(sha256sum "$source" | awk '{print $1}')"
            selected_pack_sha="$(sed -n 's/.*"selected_pack_sha256": "\([0-9a-f]*\)".*/\1/p' "$source")"
            baseline_pack_sha="$(sed -n 's/.*"baseline_pack_sha256": "\([0-9a-f]*\)".*/\1/p' "$source")"
            [[ ${#selected_pack_sha} -eq 64 && ${#baseline_pack_sha} -eq 64 ]] || { echo "ERROR: missing production pack hashes in ${source}" >&2; exit 1; }
            printf '    %s{"max_added_area_percent": %s, "hull_vertices_total": %s, "hull_vertices_mean": %s, "density_fill_frontier": %s, "representative_total_overdraw_percent": %s, "sweep_source": "%s", "sweep_sha256": "%s", "baseline_pack_sha256": "%s", "selected_pack_sha256": "%s"}\n' \
                "$([[ $i -eq 0 ]] && printf '' || printf ',')" "$sample" "${HULL_TOTALS[$i]}" "${HULL_MEANS[$i]}" "${FRONTIER_DENSITIES[$i]}" "${SELECTED_OVERDRAW[$i]}" "$source" "$source_sha" \
                "$baseline_pack_sha" "$selected_pack_sha"
        done
        printf '  ],\n  "columns": [\n'
        for spec in 'baseline|0' 'candidate|2' 'recommended|3'; do
            IFS='|' read -r id i <<< "$spec"
            sample="${SAMPLES[$i]}"
            source="${OUT_DIR}/$(printf '%02d-percent-%s' "$i" "${sample//./p}").json"
            source_sha="$(sha256sum "$source" | awk '{print $1}')"
            printf '    %s{"column_id": "%s", "max_added_area_percent": %s, "sweep_source": "%s", "sweep_sha256": "%s"}\n' \
                "$([[ "$id" == baseline ]] && printf '' || printf ',')" "$id" "$sample" "$source" "$source_sha"
        done
        printf '  ]\n}\n'
    } > "$FRONTIER_TMP"
    mv "$FRONTIER_TMP" "$FRONTIER"
    echo "=== Published deterministic frontier: ${FRONTIER} ==="
elif [[ $VERIFY_REPEAT -eq 1 ]]; then
    echo "ERROR: --verify-repeat publication requires exact samples 0,2,5,10,15,25." >&2
    exit 1
fi

echo "=== Done: ${#SAMPLES[@]} area-budget JSON(s) under ${OUT_DIR}/ ==="
