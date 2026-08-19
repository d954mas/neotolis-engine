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
include(cmake/nt_example_packs.cmake)

if(EMSCRIPTEN AND PROJECT_IS_TOP_LEVEL)
    set(_nt_default_em_cache "${NT_ENGINE_ROOT}/build/emscripten-cache")
    if(DEFINED ENV{EM_CACHE} AND NOT "$ENV{EM_CACHE}" STREQUAL "")
        set(_nt_default_em_cache "$ENV{EM_CACHE}")
    endif()
    set(NT_EMSCRIPTEN_CACHE_DIR "${_nt_default_em_cache}" CACHE PATH "Emscripten cache directory for this engine checkout.")
    file(MAKE_DIRECTORY "${NT_EMSCRIPTEN_CACHE_DIR}")

    # Build invocations do not inherit the environment used during CMake configure.
    set(_nt_emcache_prefix "${CMAKE_COMMAND}" -E env "EM_CACHE=${NT_EMSCRIPTEN_CACHE_DIR}")
    set(CMAKE_C_COMPILER_LAUNCHER ${_nt_emcache_prefix} ${CMAKE_C_COMPILER_LAUNCHER})
    set(CMAKE_C_LINKER_LAUNCHER ${_nt_emcache_prefix} ${CMAKE_C_LINKER_LAUNCHER})
endif()

option(NT_WASM_ENABLE_SIMD "Build WASM targets with -msimd128 enabled." OFF)
set(NT_WASM_SIMD_WASM_PATH "" CACHE STRING "Alternate SIMD wasm path injected into the HTML shell.")

# Escape hatch for expensive pack builds (e.g. a future PR-CI lane without sponza);
# default builds everything. Exes still build, packs copy from a prior native run if present.
set(NT_SKIP_EXAMPLE_PACKS "" CACHE STRING "Examples whose asset-pack generation is skipped (see cmake/nt_example_packs.cmake).")

# OFF lets an exe that embeds both builder and runtime pick its own CRT (see nt_set_static_crt).
option(NT_STATIC_CRT "Pin the static release CRT on Windows targets; OFF inherits the embedding project's CRT." ON)

# ON exports NvOptimusEnablement/AmdPowerXpressRequestHighPerformance from the exe so hybrid-GPU
# Windows laptops pick the discrete GPU; OFF for battery-friendly games/tools. Build-time only,
# no runtime engine code — the user's per-app Windows graphics preference still overrides it.
option(NT_HYBRID_HPG "Prefer the discrete GPU on hybrid-GPU Windows systems." ON)

# Web counterpart to the native KHR_debug callback. ONE flag enables Emscripten GL_ASSERTIONS (validate
# GL call params) + GL_DEBUG (log every GL call). OFF by default EVEN in debug: GL_DEBUG logs every GL
# call, so it is opt-in for active GL bug-hunts only. Inert on native builds (warned below).
option(NT_GFX_WEB_GL_DEBUG "Enable Emscripten GL_ASSERTIONS + GL_DEBUG on WASM builds." OFF)
if(NT_GFX_WEB_GL_DEBUG AND NOT EMSCRIPTEN)
    message(WARNING "NT_GFX_WEB_GL_DEBUG=ON has no effect on a native build — it only adds Emscripten "
        "GL_ASSERTIONS/GL_DEBUG to WASM link lines. Native GL errors go through the KHR_debug callback "
        "(debug builds, always on, no flag).")
endif()

