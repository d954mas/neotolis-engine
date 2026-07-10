#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURE=$(mktemp)
trap 'rm -f "$FIXTURE"' EXIT

cat > "$FIXTURE" <<'EOF'
add_executable(game main.c)
target_link_libraries(game PRIVATE common)
if(EMSCRIPTEN)
    target_link_libraries(game PRIVATE web)
else()
    target_link_libraries(game PRIVATE native)
endif()
if(NOT EMSCRIPTEN)
    target_link_libraries(game PRIVATE native_extra)
endif()
target_link_libraries(builder PRIVATE builder_only)
EOF

web=$(bash "$SCRIPT_DIR/cmake_linked_libraries.sh" "$FIXTURE" game EMSCRIPTEN)
native=$(bash "$SCRIPT_DIR/cmake_linked_libraries.sh" "$FIXTURE" game NATIVE)
auto_web=$(bash "$SCRIPT_DIR/cmake_linked_libraries.sh" "$FIXTURE" --first-executable EMSCRIPTEN)

if [ "$web" != $'common\nweb' ]; then
    echo "cmake linked-library parser returned wrong EMSCRIPTEN libraries:" >&2
    printf '%s\n' "$web" >&2
    exit 1
fi
if [ "$native" != $'common\nnative\nnative_extra' ]; then
    echo "cmake linked-library parser returned wrong native libraries:" >&2
    printf '%s\n' "$native" >&2
    exit 1
fi
if [ "$auto_web" != $'common\nweb' ]; then
    echo "cmake linked-library parser returned wrong first executable libraries:" >&2
    printf '%s\n' "$auto_web" >&2
    exit 1
fi

echo "cmake linked-library parser: OK"
