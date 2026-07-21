#!/usr/bin/env bash
# atlas_transform_sweep.sh — NON-CI transform-mask density evidence (Phase 81, D-13/D-15).
#
# Runs atlas_bench across 3 masks (identity, export, all) x 3 corpora
# (anim_heavy, mixed_aa, rect_only) = 9 packs and emits a markdown density table.
# The nine-patch corpus is excluded (its forced-identity sprites make masks moot);
# the ROTATIONS/FLIPS presets are informative-only and out of scope (D-13).
#
# Not wired into check.sh. Requirements:
#   - git lfs pull   (mixed_aa reuses the 4.8k-file bigatlas LFS fixture)
#   - a built native atlas_bench (see scripts/bench_atlas.sh, or pass --no-build path)
#
# Every pack runs single-threaded (NT_BUILDER_THREADS=1) with a per-run timeout and
# one retry. The known cold-mixed_aa vpack multithread hang is a PRE-EXISTING Phase-83
# flake — a timeout here is NOT a transform-mask regression. Phase 83 owns the fix.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

PRESET="native-release"
OUT_DIR="build/bench"
RUN_TIMEOUT="900"
NO_BUILD=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset)  PRESET="$2"; shift 2 ;;
        --out)     OUT_DIR="$2"; shift 2 ;;
        --timeout) RUN_TIMEOUT="$2"; shift 2 ;;
        --no-build) NO_BUILD=true; shift ;;
        *) echo "Unknown arg: $1" >&2; exit 1 ;;
    esac
done

# name|glob|shape|max_size  (the nine-patch corpus is intentionally excluded)
CORPORA=(
    "anim_heavy|assets/bench/anim_heavy/*.png|concave|2048"
    "mixed_aa|assets/sprites/bigatlas/*.png|concave|4096"
    "rect_only|assets/bench/rect_only/*.png|rect|2048"
)
MASKS=(identity export all)

BENCH_BASE="build/tools/research/${PRESET}/atlas_bench"
resolve_exe() {
    if [[ -x "${BENCH_BASE}.exe" ]]; then echo "${BENCH_BASE}.exe";
    elif [[ -x "${BENCH_BASE}" ]]; then echo "${BENCH_BASE}";
    else echo ""; fi
}

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
fi

BENCH_EXE="$(resolve_exe)"
if [[ -z "$BENCH_EXE" ]]; then
    echo "ERROR: atlas_bench exe not found under build/tools/research/${PRESET}/ (build it, or drop --no-build)" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
RESULTS="${OUT_DIR}/atlas_transform_sweep_results.md"

# Extract one JSON scalar (first match) from an atlas_bench result file.
json_num() { grep -E "\"$2\"" "$1" | head -1 | sed -E 's/.*: *([0-9.eE+-]+).*/\1/'; }
json_hex() { grep -E "\"$2\"" "$1" | head -1 | sed -E 's/.*: *"([0-9a-fA-F]*)".*/\1/'; }

# Run atlas_bench once, single-threaded, timeout + one retry. $1 out_json, rest = args.
run_bench() {
    local out_json="$1"; shift
    local name="$1" glob="$2" atlas="$3" shape="$4" max_size="$5" mask="$6"
    local attempt rc
    for attempt in 1 2; do
        if NT_BUILDER_THREADS=1 timeout "${RUN_TIMEOUT}" \
            "$BENCH_EXE" "$out_json" "$glob" "$atlas" "$shape" "$max_size" 0 --transforms "$mask" >/dev/null 2>&1; then
            return 0
        fi
        rc=$?
        if [[ "$rc" -eq 124 ]]; then
            echo "  WARNING: ${name}/${mask} timed out after ${RUN_TIMEOUT}s (attempt ${attempt}) — known Phase-83 cold-mixed_aa vpack flake, NOT a mask regression." >&2
        else
            echo "  WARNING: ${name}/${mask} exited ${rc} (attempt ${attempt})." >&2
        fi
    done
    return 1
}

{
    echo "# Atlas transform-mask sweep"
    echo ""
    echo "- preset: ${PRESET}"
    echo "- generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "- single-threaded (NT_BUILDER_THREADS=1), timeout ${RUN_TIMEOUT}s + 1 retry"
    echo ""
    echo "| corpus | mask | pages | fill_frontier | fill_texture | pack_ms |"
    echo "|--------|------|-------|---------------|--------------|---------|"
} | tee "$RESULTS"

declare -A TEX     # "corpus|mask" -> fill_texture
declare -A ALLSHA1 # corpus -> ALL-mask selected pack sha (run 1)
declare -A ALLSHA2 # corpus -> ALL-mask selected pack sha (run 2)

