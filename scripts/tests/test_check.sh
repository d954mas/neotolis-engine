#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

test_wrapper_locks_before_format() {
    local fixture="$TMP_DIR/wrapper"
    mkdir -p "$fixture/scripts" "$fixture/build/.check.lock"
    cp "$ROOT_DIR/scripts/check.sh" "$fixture/scripts/check.sh"
    cp "$ROOT_DIR/scripts/format_and_check.sh" "$fixture/scripts/format_and_check.sh"
    printf '#!/usr/bin/env bash\ntouch "$(dirname "$0")/../format-ran"\n' > "$fixture/scripts/fmt.sh"

    local rc=0
    bash "$fixture/scripts/format_and_check.sh" > "$fixture/output.log" 2>&1 || rc=$?
    if [ "$rc" -ne 2 ]; then
        echo "FAIL: locked format_and_check.sh returned $rc, expected 2"
        return 1
    fi
    if [ -e "$fixture/format-ran" ]; then
        echo "FAIL: format_and_check.sh formatted before checking the lock"
        return 1
    fi
}

test_ctest_summary_includes_exit_code_failure() {
    # shellcheck source=../lib/check_output.sh
    source "$ROOT_DIR/scripts/lib/check_output.sh"

    local log="$TMP_DIR/ctest.log"
    printf '%s\n' \
        '125/133 Test #23: test_nt_gfx_render_target_native ......Exit code 0xc0000409' \
        '***Exception: 43.59 sec' \
        'The following tests FAILED:' \
        '  23 - test_nt_gfx_render_target_native (Exit code 0xc0000409' \
        ')' \
        'Errors while running CTest' > "$log"

    local output
    output="$(print_ctest_failures "$log")"
    if [[ "$output" != *"23 - test_nt_gfx_render_target_native"* ]]; then
        echo "FAIL: CTest summary omitted the exit-code failure"
        return 1
    fi
}

test_wrapper_locks_before_format
test_ctest_summary_includes_exit_code_failure
echo "check.sh behavior tests: PASS"
