#!/usr/bin/env bash
# Gate: no engine module may link a REAL swappable impl (re-poisons the link
# graph and makes stubs unreachable -- the #126 anti-pattern). Modules link the
# header-only nt_X_interface; the executable picks exactly one impl.
#
# All 7 swappable pairs are decoupled (61-01 keystone nt_log + 61-02 rest).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Swappable real impl targets that engine modules must NEVER link directly.
SWAPPABLE=(nt_log nt_input nt_http nt_gfx nt_window nt_app nt_fs nt_clipboard)

FAIL=0
while IFS= read -r cmake; do
    while IFS= read -r entry; do
        file="${entry%%:*}"
        rest="${entry#*:}"
        lineno="${rest%%:*}"
        text="${rest#*:}"
        case "$text" in
            *target_link_libraries*)
                # Strip the link TARGET (first token after the open paren) -- a module
                # building itself (e.g. target_link_libraries(nt_gfx PUBLIC ...) in
                # engine/graphics) legitimately names its own impl. Only the DEPENDENCY
                # list (after the target) is checked. This is dir-name agnostic, so the
                # gfx/graphics dir-name mismatch is handled without a special case.
                deps="$(printf '%s' "$text" | sed -E 's/.*target_link_libraries[[:space:]]*\([[:space:]]*[A-Za-z0-9_:]+//')"
                for impl in "${SWAPPABLE[@]}"; do
                    # Match the bare token only: nt_log, not nt_log_interface / nt_log_stub.
                    if printf '%s' "$deps" | grep -Eq "(^|[^A-Za-z0-9_])${impl}([^A-Za-z0-9_]|\$)"; then
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
