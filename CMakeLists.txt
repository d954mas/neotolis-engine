cmake_minimum_required(VERSION 3.25)
project(neotolis-engine LANGUAGES C)

# Warning and sanitizer flag functions (applied per-target, not globally)
include(cmake/warnings.cmake)
include(cmake/nt_module.cmake)
include(cmake/nt_shell.cmake)

# Derive preset name from build type + platform for output directory layout
if(EMSCRIPTEN)
    if(NT_WASM_ANALYSIS)
        set(NT_PRESET_NAME "wasm-analysis")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(NT_PRESET_NAME "wasm-release")
    else()
        set(NT_PRESET_NAME "wasm-debug")
    endif()
else()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(NT_PRESET_NAME "native-release")
    else()
        set(NT_PRESET_NAME "native-debug")
    endif()
endif()

# Vendored dependencies (committed in deps/, always available)
# Must come before engine so targets like cglm_headers are visible.
add_subdirectory(deps/cglm EXCLUDE_FROM_ALL)
add_subdirectory(deps/unity EXCLUDE_FROM_ALL)

# Core libraries (always available)
add_subdirectory(shared)
add_subdirectory(engine)

# Top-level only targets (not included when engine is used as subdirectory)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()

    add_subdirectory(examples/hello)
    add_subdirectory(examples/bench_shapes)
    add_subdirectory(examples/spinning_cube)
    add_subdirectory(tests)

    if(NOT EMSCRIPTEN)
        add_subdirectory(tools/builder)
    endif()
endif()
