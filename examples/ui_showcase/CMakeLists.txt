set(UI_SHOWCASE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_showcase/${NT_PRESET_NAME}")
set(UI_SHOWCASE_ASSETS_DIR "${UI_SHOWCASE_OUTPUT_DIR}/assets")
# Shared pack location -- builder writes here once; every preset copies from here.
set(UI_SHOWCASE_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_showcase")

add_executable(ui_showcase main.c)
target_link_libraries(ui_showcase PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_shape_renderer
    nt_render nt_hash nt_mem_scratch nt_debug_overlay nt_metrics
)
# Generated asset headers from builder (ui_showcase_assets.h etc.)
target_include_directories(ui_showcase PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

# nt_clipboard (real, per platform): nt_ui links nt_clipboard_interface; the exe picks the
# real impl so Ctrl+C/X/V work in the Input tab. web = async cache-on-paste, native = glfw.
if(EMSCRIPTEN)
    target_link_libraries(ui_showcase PRIVATE nt_platform_web nt_platform nt_gfx nt_http nt_fs nt_clipboard nt_basisu_transcoder_stub nt_meshwire_stub)
else()
    # nt_platform supplies nt_platform_memory_usage for the metrics mem_used channel.
    target_link_libraries(ui_showcase PRIVATE nt_gfx nt_http_stub nt_fs nt_clipboard nt_platform nt_basisu_transcoder_stub nt_meshwire_stub)
endif()

nt_set_warning_flags(ui_showcase)
nt_set_sanitizer_flags(ui_showcase)
set_target_properties(ui_showcase PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${UI_SHOWCASE_OUTPUT_DIR}")

if(NOT EMSCRIPTEN)
    add_executable(build_ui_showcase_packs build_packs.c)
    target_link_libraries(build_ui_showcase_packs PRIVATE nt_builder nt_log nt_meshwire)
    target_compile_definitions(build_ui_showcase_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_ui_showcase_packs)
    nt_set_warning_flags(build_ui_showcase_packs)
    nt_set_sanitizer_flags(build_ui_showcase_packs)
    set_target_properties(build_ui_showcase_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${UI_SHOWCASE_OUTPUT_DIR}"
    )
endif()

nt_example_packs(
    NAME ui_showcase
    TARGET ui_showcase
    BUILDER build_ui_showcase_packs
    PACK_DIR "${UI_SHOWCASE_PACK_DIR}"
    ASSETS_DIR "${UI_SHOWCASE_ASSETS_DIR}"
    PACKS ui_showcase.ntpack
)

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(ui_showcase PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(ui_showcase PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(ui_showcase TITLE "UI Showcase - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(ui_showcase PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
endif()
