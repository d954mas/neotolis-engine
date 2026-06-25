# browser_smoke -- minimal standalone web target the Playwright browser specs drive.
# Renders a Cyrillic input field + a rich-text block with one clickable <link> and exposes the
# window.__nt smoke-test hooks. Reuses the ui_showcase pack (same generated asset ids); no own pack.

set(BROWSER_APP_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/tests/browser/app/${NT_PRESET_NAME}")
set(BROWSER_APP_ASSETS_DIR "${BROWSER_APP_OUTPUT_DIR}/assets")
# Shared pack location -- the native ui_showcase build writes ui_showcase.ntpack here; we copy from it.
set(UI_SHOWCASE_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_showcase")

# EXCLUDE_FROM_ALL: built ONLY by the browser-test CI job (explicit --target), never by the main
# wasm "build all" -- keeps the always-on EM_JS hooks out of the Closure-using wasm-release build.
add_executable(browser_smoke EXCLUDE_FROM_ALL main.c)
target_link_libraries(browser_smoke PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_hash nt_mem_scratch
)
# Reuse the showcase's generated asset header (ui_showcase_assets.h).
target_include_directories(browser_smoke PRIVATE "${CMAKE_SOURCE_DIR}/examples/ui_showcase/generated")

# nt_clipboard (real, per platform): nt_ui links nt_clipboard_interface; the exe picks the real impl
# so Ctrl+C/X/V work in the field. web = async cache-on-paste, native = glfw.
if(EMSCRIPTEN)
    target_link_libraries(browser_smoke PRIVATE nt_platform_web nt_platform nt_gfx nt_http nt_fs nt_clipboard)
else()
    target_link_libraries(browser_smoke PRIVATE nt_gfx nt_http_stub nt_fs nt_clipboard nt_platform)
endif()

nt_set_warning_flags(browser_smoke)
nt_set_sanitizer_flags(browser_smoke)
set_target_properties(browser_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BROWSER_APP_OUTPUT_DIR}")

# Copy the existing ui_showcase pack into this target's assets/. The pack is produced by a prior
# native ui_showcase build (the builder can't run under wasm). Native or wasm: a plain copy of the
# existing file; if it isn't there yet, warn (build the native ui_showcase preset first).
set(BROWSER_APP_PACK ui_showcase.ntpack)
if(EXISTS "${UI_SHOWCASE_PACK_DIR}/${BROWSER_APP_PACK}")
    add_custom_command(
        OUTPUT "${BROWSER_APP_ASSETS_DIR}/${BROWSER_APP_PACK}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BROWSER_APP_ASSETS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UI_SHOWCASE_PACK_DIR}/${BROWSER_APP_PACK}" "${BROWSER_APP_ASSETS_DIR}/${BROWSER_APP_PACK}"
        DEPENDS "${UI_SHOWCASE_PACK_DIR}/${BROWSER_APP_PACK}"
        COMMENT "Copying ${BROWSER_APP_PACK} -> tests/browser/app/${NT_PRESET_NAME}/assets/"
    )
    add_custom_target(browser_smoke_packs DEPENDS "${BROWSER_APP_ASSETS_DIR}/${BROWSER_APP_PACK}")
    add_dependencies(browser_smoke browser_smoke_packs)
else()
    # STATUS (not WARNING): the pack is absent on a fresh configure (it's produced by a later native
    # ui_showcase build). The browser-test CI job builds the pack BEFORE configuring wasm, so the copy
    # lands; this branch is just the cold-configure path and must not look alarming.
    message(STATUS
        "browser_smoke: pack not found at ${UI_SHOWCASE_PACK_DIR}/${BROWSER_APP_PACK} yet "
        "(build the native ui_showcase preset first; it produces the pack).")
endif()

# Optional CDN base URL (mirrors ui_showcase).
if(NT_CDN_URL)
    target_compile_definitions(browser_smoke PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings: emit index.html via the shared shell; the hooks are always compiled in the
# wasm build (this app's whole purpose), so there is NO test-hooks gate.
if(EMSCRIPTEN)
    set_target_properties(browser_smoke PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(browser_smoke TITLE "Browser Smoke - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(browser_smoke PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
endif()
