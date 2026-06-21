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
# All sources unconditional. Inspector + hit-zone .c are guarded internally
# by `#if NT_UI_DEBUG_TOOLS` and compile to empty TUs when the option is off
# — keeps compile_commands.json complete so clang-tidy lints both configs.
# nt_ui_clay_impl.c owns CLAY_IMPLEMENTATION + every consumer of Clay
# private types — see file header.
set(NT_UI_SOURCES
    nt_ui.c
    nt_ui.h
    nt_ui_internal.h
    nt_ui_clay_impl.c
    nt_ui_clay_impl.h
    nt_ui_label.c
    nt_ui_label.h
    nt_ui_scale.c
    nt_ui_scale.h
    nt_ui_fit.c
    nt_ui_fit.h
    nt_ui_image.c
    nt_ui_image.h
    nt_ui_fill.c
    nt_ui_fill.h
    nt_ui_radial.c
    nt_ui_radial.h
    nt_ui_radial_image.c
    nt_ui_radial_image.h
    nt_ui_rich_text.c
    nt_ui_rich_text.h
    nt_ui_rich_fx.c
    nt_ui_rich_fx.h
    nt_ui_rich_tagset.c
    nt_ui_rich_tagset.h
    nt_ui_panel.c
    nt_ui_panel.h
    nt_ui_anim.c
    nt_ui_anim.h
    nt_ui_state.c
    nt_ui_state.h
    nt_ui_scroll.c
    nt_ui_scroll.h
    nt_ui_popup.c
    nt_ui_popup.h
    nt_ui_popup_internal.h
    nt_ui_modal.c
    nt_ui_modal.h
    nt_ui_menu.c
    nt_ui_menu.h
    nt_ui_dropdown.c
    nt_ui_dropdown.h
    nt_ui_tabbar.c
    nt_ui_tabbar.h
    nt_ui_tooltip.c
    nt_ui_tooltip.h
    nt_ui_button.c
    nt_ui_button.h
    nt_ui_checkbox.c
    nt_ui_checkbox.h
    nt_ui_slider.c
    nt_ui_slider.h
    nt_ui_input.c
    nt_ui_input.h
    nt_ui_progress.c
    nt_ui_progress.h
    nt_ui_debug_hit_zones.c
    nt_ui_debug_hit_zones.h
    nt_ui_inspector.c
    nt_ui_inspector.h
)

# Header-only interface carries nt_ui.h's include root (the engine/ root resolves font/input/
# material/resource headers it pulls); a consumer that only needs the probe API decls (e.g. the
# devapi ui group, or a stub-linked test) depends on this rather than the heavy nt_ui impl chain.
nt_declare_interface(nt_ui)

nt_add_module(nt_ui LOG_DOMAIN "ui" ${NT_UI_SOURCES})

# Propagate to consumers via target_link_libraries — public headers use
# #if NT_UI_DEBUG_TOOLS. Always defined 0/1 to be -Wundef-safe. Attached to the interface too so
# the probe decl-vs-stub switch is identical whether a consumer links the impl or just the headers.
if(NT_UI_DEBUG_TOOLS)
    target_compile_definitions(nt_ui PUBLIC NT_UI_DEBUG_TOOLS=1)
    target_compile_definitions(nt_ui_interface INTERFACE NT_UI_DEBUG_TOOLS=1)
else()
    target_compile_definitions(nt_ui PUBLIC NT_UI_DEBUG_TOOLS=0)
    target_compile_definitions(nt_ui_interface INTERFACE NT_UI_DEBUG_TOOLS=0)
endif()
target_link_libraries(nt_ui PUBLIC nt_ui_interface)

# Stub impl for headless / stub-linked tests: the probe symbols the devapi ui group resolves
# (nt_ui_probe_collect / _owned / nt_ui_get_bbox / ...) without Clay or the renderer chain.
# Forces NT_UI_DEBUG_TOOLS=1 so the probe decls are in scope. Deliberately does NOT link
# nt_ui_interface: that carries the GLOBAL DEBUG_TOOLS value, which collides with the forced =1 under a
# DEBUG_TOOLS=OFF build (-Wmacro-redefined). nt_add_module already gives the engine-root include.
nt_add_module(nt_ui_stub LOG_DOMAIN "ui_stub"
    stub/nt_ui_stub.c
)
target_link_libraries(nt_ui_stub PUBLIC nt_core)
target_compile_definitions(nt_ui_stub PUBLIC NT_UI_DEBUG_TOOLS=1)

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
# nt_debug_overlay is intentionally NOT a dependency. Per-walk metrics are
# exposed via nt_ui_get_last_walk_* getters; apps that want them in the
# debug overlay forward into nt_debug_overlay themselves. Direction-of-deps
# matches nt_gfx <- nt_debug_overlay: observability pulls from rendering, never
# the reverse.
target_link_libraries(nt_ui
    PUBLIC
        nt_core
        nt_resource
        nt_font
        nt_material
        nt_input_interface
        nt_clipboard_interface
        nt_mem_scratch
        # PUBLIC: nt_atlas_region_ref_t appears in public widget headers
        # (nt_ui_button.h / nt_ui_image.h / nt_ui_panel.h).
        nt_atlas
    PRIVATE
        nt_gfx_interface
        nt_sprite_renderer
        nt_text_renderer
        nt_time
        nt_math
        nt_utf8
)

# nt_log is PRIVATE-linked by nt_add_module via the LOG_DOMAIN injection.
