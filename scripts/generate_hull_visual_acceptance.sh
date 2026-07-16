#!/usr/bin/env bash
# Generates the deterministic Phase 80 visual-acceptance artifact.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

FRONTIER="tools/research/atlas_bench/hull_area_frontier.json"
CORPUS="tests/fixtures/hull_visual_acceptance/corpus.json"
SWEEP_DIR="build/bench/hull-area-mixed-aa"
FINAL_DIR="build/reports/phase80-hull-visual-acceptance"
TEMP_DIR="${FINAL_DIR}.tmp"
PREVIOUS_DIR="${FINAL_DIR}.previous"
REQUIRED="sq9-aa-triangle:convex,sq9-aa-triangle:concave,opaque-square-max3:convex,connected-mask-adversarial:concave,mixed-aa-representative:convex,mixed-aa-representative:concave,pixel-art-threshold-control:rect"

frontier_string() {
    local column="$1"
    local field="$2"
    awk -v column="$column" -v field="$field" '
        index($0, "\"column_id\": \"" column "\"") { active = 1 }
        active && index($0, "\"" field "\"") {
            value = $0
            sub(/^.*: "/, "", value)
            sub(/"[,]?[[:space:]]*$/, "", value)
            print value
            exit
        }
    ' "$FRONTIER"
}

frontier_number() {
    local column="$1"
    local field="$2"
    awk -v column="$column" -v field="$field" '
        index($0, "\"column_id\": \"" column "\"") { active = 1 }
        active && index($0, "\"" field "\"") {
            value = $0
            sub(/^.*: /, "", value)
            sub(/[,]$/, "", value)
            print value
            exit
        }
    ' "$FRONTIER"
}

frontier_valid() {
    [[ -f "$FRONTIER" ]] || return 1
    grep -q '"measurement_source_commit": "bd379927abc66d5a850f779e445584d902e84d7e"' "$FRONTIER" || return 1
    local column source expected actual percent json_percent
    for column in baseline candidate recommended; do
        source="$(frontier_string "$column" sweep_source)"
        expected="$(frontier_string "$column" sweep_sha256)"
        percent="$(frontier_number "$column" max_added_area_percent)"
        [[ -n "$source" && -n "$expected" && -n "$percent" && -f "$source" ]] || return 1
        actual="$(sha256sum "$source" | awk '{print $1}')"
        [[ "$actual" == "$expected" ]] || return 1
        json_percent="${percent%.0}"
        grep -Eq "\"max_added_area_percent\"[[:space:]]*:[[:space:]]*${json_percent}([,.]|[[:space:]]|$)" "$source" || return 1
        grep -Fq '"corpus": "C:\\projects\\neotolis-engine\\assets\\sprites\\bigatlas\\*.png"' "$source" || return 1
        grep -q '"sprites": 4812' "$source" || return 1
    done
}

if frontier_valid; then
    echo "=== Sweep frontier provenance is valid ==="
else
    echo "ERROR: measured Phase 80-05 sweep provenance is missing or stale." >&2
    echo "       It must be reproduced from source commit bd379927 on all 4,812 mixed-AA assets," >&2
    echo "       with primary/repeat production proof equality for 0%, 5%, and 10%." >&2
    echo "       The current post-Phase-80 builder is not an equivalent measurement tool; refusing substitution." >&2
    exit 1
fi

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

case "$TEMP_DIR|$PREVIOUS_DIR" in
    "build/reports/phase80-hull-visual-acceptance.tmp|build/reports/phase80-hull-visual-acceptance.previous")
        rm -rf -- "$TEMP_DIR" "$PREVIOUS_DIR"
        ;;
    *) echo "ERROR: refusing to replace unexpected report paths" >&2; exit 1 ;;
esac
trap 'rm -rf -- "$TEMP_DIR" "$PREVIOUS_DIR"' EXIT

"$REPORT_EXE" generate --corpus "$CORPUS" --frontier "$FRONTIER" --out "$TEMP_DIR"
"$REPORT_EXE" validate --manifest "$TEMP_DIR/manifest.json" --html "$TEMP_DIR/index.html" --require-samples "$REQUIRED"

if [[ -d "$FINAL_DIR" ]]; then
    mv -- "$FINAL_DIR" "$PREVIOUS_DIR"
fi
if ! mv -- "$TEMP_DIR" "$FINAL_DIR"; then
    [[ -d "$PREVIOUS_DIR" ]] && mv -- "$PREVIOUS_DIR" "$FINAL_DIR"
    exit 1
fi
rm -rf -- "$PREVIOUS_DIR"
trap - EXIT

echo "=== Phase 80 hull visual acceptance report ==="
echo "HTML:     ${FINAL_DIR}/index.html"
echo "Manifest: ${FINAL_DIR}/manifest.json"
sha256sum "$FINAL_DIR/index.html" "$FINAL_DIR/manifest.json"
