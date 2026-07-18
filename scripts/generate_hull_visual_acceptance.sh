#!/usr/bin/env bash
# Generates the deterministic hull visual-acceptance artifact.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

VERIFY_REPEAT=false
if [[ $# -gt 1 || (${1:-} != "" && ${1:-} != "--verify-repeat") ]]; then
    echo "Usage: $0 [--verify-repeat]" >&2
    exit 2
fi
if [[ ${1:-} == "--verify-repeat" ]]; then
    VERIFY_REPEAT=true
fi

FRONTIER="tools/research/atlas_bench/hull_area_frontier.json"
CORPUS="tests/fixtures/hull_visual_acceptance/corpus.json"
FINAL_DIR="build/reports/phase80-hull-visual-acceptance"
TEMP_DIR="${FINAL_DIR}.tmp"
REPEAT_DIR="${FINAL_DIR}.repeat"
PREVIOUS_DIR="${FINAL_DIR}.previous"
REQUIRED="sq9-aa-triangle:convex,rotated-diamond:convex,concave-notch:concave,transparent-donut:concave,opaque-square-max3:convex,connected-mask-adversarial:concave,pixel-art-threshold-control:rect"
PERCENTS=(0 2 5 10 15 25)

hull_visual_cleanup() {
    local status="${1:-0}"
    rm -rf -- "$TEMP_DIR" "$REPEAT_DIR"
    if [[ -d "$PREVIOUS_DIR" && ! -d "$FINAL_DIR" ]]; then
        mv -- "$PREVIOUS_DIR" "$FINAL_DIR"
    else
        rm -rf -- "$PREVIOUS_DIR"
    fi
    return "$status"
}

hull_visual_prepare_paths() {
    rm -rf -- "$TEMP_DIR" "$REPEAT_DIR"
    if [[ -d "$PREVIOUS_DIR" && ! -d "$FINAL_DIR" ]]; then
        mv -- "$PREVIOUS_DIR" "$FINAL_DIR"
    else
        rm -rf -- "$PREVIOUS_DIR"
    fi
}

frontier_valid() {
    local expected_builder_sha="${1:-}"
    [[ -f "$FRONTIER" ]] || return 1
    [[ "$expected_builder_sha" =~ ^[0-9a-f]{64}$ ]] || return 1
    grep -Eq '"schema_version"[[:space:]]*:[[:space:]]*3([,[:space:]]|$)' "$FRONTIER" || return 1
    grep -Eq '"measurement_source_commit"[[:space:]]*:[[:space:]]*"[0-9a-f]{40}"' "$FRONTIER" || return 1
    grep -Eq '"tool_version"[[:space:]]*:[[:space:]]*"2\.0\.0"' "$FRONTIER" || return 1
    grep -Eq '"builder_threads"[[:space:]]*:[[:space:]]*1([,[:space:]]|$)' "$FRONTIER" || return 1
    grep -Fq "\"builder_binary_sha256\": \"${expected_builder_sha}\"" "$FRONTIER" || return 1
    grep -Eq '"corpus_sha256"[[:space:]]*:[[:space:]]*"[0-9a-f]{64}"' "$FRONTIER" || return 1
    grep -Eq '"settings_sha256"[[:space:]]*:[[:space:]]*"[0-9a-f]{64}"' "$FRONTIER" || return 1
    grep -Eq '"sweep_values"[[:space:]]*:[[:space:]]*\[0,[[:space:]]*2,[[:space:]]*5,[[:space:]]*10,[[:space:]]*15,[[:space:]]*25\]' "$FRONTIER" || return 1

    local sources=()
    local hashes=()
    local baseline_hashes=()
    local selected_hashes=()
    local hull_totals=()
    local hull_means=()
    local frontier_densities=()
    local representative_overdraw=()
    mapfile -t sources < <(sed -n 's/.*"sweep_source": "\([^"]*\)".*/\1/p' "$FRONTIER" | awk 'NR <= 6')
    mapfile -t hashes < <(sed -n 's/.*"sweep_sha256": "\([0-9a-f]*\)".*/\1/p' "$FRONTIER" | awk 'NR <= 6')
    mapfile -t baseline_hashes < <(sed -n 's/.*"baseline_pack_sha256": "\([0-9a-f]*\)".*/\1/p' "$FRONTIER" | awk 'NR <= 6')
    mapfile -t selected_hashes < <(sed -n 's/.*"selected_pack_sha256": "\([0-9a-f]*\)".*/\1/p' "$FRONTIER" | awk 'NR <= 6')
    mapfile -t hull_totals < <(sed -n 's/.*"hull_vertices_total": \([0-9][0-9]*\).*/\1/p' "$FRONTIER" | awk 'NR <= 6')
    mapfile -t hull_means < <(sed -n 's/.*"hull_vertices_mean": \([0-9.][0-9.]*\).*/\1/p' "$FRONTIER" | awk 'NR <= 6')
    mapfile -t frontier_densities < <(sed -n 's/.*"density_fill_frontier": \([0-9.][0-9.]*\).*/\1/p' "$FRONTIER" | awk 'NR <= 6')
    mapfile -t representative_overdraw < <(sed -n 's/.*"representative_total_overdraw_percent": \([0-9.][0-9.]*\).*/\1/p' "$FRONTIER" | awk 'NR <= 6')
    [[ ${#sources[@]} -eq 6 && ${#hashes[@]} -eq 6 && ${#baseline_hashes[@]} -eq 6 && ${#selected_hashes[@]} -eq 6 &&
        ${#hull_totals[@]} -eq 6 && ${#hull_means[@]} -eq 6 && ${#frontier_densities[@]} -eq 6 && ${#representative_overdraw[@]} -eq 6 ]] || return 1

    local i source actual proof_total proof_mean proof_density proof_overdraw
    for i in "${!PERCENTS[@]}"; do
        source="${sources[$i]}"
        [[ -f "$source" ]] || return 1
        actual="$(sha256sum "$source" | awk '{print $1}')"
        [[ "$actual" == "${hashes[$i]}" ]] || return 1
        grep -Eq "\"max_added_area_percent\"[[:space:]]*:[[:space:]]*${PERCENTS[$i]}([,.[:space:]]|$)" "$source" || return 1
        grep -Fq '"corpus": "assets/sprites/bigatlas/*.png"' "$source" || return 1
        grep -Eq '"sprites"[[:space:]]*:[[:space:]]*128([,[:space:]]|$)' "$source" || return 1
        grep -Eq '"valid"[[:space:]]*:[[:space:]]*true([,[:space:]]|$)' "$source" || return 1
        grep -Fq '"gates": {"full_cell_coverage":true,"topology":true,"triangulation":true,"allowance":true,"ceiling":true}' "$source" || return 1
        grep -Fq "\"baseline_pack_sha256\": \"${baseline_hashes[$i]}\"" "$source" || return 1
        grep -Fq "\"selected_pack_sha256\": \"${selected_hashes[$i]}\"" "$source" || return 1
        proof_total="$(sed -n '/"hull_verts": {/,/}/ s/.*"total": \([0-9][0-9]*\).*/\1/p' "$source")"
        proof_mean="$(sed -n '/"hull_verts": {/,/}/ s/.*"mean": \([0-9.][0-9.]*\).*/\1/p' "$source")"
        proof_density="$(sed -n 's/.*"density_fill_frontier": \([0-9.][0-9.]*\).*/\1/p' "$source")"
        proof_overdraw="$(sed -n 's/.*"total_overdraw_percent": \([0-9.][0-9.]*\).*/\1/p' "$source")"
        [[ "${hull_totals[$i]}" == "$proof_total" && "${hull_means[$i]}" == "$proof_mean" &&
            "${frontier_densities[$i]}" == "$proof_density" && "${representative_overdraw[$i]}" == "$proof_overdraw" ]] || return 1
    done
}

if [[ "${NT_HULL_VISUAL_GUARD_LIB_ONLY:-0}" == 1 ]]; then
    return 0 2>/dev/null || exit 0
fi

if [[ ! -f build/_cmake/native-release/CMakeCache.txt ]]; then
    cmake --preset native-release
fi
cmake --build build/_cmake/native-release --target atlas_bench
CANONICAL_BUILDER="build/tools/research/native-release/atlas_bench"
if [[ -x "${CANONICAL_BUILDER}.exe" ]]; then
    CANONICAL_BUILDER="${CANONICAL_BUILDER}.exe"
elif [[ ! -x "$CANONICAL_BUILDER" ]]; then
    echo "ERROR: canonical native-release atlas_bench executable missing" >&2
    exit 1
fi
CANONICAL_BUILDER_SHA="$(sha256sum "$CANONICAL_BUILDER" | awk '{print $1}')"

if ! frontier_valid "$CANONICAL_BUILDER_SHA"; then
    echo "ERROR: the six-column hull frontier or one of its measured proof artifacts is missing, stale, or corrupt." >&2
    exit 1
fi
echo "=== Six-column sweep provenance is valid ==="

if [[ ! -f build/_cmake/native-debug/CMakeCache.txt ]]; then
    cmake --preset native-debug
fi
cmake --build build/_cmake/native-debug --target atlas_hull_visual_report

REPORT_EXE="build/tools/research/native-debug/atlas_hull_visual_report"
if [[ -x "${REPORT_EXE}.exe" ]]; then
    REPORT_EXE="${REPORT_EXE}.exe"
elif [[ ! -x "$REPORT_EXE" ]]; then
    echo "ERROR: atlas_hull_visual_report executable missing" >&2
    exit 1
fi

case "$TEMP_DIR|$REPEAT_DIR|$PREVIOUS_DIR" in
    "build/reports/phase80-hull-visual-acceptance.tmp|build/reports/phase80-hull-visual-acceptance.repeat|build/reports/phase80-hull-visual-acceptance.previous")
        hull_visual_prepare_paths
        ;;
    *) echo "ERROR: refusing to replace unexpected report paths" >&2; exit 1 ;;
esac
trap 'hull_visual_cleanup $?' EXIT

"$REPORT_EXE" generate --corpus "$CORPUS" --frontier "$FRONTIER" --out "$TEMP_DIR"
"$REPORT_EXE" validate --manifest "$TEMP_DIR/manifest.json" --html "$TEMP_DIR/index.html" --require-samples "$REQUIRED"

if [[ "$VERIFY_REPEAT" == true ]]; then
    "$REPORT_EXE" generate --corpus "$CORPUS" --frontier "$FRONTIER" --out "$REPEAT_DIR"
    "$REPORT_EXE" validate --manifest "$REPEAT_DIR/manifest.json" --html "$REPEAT_DIR/index.html" --require-samples "$REQUIRED"
    cmp -s "$TEMP_DIR/manifest.json" "$REPEAT_DIR/manifest.json" || { echo "ERROR: repeated manifest differs" >&2; exit 1; }
    cmp -s "$TEMP_DIR/index.html" "$REPEAT_DIR/index.html" || { echo "ERROR: repeated HTML differs" >&2; exit 1; }
    rm -rf -- "$REPEAT_DIR"
fi

if [[ -d "$FINAL_DIR" ]]; then
    mv -- "$FINAL_DIR" "$PREVIOUS_DIR"
fi
if ! mv -- "$TEMP_DIR" "$FINAL_DIR"; then
    [[ -d "$PREVIOUS_DIR" ]] && mv -- "$PREVIOUS_DIR" "$FINAL_DIR"
    exit 1
fi
rm -rf -- "$PREVIOUS_DIR"
trap - EXIT

echo "=== Hull visual acceptance report ==="
echo "HTML:     ${FINAL_DIR}/index.html"
echo "Manifest: ${FINAL_DIR}/manifest.json"
sha256sum "$FINAL_DIR/index.html" "$FINAL_DIR/manifest.json"