# Inspector + hit-zone overlay; OFF strips ~30 KB binary and ~9 KB ctx state.
# The 0/1 define is attached PUBLIC to the nt_ui target inside engine/ui/CMakeLists.txt
# so external consumers see the same value via target_link_libraries.
option(NT_UI_DEBUG_TOOLS "Build nt_ui inspector + debug overlay subsystem." OFF)
# OFF compiles real no-op bodies (zero release footprint); default tracks NT_UI_DEBUG_TOOLS so the CI OFF mirror exercises the stub path.
option(NT_LOG_RING_ENABLED "Build the dev-only nt_log_ring log-tail store." ${NT_UI_DEBUG_TOOLS})
# Independent of NT_DEVAPI_ENABLED. OFF compiles real no-op bodies; default tracks NT_UI_DEBUG_TOOLS for the CI OFF mirror.
option(NT_METRICS_ENABLED "Build the dev-only nt_metrics perf collector." ${NT_UI_DEBUG_TOOLS})
# OFF compiles real no-op bodies; default tracks NT_UI_DEBUG_TOOLS for the CI OFF mirror. Set build-wide below so component describe()/registration agree without linking the module.
option(NT_INTROSPECT_ENABLED "Build the dev-only entity introspection facility." ${NT_UI_DEBUG_TOOLS})
# Set OFF for a read-only deployment tier; the devapi entity-write group hard-requires it. Build-wide below so component apply()/registration agree without linking introspect.
option(NT_INTROSPECT_WRITE_ENABLED "Build the dev-only component write (apply) hooks." ${NT_INTROSPECT_ENABLED})
# Master gate for the devapi command layer (engine/devapi). OFF excludes the
# subdir entirely so release binaries contain zero devapi object code.
option(NT_DEVAPI_ENABLED "Build the devapi command layer (engine/devapi)." OFF)
# Defined BEFORE the engine subdirs so engine/input can derive its automation surface (input
# configures before devapi). Each group flag gates a whole group; see engine/devapi/CMakeLists.txt.
option(NT_DEVAPI_GROUP_INPUT "Build the devapi input.* group (synthetic input injection)." ON)
option(NT_DEVAPI_GROUP_CORE "Build the devapi core group (ping / engine.info / view)." ON)
option(NT_DEVAPI_GROUP_TIME "Build the devapi time.*/render.*/frame.* group." ON)
option(NT_DEVAPI_GROUP_DISCOVERY "Build the devapi discovery group (endpoints / command.describe / features)." ON)
# The ui group reads nt_ui_probe_collect (gated by NT_UI_DEBUG_TOOLS) and delegates writes to the input
# scheduler (NT_DEVAPI_GROUP_INPUT). Default OFF (opt-in, "use only what you need") so a plain
# -DNT_DEVAPI_ENABLED build stays lean and does not pull in UI debug tools; enable it explicitly,
# together with NT_UI_DEBUG_TOOLS + NT_DEVAPI_GROUP_INPUT (a hard CMake guard enforces both).
option(NT_DEVAPI_GROUP_UI "Build the devapi ui.* group (UI tree extraction over the nt_ui probe)." OFF)
# Default OFF (opt-in); enable with NT_METRICS_ENABLED + NT_LOG_RING_ENABLED so the sources it reads carry real bodies (a hard CMake guard enforces it).
option(NT_DEVAPI_GROUP_OBS "Build the devapi obs group (log.*/perf.*/entity.*/resource.* reads)." OFF)
# Default OFF so a deployment can expose read-only obs WITHOUT mutation. Hard-requires NT_INTROSPECT_WRITE_ENABLED.
option(NT_DEVAPI_GROUP_ENTITY_WRITE "Build the devapi entity.set component-write group." OFF)
# Default OFF (opt-in); the first DEFERRED data-returning group — links nt_gfx (readback) + nt_fpng (PNG encode).
option(NT_DEVAPI_GROUP_CAPTURE "Build the devapi capture.frame/capture.region group (PNG framebuffer capture)." OFF)

# Group flags are inert when NT_DEVAPI_ENABLED is OFF (subdir not built). Warn, don't error.
if(NOT NT_DEVAPI_ENABLED)
    foreach(_grp CORE TIME INPUT DISCOVERY UI OBS ENTITY_WRITE CAPTURE)
        if(NT_DEVAPI_GROUP_${_grp})
            message(WARNING "NT_DEVAPI_GROUP_${_grp}=ON has no effect: NT_DEVAPI_ENABLED is OFF, so "
                "the devapi command layer (and its groups) is not built. Set NT_DEVAPI_ENABLED=ON to "
                "build the devapi groups.")
        endif()
    endforeach()
endif()

set(NT_ASSERT_MODE "" CACHE STRING "Assert mode override: 0=off (unsupported), 1=trap, 2=full. Empty=auto (debug->2 full, release->1 trap).")
if(NOT NT_ASSERT_MODE STREQUAL "")
    add_compile_definitions(NT_ASSERT_MODE=${NT_ASSERT_MODE})
endif()

# Build-wide so a component's gated describe()/registration and the nt_introspect TU agree on the value
# without the component linking the module (it calls the sink only through the passed vtable pointer).
add_compile_definitions(NT_INTROSPECT_ENABLED=$<BOOL:${NT_INTROSPECT_ENABLED}>)
add_compile_definitions(NT_INTROSPECT_WRITE_ENABLED=$<BOOL:${NT_INTROSPECT_WRITE_ENABLED}>)

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

    # Opt-in web GL debug (NT_GFX_WEB_GL_DEBUG): link-time GL glue validation + per-call logging.
    if(NT_GFX_WEB_GL_DEBUG)
        add_link_options(-sGL_ASSERTIONS=1 -sGL_DEBUG=1)
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
# cJSON is pure C and must link on all 4 presets — NOT inside the
# if(NOT EMSCRIPTEN) block. Reusable by games independent of devapi.
add_subdirectory(deps/cjson EXCLUDE_FROM_ALL)

