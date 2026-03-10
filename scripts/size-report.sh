#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PRESET="${PRESET:-wasm-analysis}"

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
usage() {
    echo "Usage: size-report.sh <target-name> [--top N]"
    echo ""
    echo "Produce a diff-friendly WASM binary analysis report with 4 sections:"
    echo "  1. WASM section sizes"
    echo "  2. Top N functions by size"
    echo "  3. Per-module contributions (.a archive sizes)"
    echo "  4. Data segments summary"
    echo ""
    echo "Arguments:"
    echo "  target-name   Name of the build target (e.g. hello)"
    echo "  --top N       Number of top functions to show (default: 30)"
    echo ""
    echo "Environment:"
    echo "  PRESET        Build preset to analyze (default: wasm-analysis)"
    exit 1
}

if [ $# -lt 1 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    usage
fi

TARGET="$1"
shift

TOP_N=30
while [ $# -gt 0 ]; do
    case "$1" in
        --top)
            if [ $# -lt 2 ]; then
                echo "ERROR: --top requires a numeric argument"
                exit 1
            fi
            TOP_N="$2"
            shift 2
            ;;
        *)
            echo "ERROR: Unknown argument: $1"
            usage
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Dependency check
# ---------------------------------------------------------------------------
if ! command -v wasm-objdump &>/dev/null; then
    echo "ERROR: wasm-objdump not found. Install wabt:"
    echo "  sudo apt install wabt   (Linux)"
    echo "  brew install wabt       (macOS)"
    exit 1
fi

# ---------------------------------------------------------------------------
# Path resolution
# ---------------------------------------------------------------------------
OUTPUT_DIR="$ROOT_DIR/build/examples/$TARGET/$PRESET"
ENGINE_LIB_DIR="$ROOT_DIR/build/engine/$PRESET"

# Find .wasm file (OUTPUT_NAME may differ from target name, e.g. "index")
WASM_FILE=""
if [ -d "$OUTPUT_DIR" ]; then
    WASM_FILE=$(find "$OUTPUT_DIR" -maxdepth 1 -name '*.wasm' | head -1)
fi

if [ -z "$WASM_FILE" ] || [ ! -f "$WASM_FILE" ]; then
    echo "ERROR: No .wasm binary found in: $OUTPUT_DIR"
    echo "Build the target first:"
    echo "  emcmake cmake --preset $PRESET && cmake --build --preset $PRESET"
    exit 1
fi

# ---------------------------------------------------------------------------
# Report header
# ---------------------------------------------------------------------------
COMMIT=$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo "unknown")

echo "# Size Report: $TARGET ($PRESET)"
echo "# Commit: $COMMIT"
echo ""

# ---------------------------------------------------------------------------
# Section 1: WASM Section Sizes
# ---------------------------------------------------------------------------
echo "## WASM Section Sizes"
echo ""
printf "%-16s %10s\n" "Section" "Size"
printf "%-16s %10s\n" "----------------" "----------"

SECTION_TOTAL=0
declare -A SECTION_COUNT=()
while IFS= read -r line; do
    name=$(echo "$line" | awk '{print $1}')
    SECTION_COUNT[$name]=$(( ${SECTION_COUNT[$name]:-0} + 1 ))
    if [ "${SECTION_COUNT[$name]}" -gt 1 ]; then
        name="${name}_${SECTION_COUNT[$name]}"
    fi
    hex_size=$(echo "$line" | sed -E 's/.*\(size=(0x[0-9a-fA-F]+)\).*/\1/')
    dec_size=$((hex_size))
    SECTION_TOTAL=$((SECTION_TOTAL + dec_size))
    printf "%-16s %7d B\n" "$name" "$dec_size"
done < <(wasm-objdump -h "$WASM_FILE" 2>/dev/null | grep -E '^\s+\w+\s+start=')

printf "%-16s %10s\n" "----------------" "----------"
printf "%-16s %7d B\n" "TOTAL" "$SECTION_TOTAL"
echo ""

# ---------------------------------------------------------------------------
# Section 2: Top N Functions by Size
# ---------------------------------------------------------------------------
echo "## Top $TOP_N Functions by Size"
echo ""
printf "%8s  %s\n" "Size" "Name"
printf "%8s  %s\n" "--------" "----"

