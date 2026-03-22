cmake_minimum_required(VERSION 3.25)
project(neotolis-engine LANGUAGES C)

# Warning and sanitizer flag functions (applied per-target, not globally)
include(cmake/warnings.cmake)
include(cmake/nt_module.cmake)
include(cmake/nt_shell.cmake)

option(NT_WASM_ENABLE_SIMD "Build WASM targets with -msimd128 enabled." OFF)
set(NT_WASM_SIMD_WASM_PATH "" CACHE STRING "Alternate SIMD wasm path injected into the HTML shell.")

if(NT_WASM_ENABLE_SIMD AND NT_WASM_SIMD_WASM_PATH)
    message(FATAL_ERROR "NT_WASM_ENABLE_SIMD and NT_WASM_SIMD_WASM_PATH cannot be enabled together. Use a paired preset for baseline+SIMD output or a standalone SIMD preset.")
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

# Derive preset name from build type + platform for output directory layout
if(EMSCRIPTEN)
    if(NT_WASM_ANALYSIS)
        set(_nt_wasm_tier "analysis")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(_nt_wasm_tier "release")
    else()
        set(_nt_wasm_tier "debug")
    endif()

    set(_nt_wasm_mode_suffix "")
    if(NT_WASM_ENABLE_SIMD)
        set(_nt_wasm_mode_suffix "-simd")
    elseif(NT_WASM_SIMD_WASM_PATH)
        set(_nt_wasm_mode_suffix "-paired")
    endif()

    set(NT_PRESET_NAME "wasm-${_nt_wasm_tier}${_nt_wasm_mode_suffix}")
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
    add_subdirectory(tests)

    if(NOT EMSCRIPTEN)
        add_subdirectory(tools/builder)
        add_subdirectory(tools/research/hash_benchmark)
        add_subdirectory(examples/builder_demo)
    endif()
endif()