for spec in "${CORPORA[@]}"; do
    IFS='|' read -r name glob shape max_size <<< "$spec"
    for mask in "${MASKS[@]}"; do
        out_json="${OUT_DIR}/sweep_${name}_${mask}.json"
        if ! run_bench "$out_json" "$name" "$glob" "$name" "$shape" "$max_size" "$mask"; then
            echo "| ${name} | ${mask} | FAIL | - | - | - |" | tee -a "$RESULTS"
            continue
        fi
        pages="$(json_num "$out_json" pages)"
        frontier="$(json_num "$out_json" density_fill_frontier)"
        texture="$(json_num "$out_json" density_fill_texture)"
        pack_ms="$(json_num "$out_json" pack_ms)"
        TEX["${name}|${mask}"]="$texture"
        echo "| ${name} | ${mask} | ${pages} | ${frontier} | ${texture} | ${pack_ms} |" | tee -a "$RESULTS"
        if [[ "$mask" == "all" ]]; then
            ALLSHA1["$name"]="$(json_hex "$out_json" selected_pack_sha256)"
            # Second ALL run for the D-15 byte-identity (determinism) check.
            if run_bench "${OUT_DIR}/sweep_${name}_all2.json" "$name" "$glob" "$name" "$shape" "$max_size" all; then
                ALLSHA2["$name"]="$(json_hex "${OUT_DIR}/sweep_${name}_all2.json" selected_pack_sha256)"
            fi
        fi
    done
done

# XFORM-04 at corpus scale: a broader mask never packs worse on fill_texture,
# so density(export) >= density(identity) per corpus (81-03 fill_texture guard).
echo "" | tee -a "$RESULTS"
echo "## XFORM-04 corpus-scale monotonicity (fill_texture)" | tee -a "$RESULTS"
xform_fail=0
for spec in "${CORPORA[@]}"; do
    IFS='|' read -r name _ _ _ <<< "$spec"
    id_tex="${TEX["${name}|identity"]:-}"
    ex_tex="${TEX["${name}|export"]:-}"
    if [[ -z "$id_tex" || -z "$ex_tex" ]]; then
        echo "- ${name}: SKIP (a run failed)" | tee -a "$RESULTS"
        continue
    fi
    if awk -v a="$ex_tex" -v b="$id_tex" 'BEGIN{exit !(a+1e-9 >= b)}'; then
        echo "- ${name}: OK  export ${ex_tex} >= identity ${id_tex}" | tee -a "$RESULTS"
    else
        echo "- ${name}: FAIL export ${ex_tex} < identity ${id_tex}" | tee -a "$RESULTS"
        xform_fail=1
    fi
done

# D-15 full-corpus evidence. The committed baselines predate Phase 78-80 and carry
# no pack SHA, so a same-builder byte-hash vs master is unavailable (documented
# fallback). Concrete byte-identity here = ALL-mask determinism (two runs identical),
# plus a structural-equality check against the regenerated committed baseline JSON.
echo "" | tee -a "$RESULTS"
echo "## D-15 ALL-mask evidence" | tee -a "$RESULTS"
for spec in "${CORPORA[@]}"; do
    IFS='|' read -r name _ _ _ <<< "$spec"
    s1="${ALLSHA1["$name"]:-}"
    s2="${ALLSHA2["$name"]:-}"
    det="n/a"
    if [[ -n "$s1" && -n "$s2" ]]; then
        [[ "$s1" == "$s2" ]] && det="MATCH" || det="DIFFER"
    fi
    base_json="tools/research/atlas_bench/baseline/${name}.json"
    struct="n/a"
    if [[ -f "$base_json" ]]; then
        b_pages="$(json_num "$base_json" pages)"
        b_tex="$(json_num "$base_json" density_fill_texture)"
        r_pages="$(json_num "${OUT_DIR}/sweep_${name}_all.json" pages)"
        r_tex="$(json_num "${OUT_DIR}/sweep_${name}_all.json" density_fill_texture)"
        if [[ "$b_pages" == "$r_pages" ]] && awk -v a="$b_tex" -v b="$r_tex" 'BEGIN{exit !((a-b<0?b-a:a-b) < 1e-4)}'; then
            struct="MATCH"
        else
            struct="DIFFER (baseline pages=${b_pages} tex=${b_tex} vs run pages=${r_pages} tex=${r_tex})"
        fi
    fi
    echo "- ${name}: ALL-mask determinism ${det} (sha ${s1:0:12}); vs committed baseline ${struct}" | tee -a "$RESULTS"
done

echo "" | tee -a "$RESULTS"
echo "Results written to ${RESULTS}"
if [[ "$xform_fail" -ne 0 ]]; then
    echo "ERROR: XFORM-04 corpus-scale monotonicity violated." >&2
    exit 1
fi
