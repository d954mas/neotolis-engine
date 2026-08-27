set(RTT_SHOWCASE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/rtt_showcase/${NT_PRESET_NAME}")
set(RTT_SHOWCASE_ASSETS_DIR "${RTT_SHOWCASE_OUTPUT_DIR}/assets")
set(RTT_SHOWCASE_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/rtt_showcase")

add_executable(rtt_showcase main.c)
target_link_libraries(rtt_showcase PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_shape_renderer nt_material nt_postfx_blur
    nt_resource nt_atlas nt_hash nt_mem_scratch nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_shared
)
target_include_directories(rtt_showcase PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(EMSCRIPTEN)
    target_link_libraries(rtt_showcase PRIVATE nt_platform_web nt_platform nt_gfx nt_http nt_fs nt_clipboard_stub nt_basisu_transcoder_stub nt_meshwire_stub)
else()
    target_link_libraries(rtt_showcase PRIVATE nt_gfx nt_http_stub nt_fs nt_clipboard_stub nt_platform nt_basisu_transcoder_stub nt_meshwire_stub)
endif()

nt_set_warning_flags(rtt_showcase)
nt_set_sanitizer_flags(rtt_showcase)

set_target_properties(rtt_showcase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${RTT_SHOWCASE_OUTPUT_DIR}"
)

if(NOT EMSCRIPTEN)
    add_executable(build_rtt_showcase_packs build_packs.c)
    target_link_libraries(build_rtt_showcase_packs PRIVATE nt_builder nt_log nt_meshwire)
    target_compile_definitions(build_rtt_showcase_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_rtt_showcase_packs)
    nt_set_warning_flags(build_rtt_showcase_packs)
    nt_set_sanitizer_flags(build_rtt_showcase_packs)
    set_target_properties(build_rtt_showcase_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${RTT_SHOWCASE_OUTPUT_DIR}"
    )
endif()

nt_example_packs(
    NAME rtt_showcase
    TARGET rtt_showcase
    BUILDER build_rtt_showcase_packs
    PACK_DIR "${RTT_SHOWCASE_PACK_DIR}"
    ASSETS_DIR "${RTT_SHOWCASE_ASSETS_DIR}"
    PACKS rtt_showcase.ntpack
)

if(NT_CDN_URL)
    target_compile_definitions(rtt_showcase PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

if(EMSCRIPTEN)
    set_target_properties(rtt_showcase PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(rtt_showcase TITLE "RTT Showcase - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")

    target_link_options(rtt_showcase PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(rtt_showcase PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()

    if(NT_WASM_ANALYSIS)
        target_link_options(rtt_showcase PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
