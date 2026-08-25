set(TQ_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/textured_quad/${NT_PRESET_NAME}")
set(TQ_ASSETS_DIR "${TQ_OUTPUT_DIR}/assets")
# Shared pack location — builder writes here once, all presets copy from here
set(TQ_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/textured_quad")

add_executable(textured_quad main.c)
target_link_libraries(textured_quad PRIVATE nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource nt_material nt_shared nt_entity nt_transform_comp nt_mesh_comp nt_material_comp nt_drawable_comp nt_mesh_renderer nt_render nt_hash)
# Generated asset headers from builder (tq_assets.h etc.)
target_include_directories(textured_quad PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(EMSCRIPTEN)
    target_link_libraries(textured_quad PRIVATE nt_platform_web nt_gfx nt_http nt_fs nt_basisu_transcoder_stub)
else()
    target_link_libraries(textured_quad PRIVATE nt_gfx nt_http_stub nt_fs nt_basisu_transcoder_stub)
endif()

nt_set_warning_flags(textured_quad)
nt_set_sanitizer_flags(textured_quad)

set_target_properties(textured_quad PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TQ_OUTPUT_DIR}"
)

# Pack builder (native only)
if(NOT EMSCRIPTEN)
    add_executable(build_tq_packs build_packs.c)
    target_link_libraries(build_tq_packs PRIVATE nt_builder nt_log)
    target_compile_definitions(build_tq_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_tq_packs)
    nt_set_warning_flags(build_tq_packs)
    nt_set_sanitizer_flags(build_tq_packs)
    set_target_properties(build_tq_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${TQ_OUTPUT_DIR}"
    )
endif()

nt_example_packs(
    NAME textured_quad
    TARGET textured_quad
    BUILDER build_tq_packs
    PACK_DIR "${TQ_PACK_DIR}"
    ASSETS_DIR "${TQ_ASSETS_DIR}"
    PACKS base.ntpack lenna_pixel.ntpack lenna_hires.ntpack
)

# Optional CDN base URL (set by CI for deployed builds)
if(NT_CDN_URL)
    target_compile_definitions(textured_quad PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(textured_quad PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(textured_quad TITLE "Textured Cube - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(textured_quad PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(textured_quad PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
    if(NT_WASM_ANALYSIS)
        target_link_options(textured_quad PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
