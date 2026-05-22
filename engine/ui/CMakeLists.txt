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

# Clay vendored header -- PUBLIC include. Clay is part of the engine's
# public surface: nt_ui wraps lifecycle (context, walker, themes) but
# game code drives layout/widgets via CLAY_* macros directly. SYSTEM
# suppresses -Werror on Clay-internal warnings (matches engine/math/cglm).
# Clay's source is excluded from clang-tidy via scripts/tidy.sh. Use
# NT_ENGINE_ROOT so the include resolves when nt_ui is consumed from a
# parent CMake project.
target_include_directories(nt_ui SYSTEM PUBLIC
    $<BUILD_INTERFACE:${NT_ENGINE_ROOT}/deps/clay>
)

# Version pin propagated to the _Static_assert in nt_ui.c
target_compile_definitions(nt_ui PRIVATE
    CLAY_PINNED_MAJOR=${_clay_major}
    CLAY_PINNED_MINOR=${_clay_minor}
)

# PUBLIC: types referenced in nt_ui.h (handles + pointer state struct).
# PRIVATE: walker implementation deps (renderer + gfx + atlas).
#
# nt_stats is intentionally NOT a dependency. Per-walk metrics are
# exposed via nt_ui_get_last_walk_* getters; apps that want them in the
# debug overlay forward into nt_stats themselves. Direction-of-deps
# matches nt_gfx <- nt_stats: observability pulls from rendering, never
# the reverse.
target_link_libraries(nt_ui
    PUBLIC
        nt_core
        nt_resource
        nt_font
        nt_material
        nt_input
        nt_mem_scratch
    PRIVATE
        nt_gfx
        nt_sprite_renderer
        nt_text_renderer
        nt_atlas
)

# nt_log is PRIVATE-linked by nt_add_module via the LOG_DOMAIN injection.
