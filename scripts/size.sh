#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PRESET="${PRESET:-wasm-release}"

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
usage() {
    echo "Usage: size.sh <target-name> [--json]"
    echo ""
    echo "Measure raw and gzip sizes of all files in a WASM release build."
    echo ""
    echo "Arguments:"
    echo "  target-name   Name of the build target (e.g. hello)"
    echo "  --json        Output a single JSON object to stdout (table goes to stderr)"
    echo ""
    echo "Environment:"
    echo "  PRESET        Build preset to measure (default: wasm-release)"
    exit 1
}

if [ $# -lt 1 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    usage
fi

TARGET="$1"
shift

JSON_MODE=0
for arg in "$@"; do
    case "$arg" in
        --json) JSON_MODE=1 ;;
        *) echo "ERROR: Unknown argument: $arg"; usage ;;
    esac
done

OUTPUT_DIR="$ROOT_DIR/build/examples/$TARGET/$PRESET"

if [ ! -d "$OUTPUT_DIR" ]; then
    echo "ERROR: Build output not found: $OUTPUT_DIR"
    echo "Build the target first:"
    echo "  emcmake cmake --preset $PRESET && cmake --build --preset $PRESET"
    exit 1
fi

# ---------------------------------------------------------------------------
# Measure sizes
# ---------------------------------------------------------------------------
TOTAL_RAW=0
TOTAL_GZIP=0
ENGINE_RAW=0
ENGINE_GZIP=0
RESOURCES_RAW=0
RESOURCES_GZIP=0

# Parallel arrays for file data
declare -a FILE_NAMES=()
declare -a FILE_RAWS=()
declare -a FILE_GZIPS=()

measure_file() {
    local file="$1" prefix="$2"
    local fname="${prefix}$(basename "$file")"
    local raw gz
    raw=$(wc -c < "$file" | tr -d ' ')
    gz=$(gzip -c "$file" | wc -c | tr -d ' ')
    TOTAL_RAW=$((TOTAL_RAW + raw))
    TOTAL_GZIP=$((TOTAL_GZIP + gz))
    if [ -n "$prefix" ]; then
        RESOURCES_RAW=$((RESOURCES_RAW + raw))
        RESOURCES_GZIP=$((RESOURCES_GZIP + gz))
    else
        ENGINE_RAW=$((ENGINE_RAW + raw))
        ENGINE_GZIP=$((ENGINE_GZIP + gz))
    fi
    FILE_NAMES+=("$fname")
    FILE_RAWS+=("$raw")
    FILE_GZIPS+=("$gz")
}

