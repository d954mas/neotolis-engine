#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TARGET="${1:-hello}"
PRESET="wasm-release"

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------
if ! command -v emcmake &>/dev/null; then
    echo "ERROR: emcmake not found in PATH"
    echo "Install Emscripten SDK: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found in PATH"
    exit 1
fi

CMAKE_FILE="$ROOT_DIR/examples/$TARGET/CMakeLists.txt"
if [ ! -f "$CMAKE_FILE" ]; then
    echo "ERROR: CMakeLists.txt not found for target '$TARGET': $CMAKE_FILE"
    exit 1
fi

OUTPUT_DIR="$ROOT_DIR/build/examples/$TARGET/$PRESET"

# ---------------------------------------------------------------------------
# Cleanup: always restore CMakeLists.txt backup
# ---------------------------------------------------------------------------
BACKUP_FILE=""
cleanup() {
    if [ -n "$BACKUP_FILE" ] && [ -f "$BACKUP_FILE" ]; then
        cp "$BACKUP_FILE" "$CMAKE_FILE"
        rm -f "$BACKUP_FILE"
        echo "[cleanup] CMakeLists.txt restored from backup"
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Flag combinations to benchmark
# ---------------------------------------------------------------------------
COMBO_NAMES=(
    "baseline"
    "-flto"
    "--closure 1"
    "-flto --closure 1"
    "-sMALLOC=emmalloc"
    "-sFILESYSTEM=0"
    "-sENVIRONMENT=web"
    "-flto -sMALLOC=emmalloc -sFILESYSTEM=0 -sENVIRONMENT=web"
    "-flto --closure 1 -sMALLOC=emmalloc -sFILESYSTEM=0 -sENVIRONMENT=web"
)

COMBO_LABELS=(
    "baseline"
    "flto"
    "closure"
    "flto+closure"
    "emmalloc"
    "no-fs"
    "env-web"
    "flto+emmalloc+no-fs+web"
    "kitchen sink"
)

# Results arrays (parallel with combos)
declare -a RES_WASM_RAW=()
declare -a RES_WASM_GZ=()
declare -a RES_JS_RAW=()
declare -a RES_JS_GZ=()
declare -a RES_TOTAL_RAW=()
declare -a RES_TOTAL_GZ=()
declare -a RES_STATUS=()

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
measure_file() {
    local file="$1"
    if [ -f "$file" ]; then
        wc -c < "$file" | tr -d ' '
    else
        echo "0"
    fi
}

measure_file_gzip() {
    local file="$1"
    if [ -f "$file" ]; then
        gzip -c "$file" | wc -c | tr -d ' '
    else
        echo "0"
    fi
}

inject_flags() {
    local flags="$1"
    # Append a new EMSCRIPTEN block with the extra link options at end of file
    {
        echo ""
        echo "# --- benchmark-flags.sh: temporary flags ---"
        echo "if(EMSCRIPTEN)"
        # Split flags and add each as a SHELL: option
        for flag in $flags; do
            echo "    target_link_options($TARGET PRIVATE \"SHELL:$flag\")"
        done
        echo "endif()"
    } >> "$CMAKE_FILE"
}

build_target() {
    # Reconfigure + build
    (cd "$ROOT_DIR" && emcmake cmake --preset "$PRESET" 2>&1) && \
    (cd "$ROOT_DIR" && cmake --build --preset "$PRESET" 2>&1)
}

smoke_test_js() {
    local js_file="$OUTPUT_DIR/$TARGET.js"
    if [ -f "$js_file" ] && command -v node &>/dev/null; then
        if node --check "$js_file" 2>/dev/null; then
            return 0
        else
            return 1
        fi
    fi
    # No node or no file -- skip check
    return 0
}

# ---------------------------------------------------------------------------
# Main benchmark loop
# ---------------------------------------------------------------------------
BACKUP_FILE="$(mktemp)"
cp "$CMAKE_FILE" "$BACKUP_FILE"

TOTAL_COMBOS=${#COMBO_NAMES[@]}
echo ""
echo "================================================================="
echo "  Benchmark: $TARGET ($PRESET)"
echo "  Testing $TOTAL_COMBOS flag combinations"
echo "================================================================="
echo ""

for i in "${!COMBO_NAMES[@]}"; do
    combo="${COMBO_NAMES[$i]}"
    label="${COMBO_LABELS[$i]}"
    idx=$((i + 1))

    echo "[$idx/$TOTAL_COMBOS] $label"

    # Restore clean CMakeLists.txt before each combo
    cp "$BACKUP_FILE" "$CMAKE_FILE"

    # Inject flags (skip for baseline)
    if [ "$combo" != "baseline" ]; then
        inject_flags "$combo"
    fi

    # Build
    if ! build_output=$(build_target 2>&1); then
        echo "  => BUILD FAILED"
        RES_WASM_RAW+=("0")
        RES_WASM_GZ+=("0")
        RES_JS_RAW+=("0")
        RES_JS_GZ+=("0")
        RES_TOTAL_RAW+=("0")
        RES_TOTAL_GZ+=("0")
        RES_STATUS+=("BUILD FAILED")
        continue
    fi

    # Smoke test for --closure combos
    status="OK"
    if echo "$combo" | grep -q "closure"; then
        if ! smoke_test_js; then
            status="JS PARSE ERROR"
        fi
    fi

    # Measure sizes
    wasm_raw=$(measure_file "$OUTPUT_DIR/$TARGET.wasm")
    wasm_gz=$(measure_file_gzip "$OUTPUT_DIR/$TARGET.wasm")
    js_raw=$(measure_file "$OUTPUT_DIR/$TARGET.js")
    js_gz=$(measure_file_gzip "$OUTPUT_DIR/$TARGET.js")

    total_raw=0
    total_gz=0
    for file in "$OUTPUT_DIR"/*; do
        [ -f "$file" ] || continue
        raw=$(measure_file "$file")
        gz=$(measure_file_gzip "$file")
        total_raw=$((total_raw + raw))
        total_gz=$((total_gz + gz))
    done

    RES_WASM_RAW+=("$wasm_raw")
    RES_WASM_GZ+=("$wasm_gz")
    RES_JS_RAW+=("$js_raw")
    RES_JS_GZ+=("$js_gz")
    RES_TOTAL_RAW+=("$total_raw")
    RES_TOTAL_GZ+=("$total_gz")
    RES_STATUS+=("$status")

    echo "  => wasm: ${wasm_raw}B  js: ${js_raw}B  total: ${total_raw}B  gzip: ${total_gz}B  [$status]"
done

# Restore CMakeLists.txt (trap will also do this, but be explicit)
cp "$BACKUP_FILE" "$CMAKE_FILE"

# ---------------------------------------------------------------------------
# Results table
# ---------------------------------------------------------------------------
echo ""
echo "================================================================="
echo "  Results: $TARGET ($PRESET)"
echo "================================================================="
echo ""

# Header
printf "%-28s %8s %8s %8s %8s %8s %8s  %s\n" \
    "Flags" ".wasm" ".wasm gz" ".js" ".js gz" "Total" "Gzip" "Status"
printf "%s\n" "-------------------------------------------------------------------------------------------------------------"

# Baseline values for savings calculation
BASE_TOTAL_RAW="${RES_TOTAL_RAW[0]}"
BASE_TOTAL_GZ="${RES_TOTAL_GZ[0]}"

for i in "${!COMBO_LABELS[@]}"; do
    label="${COMBO_LABELS[$i]}"
    status="${RES_STATUS[$i]}"

    if [ "$status" = "BUILD FAILED" ] || [ "$status" = "JS PARSE ERROR" ]; then
        printf "%-28s %8s %8s %8s %8s %8s %8s  %s\n" \
            "$label" "-" "-" "-" "-" "-" "-" "$status"
        continue
    fi

    printf "%-28s %8s %8s %8s %8s %8s %8s  %s\n" \
        "$label" \
        "${RES_WASM_RAW[$i]}" "${RES_WASM_GZ[$i]}" \
        "${RES_JS_RAW[$i]}" "${RES_JS_GZ[$i]}" \
        "${RES_TOTAL_RAW[$i]}" "${RES_TOTAL_GZ[$i]}" \
        "$status"
done

# ---------------------------------------------------------------------------
# Savings vs baseline
# ---------------------------------------------------------------------------
echo ""
echo "Savings vs baseline:"
echo ""
printf "%-28s %12s %12s\n" "Flags" "Total raw" "Total gzip"
printf "%s\n" "------------------------------------------------------"

for i in "${!COMBO_LABELS[@]}"; do
    label="${COMBO_LABELS[$i]}"
    status="${RES_STATUS[$i]}"

    if [ "$status" = "BUILD FAILED" ] || [ "$status" = "JS PARSE ERROR" ]; then
        printf "%-28s %12s %12s\n" "$label" "N/A" "N/A"
        continue
    fi

    raw="${RES_TOTAL_RAW[$i]}"
    gz="${RES_TOTAL_GZ[$i]}"

    if [ "$BASE_TOTAL_RAW" -gt 0 ]; then
        diff_raw=$((raw - BASE_TOTAL_RAW))
        pct_raw=$(( diff_raw * 100 / BASE_TOTAL_RAW ))
    else
        diff_raw=0
        pct_raw=0
    fi

    if [ "$BASE_TOTAL_GZ" -gt 0 ]; then
        diff_gz=$((gz - BASE_TOTAL_GZ))
        pct_gz=$(( diff_gz * 100 / BASE_TOTAL_GZ ))
    else
        diff_gz=0
        pct_gz=0
    fi

    if [ "$i" -eq 0 ]; then
        printf "%-28s %12s %12s\n" "$label" "(baseline)" "(baseline)"
    else
        printf "%-28s %+8dB %+3d%% %+8dB %+3d%%\n" "$label" "$diff_raw" "$pct_raw" "$diff_gz" "$pct_gz"
    fi
done

# ---------------------------------------------------------------------------
# Find best combo by total gzip size
# ---------------------------------------------------------------------------
best_idx=0
best_gz="${RES_TOTAL_GZ[0]}"

for i in "${!COMBO_LABELS[@]}"; do
    status="${RES_STATUS[$i]}"
    if [ "$status" = "BUILD FAILED" ] || [ "$status" = "JS PARSE ERROR" ]; then
        continue
    fi
    gz="${RES_TOTAL_GZ[$i]}"
    if [ "$gz" -lt "$best_gz" ]; then
        best_gz="$gz"
        best_idx="$i"
    fi
done

echo ""
echo "Best by total gzip: ${COMBO_LABELS[$best_idx]} (${best_gz}B gzip)"
echo ""

# ---------------------------------------------------------------------------
# Interactive prompt: apply flags?
# ---------------------------------------------------------------------------
echo "Available combos:"
for i in "${!COMBO_LABELS[@]}"; do
    status="${RES_STATUS[$i]}"
    marker=""
    if [ "$i" -eq "$best_idx" ]; then
        marker=" (*best*)"
    fi
    if [ "$status" = "BUILD FAILED" ] || [ "$status" = "JS PARSE ERROR" ]; then
        echo "  $i. ${COMBO_LABELS[$i]}  [$status]"
    else
        echo "  $i. ${COMBO_LABELS[$i]}  (gzip: ${RES_TOTAL_GZ[$i]}B)$marker"
    fi
done

echo ""
read -r -p "Apply flags from combo number to CMakeLists.txt? (N/number): " choice

if [ -z "$choice" ] || [ "$choice" = "N" ] || [ "$choice" = "n" ]; then
    echo "No changes applied."
    exit 0
fi

# Validate choice is a number in range
if ! [[ "$choice" =~ ^[0-9]+$ ]] || [ "$choice" -ge "$TOTAL_COMBOS" ]; then
    echo "Invalid choice: $choice"
    echo "No changes applied."
    exit 1
fi

chosen_combo="${COMBO_NAMES[$choice]}"
chosen_label="${COMBO_LABELS[$choice]}"

if [ "$chosen_combo" = "baseline" ]; then
    echo "Baseline selected -- no changes needed."
    exit 0
fi

# Check status of chosen combo
if [ "${RES_STATUS[$choice]}" = "BUILD FAILED" ] || [ "${RES_STATUS[$choice]}" = "JS PARSE ERROR" ]; then
    echo "ERROR: Combo '$chosen_label' had issues (${RES_STATUS[$choice]}). Not applying."
    exit 1
fi

# Apply chosen flags to CMakeLists.txt
# Restore clean state first (from backup)
cp "$BACKUP_FILE" "$CMAKE_FILE"

# Inject flags permanently (inside existing EMSCRIPTEN block)
# We add a clearly marked section at the end of the file
{
    echo ""
    echo "# Optimization flags (applied by benchmark-flags.sh)"
    echo "if(EMSCRIPTEN)"
    for flag in $chosen_combo; do
        echo "    target_link_options($TARGET PRIVATE \"SHELL:$flag\")"
    done
    echo "endif()"
} >> "$CMAKE_FILE"

echo ""
echo "Applied '$chosen_label' flags to $CMAKE_FILE"
echo "Verify with: emcmake cmake --preset $PRESET && cmake --build --preset $PRESET"
echo "Then check sizes: bash scripts/size.sh $TARGET"
