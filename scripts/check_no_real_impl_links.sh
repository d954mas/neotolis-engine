#!/usr/bin/env bash
# Gate: no engine module may link a REAL swappable impl (re-poisons the link
# graph and makes stubs unreachable -- the module-composition anti-pattern). Modules
# link the header-only nt_X_interface; the executable picks exactly one impl.
#
# All swappable pairs are decoupled: the keystone nt_log plus the rest.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Swappable real impl targets that engine modules must NEVER link directly. Derived from the
# nt_declare_interface(...) call sites (every interface module is swappable by definition) so a new
# module is auto-covered without editing this gate. Empty = derivation broke -> fail loud, don't pass vacuously.
mapfile -t SWAPPABLE < <(
    find "$ROOT_DIR/engine" -name CMakeLists.txt -exec grep -hoE 'nt_declare_interface\([[:space:]]*[A-Za-z0-9_]+' {} + |
        sed -E 's/.*nt_declare_interface\([[:space:]]*//' | sort -u
)
if [ "${#SWAPPABLE[@]}" -eq 0 ]; then
    echo "check_no_real_impl_links: ERROR — found no nt_declare_interface() targets (derivation broken)" >&2
    exit 1
fi

# Single awk pass over every CMakeLists: collapse each target_link_libraries(...)
# call to one logical line (depth-balanced paren tracking -- a line-by-line read
# silently skips continuation lines, the bug this guards), strip the link TARGET
# (a module naming its own impl is legitimate; only the DEPENDENCY list is checked),
# then match bare swappable tokens in-process. One awk spawn total: the previous
# per-call-per-impl grep loop cost ~22 s in Windows process spawns alone.
IMPL_ALT="$(printf '%s|' "${SWAPPABLE[@]}")"
IMPL_ALT="${IMPL_ALT%|}"
FAIL=0
VIOLATIONS="$(find "$ROOT_DIR/engine" -name CMakeLists.txt -print0 | xargs -0 awk -v impls="$IMPL_ALT" '
    FNR == 1 { in_call = 0 }
    {
        line = $0
        sub(/#.*/, "", line)                 # strip trailing comment
    }
    !in_call {
        if (line ~ /target_link_libraries[[:space:]]*\(/) {
            in_call = 1
            start = FNR
            acc = ""
            depth = 0
        } else {
            next
        }
    }
    in_call {
        gsub(/^[[:space:]]+/, "", line)
        acc = (acc == "" ? line : acc " " line)
        n_open = gsub(/\(/, "(", line)
        n_close = gsub(/\)/, ")", line)
        depth += n_open - n_close
        if (depth <= 0) {
            gsub(/[[:space:]]+/, " ", acc)
            deps = acc
            sub(/.*target_link_libraries[[:space:]]*\([[:space:]]*[A-Za-z0-9_:]+/, "", deps)
            n = split(deps, toks, /[^A-Za-z0-9_]+/)
            for (i = 1; i <= n; i++) {
                if (toks[i] != "" && toks[i] ~ ("^(" impls ")$")) {
                    printf "ERROR: %s:%d links real impl %s (use %s_interface):\n    %s\n", FILENAME, start, toks[i], toks[i], acc
                }
            }
            in_call = 0
        }
    }
')"
if [ -n "$VIOLATIONS" ]; then
    printf '%s\n' "$VIOLATIONS"
    FAIL=1
fi

if [ "$FAIL" -ne 0 ]; then
    echo "check_no_real_impl_links: FAILED"
    exit 1
fi
echo "check_no_real_impl_links: passed (no engine module links a real swappable impl)"
