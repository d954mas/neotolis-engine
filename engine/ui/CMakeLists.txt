# nt_ui -- UI module (Phase 51 skeleton; Phase 52 fills public API)
#
# Vendors Clay v0.14 (deps/clay/clay.h, zlib license). The version is pinned
# via deps/clay/VERSION; CMake parses major/minor and feeds them to
# CLAY_PINNED_MAJOR/CLAY_PINNED_MINOR target_compile_definitions, which the
# _Static_assert in nt_ui.c checks against the expected pin (0.14).
#
# Per Drift 1 Option D (see .planning/phases/051-.../051-RESEARCH.md): the
# macros CLAY_VERSION_MAJOR/CLAY_VERSION_MINOR do NOT exist in Clay v0.14;
# we use our own CLAY_PINNED_* defined here as the single source of truth.

# --- Parse deps/clay/VERSION for major.minor ---
# NT_ENGINE_ROOT is set by the top-level CMakeLists.txt; this lets nt_ui be
# consumed as a submodule (where CMAKE_SOURCE_DIR points at the host project,
# not the engine root). See the comment near NT_ENGINE_ROOT in the top-level
# CMakeLists.txt.
set(_clay_version_file "${NT_ENGINE_ROOT}/deps/clay/VERSION")
if(NOT EXISTS "${_clay_version_file}")
    message(FATAL_ERROR
        "nt_ui: deps/clay/VERSION not found at ${_clay_version_file}. "
        "Vendor Clay v0.14 before building (see Phase 51 Plan 01)."
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

# --- Module declaration (skeleton; Phase 52 fills sources) ---
nt_add_module(nt_ui LOG_DOMAIN "ui"
    nt_ui.c
    nt_ui.h
)

# Clay vendored header -- PRIVATE include (only nt_ui.c sees clay.h).
# SYSTEM suppresses -Werror on Clay-internal warnings (matches engine/math/cglm
# precedent). Clay's source is excluded from clang-tidy via scripts/tidy.sh.
# Use NT_ENGINE_ROOT so the include resolves correctly when nt_ui is
# consumed from a parent CMake project (submodule scenario).
target_include_directories(nt_ui SYSTEM PRIVATE
    ${NT_ENGINE_ROOT}/deps/clay
)

# Version pin propagated to the _Static_assert in nt_ui.c
target_compile_definitions(nt_ui PRIVATE
    CLAY_PINNED_MAJOR=${_clay_major}
    CLAY_PINNED_MINOR=${_clay_minor}
)

# Phase 51 link surface is minimal; Phase 52 adds nt::gfx, nt::font,
# nt::text_renderer, nt::sprite_renderer, nt::input as PUBLIC.
# nt_log is PRIVATE-linked by nt_add_module via the LOG_DOMAIN injection.
