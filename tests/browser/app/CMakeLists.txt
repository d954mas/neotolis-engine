# browser_smoke -- minimal standalone web target the Playwright browser specs drive.
# Renders a Cyrillic input field + a rich-text block with one clickable <link> and exposes the
# window.__nt smoke-test hooks. Reuses two example packs (same generated asset ids); no own pack.

set(BROWSER_APP_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/tests/browser/app/${NT_PRESET_NAME}")
set(BROWSER_APP_ASSETS_DIR "${BROWSER_APP_OUTPUT_DIR}/assets")
# Shared pack locations -- the native example builds write the packs here; we copy from them.
set(UI_SHOWCASE_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_showcase")
set(BUNNYMARK_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/bunnymark")

# EXCLUDE_FROM_ALL: browser tests build this target explicitly; regular game builds omit its hooks.
add_executable(browser_smoke EXCLUDE_FROM_ALL main.c)
target_link_libraries(browser_smoke PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_ui_rich nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_hash nt_mem_scratch nt_metrics
)
target_compile_definitions(browser_smoke PRIVATE NT_TEST_PRESET_NAME="${NT_PRESET_NAME}")
# Reuse the examples' generated asset headers (ui_showcase_assets.h, bunnymark_sd.h).
target_include_directories(browser_smoke PRIVATE
    "${CMAKE_SOURCE_DIR}/examples/ui_showcase/generated"
    "${CMAKE_SOURCE_DIR}/examples/bunnymark/generated")

# nt_clipboard (real, per platform): nt_ui links nt_clipboard_interface; the exe picks the real impl
# so Ctrl+C/X/V work in the field. web = async cache-on-paste, native = glfw.
# The real transcoder (not the stub): the bunnymark pack's UASTC atlas is the Basis activation fixture.
if(EMSCRIPTEN)
    target_link_libraries(browser_smoke PRIVATE nt_platform_web nt_platform nt_gfx nt_http nt_clipboard nt_basisu_transcoder nt_meshwire_stub)
else()
    target_link_libraries(browser_smoke PRIVATE nt_gfx nt_http_stub nt_fs nt_clipboard nt_platform nt_basisu_transcoder nt_meshwire_stub)
endif()

nt_set_warning_flags(browser_smoke)
nt_set_sanitizer_flags(browser_smoke)
set_target_properties(browser_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BROWSER_APP_OUTPUT_DIR}")

# Copy the existing example packs into this target's assets/. Each is produced by a prior native
# example build (the builder can't run under wasm): ui_showcase.ntpack by ui_showcase_packs (RAW
# atlas), bunnymark_sd.ntpack by bunnymark_demo_packs (UASTC atlas -- the Basis fixture). Native or
# wasm: a plain copy of the existing file; if it isn't there yet, report it (build the native preset
# first).
set(BROWSER_APP_PACK_SOURCES
    "${UI_SHOWCASE_PACK_DIR}/ui_showcase.ntpack"
    "${BUNNYMARK_PACK_DIR}/bunnymark_sd.ntpack")
set(BROWSER_APP_PACK_OUTPUTS "")
foreach(PACK_SOURCE ${BROWSER_APP_PACK_SOURCES})
    get_filename_component(PACK_NAME "${PACK_SOURCE}" NAME)
    if(EXISTS "${PACK_SOURCE}")
        add_custom_command(
            OUTPUT "${BROWSER_APP_ASSETS_DIR}/${PACK_NAME}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${BROWSER_APP_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PACK_SOURCE}" "${BROWSER_APP_ASSETS_DIR}/${PACK_NAME}"
            DEPENDS "${PACK_SOURCE}"
            COMMENT "Copying ${PACK_NAME} -> tests/browser/app/${NT_PRESET_NAME}/assets/"
        )
        list(APPEND BROWSER_APP_PACK_OUTPUTS "${BROWSER_APP_ASSETS_DIR}/${PACK_NAME}")
    else()
        # STATUS (not WARNING): the pack is absent on a fresh configure (it's produced by a later
        # native example build). The browser-test CI job builds both packs BEFORE configuring wasm,
        # so the copies land; this branch is just the cold-configure path and must not look alarming.
        message(STATUS
            "browser_smoke: pack not found at ${PACK_SOURCE} yet "
            "(build the native example preset first; it produces the pack).")
    endif()
endforeach()
if(BROWSER_APP_PACK_OUTPUTS)
    add_custom_target(browser_smoke_packs DEPENDS ${BROWSER_APP_PACK_OUTPUTS})
    add_dependencies(browser_smoke browser_smoke_packs)
endif()

# Optional CDN base URL (mirrors ui_showcase).
if(NT_CDN_URL)
    target_compile_definitions(browser_smoke PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# The hooks are always compiled in the wasm build (this app's whole purpose): NO test-hooks gate.
nt_configure_shell(browser_smoke TITLE "Browser Smoke - Neotolis Engine")