# Desktop dependencies: GLFW windowing + glad GL loader (not needed for WASM)
if(NOT EMSCRIPTEN)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_USE_HYBRID_HPG ${NT_HYBRID_HPG} CACHE BOOL "" FORCE)
    # The GPU hint symbols must live in the exe's export table — in a glfw DLL Windows
    # ignores them. Directory-scoped set (no CACHE FORCE): outside the hint's contract
    # the consumer's BUILD_SHARED_LIBS / GLFW_LIBRARY_TYPE choice stays untouched.
    # Pre-create the cache entry so glfw's own set(CACHE) never persists our STATIC
    # override — otherwise an ON configure would stick after switching to OFF.
    set(GLFW_LIBRARY_TYPE "${GLFW_LIBRARY_TYPE}" CACHE STRING "GLFW library type override")
    if(WIN32 AND NT_HYBRID_HPG)
        set(GLFW_LIBRARY_TYPE "STATIC")
    endif()
    add_subdirectory(deps/glfw EXCLUDE_FROM_ALL)
    add_subdirectory(deps/glad EXCLUDE_FROM_ALL)
    add_subdirectory(deps/cgltf EXCLUDE_FROM_ALL)
    add_subdirectory(deps/stb EXCLUDE_FROM_ALL)
    add_subdirectory(deps/mikktspace EXCLUDE_FROM_ALL)
    add_subdirectory(deps/miniz EXCLUDE_FROM_ALL)

    # GLFW calls CRT functions (assert, sscanf, powf, wcscpy) that resolve to
    # __declspec(dllimport) stubs under the default -D_DLL flag — missing with a static UCRT.
    nt_set_static_crt(glfw)
endif()

# Tests build only at top level. NT_BUILDING_TESTS gates the test targets; it does not touch the
# nt_input automation surface, so production nt_input stays lean under a devapi-OFF build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(NT_BUILDING_TESTS ON)
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
    add_subdirectory(examples/rtt_showcase)
    add_subdirectory(examples/sponza)
    add_subdirectory(examples/text)
    add_subdirectory(examples/atlas)
    add_subdirectory(examples/bunnymark)
    add_subdirectory(examples/slice9_demo)
    add_subdirectory(examples/ui_3d_demo)
    add_subdirectory(examples/ui_showcase)
    # A typo in the skip list would silently run the multi-hour build it tried to avoid.
    get_property(_nt_pack_names GLOBAL PROPERTY NT_EXAMPLE_PACK_NAMES)
    foreach(_nt_skip ${NT_SKIP_EXAMPLE_PACKS})
        if(NOT "${_nt_skip}" IN_LIST _nt_pack_names)
            message(FATAL_ERROR "NT_SKIP_EXAMPLE_PACKS: unknown example '${_nt_skip}' (known: ${_nt_pack_names})")
        endif()
    endforeach()
    # Minimal standalone web smoke-test app (window.__nt hooks the browser specs drive).
    add_subdirectory(tests/browser/app)
    # devapi host links nt_devapi via the transport split: native nt_devapi_net (TCP) OR web
    # nt_devapi_web (the push/pull ccall bridge, no socket). Builds on BOTH native and EMSCRIPTEN now —
    # the web target is the Playwright web-smoke gate. Gate only on the layer being enabled.
    if(NT_DEVAPI_ENABLED)
        add_subdirectory(examples/devapi_host)
        # capture_host needs the deferred-data capture group + a real GL context (the live capture UAT).
        # It links only the devapi group libs it uses (capture + core + discovery), never the
        # nt_ui/obs/entity chains. Native-only: it wires the TCP transport unconditionally. Gate on all
        # three groups so a config that disables any of them skips the host cleanly instead of
        # link-failing on a missing group lib.
        if(NOT EMSCRIPTEN AND NT_DEVAPI_GROUP_CAPTURE AND NT_DEVAPI_GROUP_CORE AND NT_DEVAPI_GROUP_DISCOVERY)
            add_subdirectory(examples/capture_host)
        endif()
    endif()
    if(NOT EMSCRIPTEN)
        # Declared before tests/ so NT_TEST_ACCESS_LIBS can gate the builder's test surface.
        add_subdirectory(tools/builder)
        add_subdirectory(tools/research/hash_benchmark)
        add_subdirectory(tools/research/atlas_bench)
    endif()
    add_subdirectory(tests)
endif()
