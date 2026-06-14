cmake_minimum_required(VERSION 3.25)
project(neotolis-engine LANGUAGES C)

# Engine root — use instead of CMAKE_SOURCE_DIR for paths to deps/
# (CMAKE_SOURCE_DIR points to the top-level project, not engine root when used as submodule)
set(NT_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Engine source root" FORCE)

# Warning and sanitizer flag functions (applied per-target, not globally)
include(cmake/warnings.cmake)
include(cmake/test_target.cmake)
include(cmake/nt_module.cmake)
include(cmake/nt_shell.cmake)

option(NT_WASM_ENABLE_SIMD "Build WASM targets with -msimd128 enabled." OFF)
set(NT_WASM_SIMD_WASM_PATH "" CACHE STRING "Alternate SIMD wasm path injected into the HTML shell.")

# Inspector + hit-zone overlay; OFF strips ~30 KB binary and ~9 KB ctx state.
# The 0/1 define is attached PUBLIC to the nt_ui target inside engine/ui/CMakeLists.txt
# so external consumers see the same value via target_link_libraries.
option(NT_UI_DEBUG_TOOLS "Build nt_ui inspector + debug overlay subsystem." OFF)
# Master gate for the devapi command layer (engine/devapi). OFF excludes the
# subdir entirely so release binaries contain zero devapi object code.
option(NT_DEVAPI_ENABLED "Build the devapi command layer (engine/devapi)." OFF)
set(NT_ASSERT_MODE "" CACHE STRING "Assert mode override: 0=off, 1=trap, 2=full. Empty=auto (debug->2 full, release->1 trap; OFF only via explicit override for final production).")
if(NOT NT_ASSERT_MODE STREQUAL "")
    add_compile_definitions(NT_ASSERT_MODE=${NT_ASSERT_MODE})
endif()

if(NT_WASM_ENABLE_SIMD AND NT_WASM_SIMD_WASM_PATH)
    message(FATAL_ERROR "NT_WASM_ENABLE_SIMD and NT_WASM_SIMD_WASM_PATH cannot be enabled together. Use a paired preset for baseline+SIMD output or a standalone SIMD preset.")
endif()

if(NT_WASM_SIMD_WASM_PATH AND NOT NT_WASM_ENABLE_SIMD)
    message(WARNING "NT_WASM_SIMD_WASM_PATH is set to \"${NT_WASM_SIMD_WASM_PATH}\". "
        "The HTML shell will reference this file, but this build does not produce it. "
        "Use scripts/build_wasm_paired.sh or build the matching SIMD preset and run "
        "scripts/package_wasm_simd.sh to place the SIMD wasm beside the baseline output.")
endif()

# Emscripten optimizations
if(EMSCRIPTEN)
    if(NT_WASM_ENABLE_SIMD)
        add_compile_options(-msimd128)
        add_link_options(-msimd128)
    endif()

    # emmalloc: compact allocator, ~4 KB smaller than default dlmalloc
    add_link_options(-sMALLOC=emmalloc)
    # Closure Compiler minifies JS glue code (~60% reduction)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_link_options(--closure 1)
    endif()
endif()

# Output-layout name (build/engine|tests|examples/<name>) follows the BINARY DIR name,
# so every configuration is isolated by construction — two Debug variants (e.g. the CI
# NT_UI_DEBUG_TOOLS=OFF mirror) can never clobber each other's artifacts. All presets
# configure into build/_cmake/<preset>, so preset users see identical paths as before.
# Overridable (-DNT_PRESET_NAME=...) for non-standard binary dirs.
if(NOT NT_PRESET_NAME)
    get_filename_component(NT_PRESET_NAME "${CMAKE_BINARY_DIR}" NAME)
endif()

# Vendored dependencies (committed in deps/, always available)
# Must come before engine so targets like cglm_headers are visible.
add_subdirectory(deps/cglm EXCLUDE_FROM_ALL)
add_subdirectory(deps/unity EXCLUDE_FROM_ALL)
# cJSON is pure C and must link on all 4 presets (PROTO-01) — NOT inside the
# if(NOT EMSCRIPTEN) block. Reusable by games independent of devapi.
add_subdirectory(deps/cjson EXCLUDE_FROM_ALL)

# Desktop dependencies: GLFW windowing + glad GL loader (not needed for WASM)
if(NOT EMSCRIPTEN)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(deps/glfw EXCLUDE_FROM_ALL)
    add_subdirectory(deps/glad EXCLUDE_FROM_ALL)
    add_subdirectory(deps/cgltf EXCLUDE_FROM_ALL)
    add_subdirectory(deps/stb EXCLUDE_FROM_ALL)
    add_subdirectory(deps/mikktspace EXCLUDE_FROM_ALL)
    add_subdirectory(deps/miniz EXCLUDE_FROM_ALL)

    # GLFW uses CRT functions (assert, sscanf, powf, wcscpy) that resolve to
    # __declspec(dllimport) stubs under the default -D_DLL flag. Since we link
    # static UCRT, those dllimport thunks are missing. Remove _DLL so GLFW
    # calls the static CRT symbols directly.
    target_compile_options(glfw PRIVATE -U_DLL)
endif()

# Core libraries (always available)
add_subdirectory(shared)
add_subdirectory(engine)

# Top-level only targets (not included when engine is used as subdirectory)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()

    add_subdirectory(examples/hello)
    add_subdirectory(examples/bench_shapes)
    add_subdirectory(examples/spinning_cube)
    add_subdirectory(examples/textured_quad)
    add_subdirectory(examples/sponza)
    add_subdirectory(examples/text)
    add_subdirectory(examples/atlas)
    add_subdirectory(examples/bunnymark)
    add_subdirectory(examples/slice9_demo)
    add_subdirectory(examples/ui_3d_demo)
    add_subdirectory(examples/ui_showcase)
    add_subdirectory(tests)

    if(NOT EMSCRIPTEN)
        add_subdirectory(tools/builder)
        add_subdirectory(tools/research/hash_benchmark)

    endif()
endif()
