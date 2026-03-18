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
    FILE_NAMES+=("$fname")
    FILE_RAWS+=("$raw")
    FILE_GZIPS+=("$gz")
}

for file in "$OUTPUT_DIR"/*; do
    [ -f "$file" ] || continue
    measure_file "$file" ""
done

# Include asset packs (assets/ subdirectory) if present
if [ -d "$OUTPUT_DIR/assets" ]; then
    for file in "$OUTPUT_DIR/assets"/*; do
        [ -f "$file" ] || continue
        measure_file "$file" "assets/"
    done
fi

# ---------------------------------------------------------------------------
# Table output (to stdout normally, to stderr in JSON mode)
# ---------------------------------------------------------------------------
print_table() {
    local sep="-------------------------------------------"
    printf "\nTarget: %s (%s)\n" "$TARGET" "$PRESET"
    printf "%s\n" "$sep"
    printf "%-24s %10s %10s\n" "File" "Raw" "Gzip"
    printf "%s\n" "$sep"

    for i in "${!FILE_NAMES[@]}"; do
        printf "%-24s %7d B  %7d B\n" "${FILE_NAMES[$i]}" "${FILE_RAWS[$i]}" "${FILE_GZIPS[$i]}"
    done

    printf "%s\n" "$sep"
    printf "%-24s %7d B  %7d B\n" "TOTAL" "$TOTAL_RAW" "$TOTAL_GZIP"
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
