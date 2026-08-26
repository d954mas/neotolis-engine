set(SLICE9_DEMO_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/slice9_demo/${NT_PRESET_NAME}")
set(SLICE9_DEMO_ASSETS_DIR "${SLICE9_DEMO_OUTPUT_DIR}/assets")
# Shared pack location -- builder writes here once; every preset copies from here.
set(SLICE9_DEMO_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/slice9_demo")

add_executable(slice9_demo main.c)
target_link_libraries(slice9_demo PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_hash nt_mem_scratch nt_debug_overlay nt_metrics
)
# Generated asset headers from builder (slice9_demo_assets.h etc.)
target_include_directories(slice9_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

# nt_ui calls nt_clipboard_* (no text field here, but the symbols must resolve); stub = no exports.
if(EMSCRIPTEN)
    target_link_libraries(slice9_demo PRIVATE nt_platform_web nt_platform nt_gfx nt_http nt_fs nt_clipboard_stub nt_basisu_transcoder_stub nt_meshwire_stub)
else()
    # nt_platform supplies nt_platform_memory_usage for the metrics mem_used channel.
    target_link_libraries(slice9_demo PRIVATE nt_gfx nt_http_stub nt_fs nt_clipboard_stub nt_platform nt_basisu_transcoder_stub nt_meshwire_stub)
endif()

nt_set_warning_flags(slice9_demo)
nt_set_sanitizer_flags(slice9_demo)

set_target_properties(slice9_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SLICE9_DEMO_OUTPUT_DIR}"
)

if(NOT EMSCRIPTEN)
    add_executable(build_slice9_demo_packs build_packs.c)
    target_link_libraries(build_slice9_demo_packs PRIVATE nt_builder nt_log)
    target_compile_definitions(build_slice9_demo_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_slice9_demo_packs)
    nt_set_warning_flags(build_slice9_demo_packs)
    nt_set_sanitizer_flags(build_slice9_demo_packs)
    set_target_properties(build_slice9_demo_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SLICE9_DEMO_OUTPUT_DIR}"
    )
endif()

nt_example_packs(
    NAME slice9_demo
    TARGET slice9_demo
    BUILDER build_slice9_demo_packs
    PACK_DIR "${SLICE9_DEMO_PACK_DIR}"
    ASSETS_DIR "${SLICE9_DEMO_ASSETS_DIR}"
    PACKS slice9_demo.ntpack
)

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(slice9_demo PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(slice9_demo PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(slice9_demo TITLE "Slice9 Demo - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(slice9_demo PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(slice9_demo PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
endif()
