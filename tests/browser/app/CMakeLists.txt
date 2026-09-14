# browser_smoke -- minimal standalone web target the Playwright browser specs drive.
# Renders a Cyrillic input field + a rich-text block with one clickable <link> and exposes the
# window.__nt smoke-test hooks. Reuses the ui_showcase pack (same generated asset ids) and builds
# its own Basis fixture pack for the admission set of this configure.

set(BROWSER_APP_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/tests/browser/app/${NT_PRESET_NAME}")
set(BROWSER_APP_ASSETS_DIR "${BROWSER_APP_OUTPUT_DIR}/assets")
# Shared pack location -- the native ui_showcase build writes the pack here; we copy from it.
set(UI_SHOWCASE_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_showcase")
# The Basis fixture is bound to NT_BASISU_CODECS/NT_BASISU_TARGETS: a native configure of one set
# produces it, the wasm configure of the same set copies it, other sets never see it.
set(BROWSER_FIXTURE_PACK_DIR "${CMAKE_SOURCE_DIR}/build/tests/browser/fixtures/${NT_BASISU_SET_TAG}")

# EXCLUDE_FROM_ALL: browser tests build this target explicitly; regular game builds omit its hooks.
add_executable(browser_smoke EXCLUDE_FROM_ALL main.c)
target_link_libraries(browser_smoke PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_ui_rich nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_hash nt_mem_scratch nt_metrics
)
target_compile_definitions(browser_smoke PRIVATE NT_TEST_PRESET_NAME="${NT_PRESET_NAME}")
# Reuse the showcase's generated asset header (ui_showcase_assets.h).
target_include_directories(browser_smoke PRIVATE "${CMAKE_SOURCE_DIR}/examples/ui_showcase/generated")

# nt_clipboard (real, per platform): nt_ui links nt_clipboard_interface; the exe picks the real impl
# so Ctrl+C/X/V work in the field. web = async cache-on-paste, native = glfw.
# The real transcoder (not the stub): the fixture pack's Basis textures are the activation fixture.
if(EMSCRIPTEN)
    target_link_libraries(browser_smoke PRIVATE nt_platform_web nt_platform nt_gfx nt_http nt_clipboard nt_basisu_transcoder nt_meshwire_stub)
else()
    target_link_libraries(browser_smoke PRIVATE nt_gfx nt_http_stub nt_fs nt_clipboard nt_platform nt_basisu_transcoder nt_meshwire_stub)
endif()

nt_set_warning_flags(browser_smoke)
nt_set_sanitizer_flags(browser_smoke)
set_target_properties(browser_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BROWSER_APP_OUTPUT_DIR}")

# Basis fixture producer (native only); nt_example_packs wires build + copy + the codec-set check.
if(NOT EMSCRIPTEN)
    add_executable(build_browser_fixture_pack build_fixture_pack.c)
    target_link_libraries(build_browser_fixture_pack PRIVATE nt_builder nt_log nt_meshwire)
    target_compile_definitions(build_browser_fixture_pack PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_browser_fixture_pack)
    nt_set_warning_flags(build_browser_fixture_pack)
    nt_set_sanitizer_flags(build_browser_fixture_pack)
    set_target_properties(build_browser_fixture_pack PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BROWSER_APP_OUTPUT_DIR}")
endif()
nt_example_packs(
    NAME browser_fixture
    TARGET browser_smoke
    BUILDER build_browser_fixture_pack
    PACK_DIR "${BROWSER_FIXTURE_PACK_DIR}"
    ASSETS_DIR "${BROWSER_APP_ASSETS_DIR}"
    PACKS basis_fixture.ntpack
)

# Copy the existing showcase pack into this target's assets/. It is produced by a prior native
# ui_showcase build (the builder can't run under wasm): a plain copy of the existing file; if it
# isn't there yet, report it (build the native preset first).
set(UI_SHOWCASE_PACK_SOURCE "${UI_SHOWCASE_PACK_DIR}/ui_showcase.ntpack")
if(EXISTS "${UI_SHOWCASE_PACK_SOURCE}")
    add_custom_command(
        OUTPUT "${BROWSER_APP_ASSETS_DIR}/ui_showcase.ntpack"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BROWSER_APP_ASSETS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UI_SHOWCASE_PACK_SOURCE}" "${BROWSER_APP_ASSETS_DIR}/ui_showcase.ntpack"
        DEPENDS "${UI_SHOWCASE_PACK_SOURCE}"
        COMMENT "Copying ui_showcase.ntpack -> tests/browser/app/${NT_PRESET_NAME}/assets/"
    )
    add_custom_target(browser_smoke_showcase_pack DEPENDS "${BROWSER_APP_ASSETS_DIR}/ui_showcase.ntpack")
    add_dependencies(browser_smoke browser_smoke_showcase_pack)
else()
    # STATUS (not WARNING): the pack is absent on a fresh configure (it's produced by a later
    # native example build). The browser-test CI job builds the pack BEFORE configuring wasm,
    # so the copy lands; this branch is just the cold-configure path and must not look alarming.
    message(STATUS
        "browser_smoke: pack not found at ${UI_SHOWCASE_PACK_SOURCE} yet "
        "(build the native example preset first; it produces the pack).")
endif()

# Optional CDN base URL (mirrors ui_showcase).
if(NT_CDN_URL)
    target_compile_definitions(browser_smoke PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# The hooks are always compiled in the wasm build (this app's whole purpose): NO test-hooks gate.
nt_configure_shell(browser_smoke TITLE "Browser Smoke - Neotolis Engine")
