set(ATLAS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/atlas/${NT_PRESET_NAME}")
set(ATLAS_ASSETS_DIR "${ATLAS_OUTPUT_DIR}/assets")
# Shared pack location — builder writes here once, all presets copy from here
set(ATLAS_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/atlas")

add_executable(atlas_demo main.c)
target_link_libraries(atlas_demo PRIVATE nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource nt_material nt_shared nt_entity nt_transform_comp nt_mesh_comp nt_material_comp nt_drawable_comp nt_mesh_renderer nt_render nt_hash)
# Generated asset headers from builder (atlas_assets.h etc.)
target_include_directories(atlas_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(EMSCRIPTEN)
    target_link_libraries(atlas_demo PRIVATE nt_platform_web nt_gfx nt_http nt_fs nt_basisu_transcoder_stub)
else()
    target_link_libraries(atlas_demo PRIVATE nt_gfx nt_http_stub nt_fs nt_basisu_transcoder_stub)
endif()

nt_set_warning_flags(atlas_demo)
nt_set_sanitizer_flags(atlas_demo)

set_target_properties(atlas_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${ATLAS_OUTPUT_DIR}"
)

# Pack builder (native only)
if(NOT EMSCRIPTEN)
    add_executable(build_atlas_packs build_packs.c)
    target_link_libraries(build_atlas_packs PRIVATE nt_builder nt_log)
    target_compile_definitions(build_atlas_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_atlas_packs)
    nt_set_warning_flags(build_atlas_packs)
    nt_set_sanitizer_flags(build_atlas_packs)
    set_target_properties(build_atlas_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ATLAS_OUTPUT_DIR}"
    )
endif()

nt_example_packs(
    NAME atlas
    TARGET atlas_demo
    BUILDER build_atlas_packs
    PACK_DIR "${ATLAS_PACK_DIR}"
    ASSETS_DIR "${ATLAS_ASSETS_DIR}"
    PACKS atlas_demo.ntpack
)

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(atlas_demo PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(atlas_demo PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(atlas_demo TITLE "Atlas Demo - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(atlas_demo PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(atlas_demo PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
    if(NT_WASM_ANALYSIS)
        target_link_options(atlas_demo PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