# Paired builds produce both index.wasm and index_simd.wasm but a player
# downloads only one.  Show both files with real sizes; count only the
# larger one in engine/total sums.
_baseline_wasm=""
_simd_wasm=""
for file in "$OUTPUT_DIR"/*; do
    [ -f "$file" ] || continue
    case "$(basename "$file")" in
        index.wasm)      _baseline_wasm="$file" ;;
        index_simd.wasm) _simd_wasm="$file" ;;
        *) measure_file "$file" "" ;;
    esac
done

if [ -n "$_simd_wasm" ] && [ -n "$_baseline_wasm" ]; then
    _base_sz=$(wc -c < "$_baseline_wasm" | tr -d ' ')
    _simd_sz=$(wc -c < "$_simd_wasm" | tr -d ' ')
    if [ "$_simd_sz" -ge "$_base_sz" ]; then
        measure_file "$_simd_wasm" ""
        # Show baseline but don't add to totals
        _base_gz=$(gzip -c "$_baseline_wasm" | wc -c | tr -d ' ')
        FILE_NAMES+=("index.wasm")
        FILE_RAWS+=("$_base_sz")
        FILE_GZIPS+=("$_base_gz")
    else
        measure_file "$_baseline_wasm" ""
        _simd_gz=$(gzip -c "$_simd_wasm" | wc -c | tr -d ' ')
        FILE_NAMES+=("index_simd.wasm")
        FILE_RAWS+=("$_simd_sz")
        FILE_GZIPS+=("$_simd_gz")
    fi
elif [ -n "$_baseline_wasm" ]; then
    measure_file "$_baseline_wasm" ""
elif [ -n "$_simd_wasm" ]; then
    measure_file "$_simd_wasm" ""
fi

# Include asset packs (assets/ subdirectory) if present
if [ -d "$OUTPUT_DIR/assets" ]; then
    for file in "$OUTPUT_DIR/assets"/*; do
        [ -f "$file" ] || continue
        measure_file "$file" "assets/"
    done
fi

# ---------------------------------------------------------------------------
# Human-readable size formatting (KB/MB with 3 decimal places)
# ---------------------------------------------------------------------------
fmt_size() {
    local bytes="$1"
    if [ "$bytes" -ge 1048576 ]; then
        awk "BEGIN { printf \"%.3f MB\", $bytes / 1048576 }"
    elif [ "$bytes" -ge 1024 ]; then
        awk "BEGIN { printf \"%.3f KB\", $bytes / 1024 }"
    else
        printf "%d B" "$bytes"
    fi
}

# ---------------------------------------------------------------------------
# Table output (to stdout normally, to stderr in JSON mode)
# ---------------------------------------------------------------------------
print_table() {
    local sep="-----------------------------------------------------"
    printf "\nTarget: %s (%s)\n" "$TARGET" "$PRESET"
    printf "%s\n" "$sep"
    printf "%-24s %12s %12s\n" "File" "Raw" "Gzip"
    printf "%s\n" "$sep"

    # Engine files (no prefix)
    for i in "${!FILE_NAMES[@]}"; do
        case "${FILE_NAMES[$i]}" in
            assets/*) continue ;;
        esac
        printf "%-24s %12s %12s\n" "${FILE_NAMES[$i]}" "$(fmt_size "${FILE_RAWS[$i]}")" "$(fmt_size "${FILE_GZIPS[$i]}")"
    done
    printf "%-24s %12s %12s\n" "= Engine" "$(fmt_size "$ENGINE_RAW")" "$(fmt_size "$ENGINE_GZIP")"

    # Resources (prefix assets/)
    if [ "$RESOURCES_RAW" -gt 0 ]; then
        printf "%s\n" "$sep"
        for i in "${!FILE_NAMES[@]}"; do
            case "${FILE_NAMES[$i]}" in
                assets/*) ;;
                *) continue ;;
            esac
            printf "%-24s %12s %12s\n" "${FILE_NAMES[$i]}" "$(fmt_size "${FILE_RAWS[$i]}")" "$(fmt_size "${FILE_GZIPS[$i]}")"
        done
        printf "%-24s %12s %12s\n" "= Resources" "$(fmt_size "$RESOURCES_RAW")" "$(fmt_size "$RESOURCES_GZIP")"
    fi

    printf "%s\n" "$sep"
    printf "%-24s %12s %12s\n" "TOTAL" "$(fmt_size "$TOTAL_RAW")" "$(fmt_size "$TOTAL_GZIP")"
    printf "\n"
}

# ---------------------------------------------------------------------------
# JSON output (stdout)
# ---------------------------------------------------------------------------
print_json() {
    local commit date
    commit=$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo "unknown")
    date=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    printf '{\n'
    printf '  "commit": "%s",\n' "$commit"
    printf '  "date": "%s",\n' "$date"
    printf '  "files": {\n'

    for i in "${!FILE_NAMES[@]}"; do
        local comma=","
        if [ "$i" -eq $(( ${#FILE_NAMES[@]} - 1 )) ]; then
            comma=""
        fi
        printf '    "%s": { "raw": %d, "gzip": %d }%s\n' \
            "${FILE_NAMES[$i]}" "${FILE_RAWS[$i]}" "${FILE_GZIPS[$i]}" "$comma"
    done

    printf '  },\n'
    printf '  "engine": { "raw": %d, "gzip": %d },\n' "$ENGINE_RAW" "$ENGINE_GZIP"
    printf '  "resources": { "raw": %d, "gzip": %d },\n' "$RESOURCES_RAW" "$RESOURCES_GZIP"
    printf '  "total": { "raw": %d, "gzip": %d }\n' "$TOTAL_RAW" "$TOTAL_GZIP"
    printf '}\n'
}

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
if [ "$JSON_MODE" -eq 1 ]; then
    print_table >&2
    print_json
else
    print_table
fi
