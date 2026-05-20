# nt_ui -- UI module wrapping Clay v0.14.
#
# Version pinned via deps/clay/VERSION; CMake parses major/minor and feeds
# them to CLAY_PINNED_MAJOR/CLAY_PINNED_MINOR target_compile_definitions,
# which the _Static_assert in nt_ui.c checks against the expected pin.
# Clay v0.14 has no CLAY_VERSION_* macros of its own, hence CLAY_PINNED_*
# defined here as the single source of truth.

# --- Parse deps/clay/VERSION for major.minor ---
# NT_ENGINE_ROOT is set by the top-level CMakeLists.txt; this lets nt_ui be
# consumed as a submodule (where CMAKE_SOURCE_DIR points at the host project,
# not the engine root). See the comment near NT_ENGINE_ROOT in the top-level
# CMakeLists.txt.
set(_clay_version_file "${NT_ENGINE_ROOT}/deps/clay/VERSION")
if(NOT EXISTS "${_clay_version_file}")
    message(FATAL_ERROR
        "nt_ui: deps/clay/VERSION not found at ${_clay_version_file}. "
        "Vendor Clay v0.14 before building."
    )
endif()
file(STRINGS "${_clay_version_file}" _clay_version_lines LIMIT_COUNT 1)
list(GET _clay_version_lines 0 _clay_version_first_line)
string(STRIP "${_clay_version_first_line}" _clay_version_first_line)
if(NOT _clay_version_first_line MATCHES "^([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR
        "nt_ui: first line of deps/clay/VERSION must be \"<major>.<minor>\" "
        "(e.g. \"0.14\"), got: \"${_clay_version_first_line}\""
    )
endif()
set(_clay_major "${CMAKE_MATCH_1}")
set(_clay_minor "${CMAKE_MATCH_2}")

# --- Module declaration ---
nt_add_module(nt_ui LOG_DOMAIN "ui"
    nt_ui.c
    nt_ui.h
    nt_ui_internal.h
)

# Clay vendored header -- PRIVATE include (only nt_ui.c sees clay.h).
# SYSTEM suppresses -Werror on Clay-internal warnings (matches engine/math/cglm
# precedent). Clay's source is excluded from clang-tidy via scripts/tidy.sh.
# Use NT_ENGINE_ROOT so the include resolves correctly when nt_ui is
# consumed from a parent CMake project (submodule scenario).
#
# Phase 52 Task 2.2: nt_ui_internal.h includes "clay.h", and test TUs
# compiled with NT_UI_TEST_ACCESS pull that internal header in. The
# include directory therefore needs to be visible to consumers of nt_ui
# that opt into test access.
target_include_directories(nt_ui SYSTEM PRIVATE
    ${NT_ENGINE_ROOT}/deps/clay
)
target_include_directories(nt_ui SYSTEM INTERFACE
    $<BUILD_INTERFACE:${NT_ENGINE_ROOT}/deps/clay>
)

# Version pin propagated to the _Static_assert in nt_ui.c
target_compile_definitions(nt_ui PRIVATE
    CLAY_PINNED_MAJOR=${_clay_major}
    CLAY_PINNED_MINOR=${_clay_minor}
)

# Phase 52 Task 2.2: nt_ui needs nt_core (asserts/logging/types), nt_resource
# (handle types referenced in public headers), nt_font (font handle),
# nt_material (material handle), nt_input (pointer state struct).
#
# Phase 52 Plan 04: walker private deps. The walker translates Clay render
# commands into sprite/text renderer emits + gfx scissor/viewport state. These
# stay PRIVATE because nothing in nt_ui's public surface references them.
#
# Phase 52 Plan 05: nt_stats added for per-walk ui_draw_calls + ui_element_count
# user-counter wiring (WALK-09 / D-52-20). Stays PRIVATE because nt_ui's public
# API does not reference any nt_stats type.
target_link_libraries(nt_ui
    PUBLIC
        nt_core
        nt_resource
        nt_font
        nt_material
        nt_input
    PRIVATE
        nt_gfx
        nt_sprite_renderer
        nt_text_renderer
        nt_atlas
        nt_stats
)

# nt_log is PRIVATE-linked by nt_add_module via the LOG_DOMAIN injection.
