#!/usr/bin/env bash
# Gate: no engine module may link a REAL swappable impl (re-poisons the link
# graph and makes stubs unreachable -- the #126 anti-pattern). Modules link the
# header-only nt_X_interface; the executable picks exactly one impl.
#
# Phase 61-01 scopes this to nt_log (the keystone). 61-02 decouples the other 6
# pairs and extends SWAPPABLE below to: nt_input nt_http nt_gfx nt_window nt_app nt_fs.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Swappable real impl targets that engine modules must NEVER link directly.
SWAPPABLE=(nt_log)

FAIL=0
while IFS= read -r cmake; do
    # The file that DEFINES the impl (engine/log/CMakeLists.txt) legitimately
    # names nt_log to build the target itself -- skip the defining module dir.
    while IFS= read -r entry; do
        file="${entry%%:*}"
        rest="${entry#*:}"
        lineno="${rest%%:*}"
        text="${rest#*:}"
        case "$text" in
            *target_link_libraries*)
                for impl in "${SWAPPABLE[@]}"; do
                    # The defining module's own CMakeLists builds the impl -- allow it.
                    case "$file" in */"${impl#nt_}"/CMakeLists.txt) continue ;; esac
                    # Match the bare token only: nt_log, not nt_log_interface / nt_log_stub.
                    if printf '%s' "$text" | grep -Eq "(^|[^A-Za-z0-9_])${impl}([^A-Za-z0-9_]|\$)"; then
                        echo "ERROR: $file:$lineno links real impl '$impl' (use ${impl}_interface):"
                        echo "    $text"
                        FAIL=1
                    fi
                done
                ;;
        esac
    done < <(grep -n -v '^[[:space:]]*#' "$cmake" | sed "s|^|$cmake:|")
done < <(find "$ROOT_DIR/engine" -name CMakeLists.txt)

if [ "$FAIL" -ne 0 ]; then
    echo "check_no_real_impl_links: FAILED"
    exit 1
fi
echo "check_no_real_impl_links: passed (no engine module links a real swappable impl)"
