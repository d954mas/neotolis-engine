set(TEXT_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/text/${NT_PRESET_NAME}")
set(TEXT_ASSETS_DIR "${TEXT_OUTPUT_DIR}/assets")
set(TEXT_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/text")

# Runtime demo
add_executable(text_demo main.c)
target_link_libraries(text_demo PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time
    nt_resource nt_material nt_shared nt_font nt_text_renderer
    nt_render nt_hash
)
target_include_directories(text_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

if(EMSCRIPTEN)
    target_link_libraries(text_demo PRIVATE nt_platform_web nt_gfx nt_http nt_fs nt_basisu_transcoder_stub nt_meshwire_stub)
else()
    target_link_libraries(text_demo PRIVATE nt_gfx nt_http_stub nt_fs nt_basisu_transcoder_stub nt_meshwire_stub)
endif()

nt_set_warning_flags(text_demo)
nt_set_sanitizer_flags(text_demo)

set_target_properties(text_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TEXT_OUTPUT_DIR}"
)

# Pack builder (native only)
if(NOT EMSCRIPTEN)
    add_executable(build_text_packs build_packs.c)
    target_link_libraries(build_text_packs PRIVATE nt_builder nt_log)
    target_compile_definitions(build_text_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_text_packs)
    nt_set_warning_flags(build_text_packs)
    nt_set_sanitizer_flags(build_text_packs)
    set_target_properties(build_text_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${TEXT_OUTPUT_DIR}"
    )
endif()

# text_cjk is OPTIONAL: the builder skips it when the local font lacks CJK glyphs.
nt_example_packs(
    NAME text
    TARGET text_demo
    BUILDER build_text_packs
    PACK_DIR "${TEXT_PACK_DIR}"
    ASSETS_DIR "${TEXT_ASSETS_DIR}"
    PACKS text_base.ntpack
    OPTIONAL_PACKS text_cjk.ntpack
)

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(text_demo PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(text_demo PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(text_demo TITLE "Text Demo - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(text_demo PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(text_demo PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
endif()
