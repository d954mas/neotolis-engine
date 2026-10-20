set(BUNNYMARK_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/bunnymark/${NT_PRESET_NAME}")
set(BUNNYMARK_ASSETS_DIR "${BUNNYMARK_OUTPUT_DIR}/assets")
# Shared pack location -- builder writes here once, all presets copy from here
set(BUNNYMARK_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/bunnymark")

add_executable(bunnymark_demo main.c)
target_link_libraries(bunnymark_demo PRIVATE nt_version)
target_link_libraries(bunnymark_demo PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_entity nt_transform_comp nt_material_comp
    nt_drawable_comp nt_sprite_comp nt_atlas nt_sprite_renderer
    nt_debug_overlay nt_metrics nt_text_renderer nt_font
    nt_render nt_hash
)
# Generated per-pack asset headers from builder
target_include_directories(bunnymark_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

if(EMSCRIPTEN)
    # nt_platform (memory probe for metrics mem_used) is distinct from nt_platform_web (window target).
    target_link_libraries(bunnymark_demo PRIVATE nt_platform_web nt_platform nt_gfx nt_http nt_basisu_transcoder nt_meshwire_stub)
else()
    target_link_libraries(bunnymark_demo PRIVATE nt_gfx nt_http_stub nt_fs nt_platform nt_basisu_transcoder nt_meshwire_stub)
endif()

nt_set_warning_flags(bunnymark_demo)
nt_set_sanitizer_flags(bunnymark_demo)

set_target_properties(bunnymark_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BUNNYMARK_OUTPUT_DIR}"
)

# Pack builder (native only)
if(NOT EMSCRIPTEN)
    add_executable(build_bunnymark_packs build_packs.c)
    target_link_libraries(build_bunnymark_packs PRIVATE nt_builder nt_log nt_meshwire)
    target_compile_definitions(build_bunnymark_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_bunnymark_packs)
    nt_set_warning_flags(build_bunnymark_packs)
    nt_set_sanitizer_flags(build_bunnymark_packs)
    set_target_properties(build_bunnymark_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BUNNYMARK_OUTPUT_DIR}"
    )
endif()

nt_example_packs(
    NAME bunnymark
    TARGET bunnymark_demo
    BUILDER build_bunnymark_packs
    PACK_DIR "${BUNNYMARK_PACK_DIR}"
    ASSETS_DIR "${BUNNYMARK_ASSETS_DIR}"
    GENERATED_DIR generated
    PACKS bunnymark_sd.ntpack bunnymark_hd.ntpack
)

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(bunnymark_demo PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

nt_configure_shell(bunnymark_demo TITLE "Bunnymark Demo - Neotolis Engine")
