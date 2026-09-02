#!/usr/bin/env bash

print_ctest_failures() {
    local log="$1"
    sed -n '/^The following tests FAILED:/,/^Errors while running CTest/p' "$log" | sed 's/^/  /'
}