FUNC_COUNT=0
FUNC_TOTAL=0
# Parse function entries, sort by size desc then name asc
while IFS=$'\t' read -r size name; do
    FUNC_COUNT=$((FUNC_COUNT + 1))
    FUNC_TOTAL=$((FUNC_TOTAL + size))
done < <(wasm-objdump -j Code -x "$WASM_FILE" 2>/dev/null \
    | grep -E '^\s+-\s+func\[' \
    | sed -E 's/.*size=([0-9]+)\s+<(.*)>/\1\t\2/' \
    | sort -t$'\t' -k1,1nr -k2,2)

# Re-run to display top N (already counted totals above)
SHOWN=0
while IFS=$'\t' read -r size name; do
    SHOWN=$((SHOWN + 1))
    if [ "$SHOWN" -le "$TOP_N" ]; then
        printf "%5d B   %s\n" "$size" "$name"
    fi
done < <(wasm-objdump -j Code -x "$WASM_FILE" 2>/dev/null \
    | grep -E '^\s+-\s+func\[' \
    | sed -E 's/.*size=([0-9]+)\s+<(.*)>/\1\t\2/' \
    | sort -t$'\t' -k1,1nr -k2,2)

echo ""
echo "Total functions: $FUNC_COUNT"
echo "Total code size: $FUNC_TOTAL B"
echo ""

# ---------------------------------------------------------------------------
# Section 3: Per-Module Contributions
# ---------------------------------------------------------------------------
echo "## Per-Module Contributions"
echo ""

# Parse linked libraries from target's CMakeLists.txt
TARGET_CMAKE="$ROOT_DIR/examples/$TARGET/CMakeLists.txt"
LINKED_LIBS=""
if [ -f "$TARGET_CMAKE" ]; then
    LINKED_LIBS=$(grep 'target_link_libraries' "$TARGET_CMAKE" \
        | sed -E 's/.*target_link_libraries\([^ ]+ [A-Z]+ //' \
        | sed -E 's/\)$//' || true)
fi

echo "(pre-link archive sizes, before LTO)"
echo ""
printf "%-24s %10s\n" "Module" "Size"
printf "%-24s %10s\n" "------------------------" "----------"

LINKED_TOTAL=0
NOT_LINKED=""

if [ -d "$ENGINE_LIB_DIR" ]; then
    for afile in "$ENGINE_LIB_DIR"/*.a; do
        [ -f "$afile" ] || continue
        fname=$(basename "$afile")
        fsize=$(wc -c < "$afile" | tr -d ' ')

        # Strip lib prefix and .a suffix to get CMake target name
        cmake_name=$(echo "$fname" | sed -E 's/^lib//; s/\.a$//')

        # Check if this module is in linked libraries
        is_linked=0
        for lib in $LINKED_LIBS; do
            if [ "$lib" = "$cmake_name" ]; then
                is_linked=1
                break
            fi
        done

        if [ "$is_linked" -eq 1 ]; then
            LINKED_TOTAL=$((LINKED_TOTAL + fsize))
            printf "%-24s %7d B\n" "$fname" "$fsize"
        else
            NOT_LINKED="${NOT_LINKED}$(printf "%-24s %7d B" "$fname" "$fsize")\n"
        fi
    done
else
    echo "(no .a files found in $ENGINE_LIB_DIR)"
fi

printf "%-24s %10s\n" "------------------------" "----------"
printf "%-24s %7d B\n" "TOTAL (linked)" "$LINKED_TOTAL"

if [ -n "$NOT_LINKED" ]; then
    echo ""
    echo "Not linked to $TARGET:"
    printf "%b\n" "$NOT_LINKED"
fi
echo ""

# ---------------------------------------------------------------------------
# Section 4: Data Segments Summary
# ---------------------------------------------------------------------------
echo "## Data Segments Summary"
echo ""

DATA_OUTPUT=$(wasm-objdump -j Data -x "$WASM_FILE" 2>/dev/null || true)

if [ -z "$DATA_OUTPUT" ]; then
    echo "No data segments found."
else
    SEG_COUNT=$(echo "$DATA_OUTPUT" | grep -cE '^\s+-\s+segment' || echo "0")
    SEG_TOTAL=$(echo "$DATA_OUTPUT" \
        | grep -E '^\s+-\s+segment' \
        | sed -E 's/.*size=([0-9]+).*/\1/' \
        | awk '{s+=$1} END {print s+0}')
    echo "Segments: $SEG_COUNT"
    echo "Total data size: $SEG_TOTAL B"
fi
