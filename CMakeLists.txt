cmake_minimum_required(VERSION 3.21)
project(neotolis-engine LANGUAGES C)

# Warning and sanitizer flag functions (applied per-target, not globally)
include(cmake/warnings.cmake)
include(cmake/nt_module.cmake)

# Derive preset name from build type + platform for output directory layout
if(EMSCRIPTEN)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
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

# Core libraries (always available)
add_subdirectory(shared)
add_subdirectory(engine)

# Vendored dependencies (committed in deps/, always available)
add_subdirectory(deps/cglm EXCLUDE_FROM_ALL)
add_subdirectory(deps/unity EXCLUDE_FROM_ALL)

# Top-level only targets (not included when engine is used as subdirectory)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()

    add_subdirectory(examples/hello)
    add_subdirectory(tests)

    if(NOT EMSCRIPTEN)
        add_subdirectory(tools/builder)
    endif()
endif()
