set(SKELETAL_SHOWCASE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/skeletal_showcase/${NT_PRESET_NAME}")
set(SKELETAL_SHOWCASE_ASSETS_DIR "${SKELETAL_SHOWCASE_OUTPUT_DIR}/assets")
set(SKELETAL_SHOWCASE_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/skeletal_showcase")

add_executable(skeletal_showcase main.c)
if(NT_DEVAPI_ENABLED)
    if(EMSCRIPTEN)
        target_link_libraries(skeletal_showcase PRIVATE nt_devapi_web nt_devapi_default)
        target_link_options(skeletal_showcase PRIVATE
            "SHELL:-sEXPORTED_FUNCTIONS=['_main','_nt_devapi_web_submit','_nt_devapi_web_poll','_nt_devapi_web_reset']"
            "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString']"
        )
    else()
        target_link_libraries(skeletal_showcase PRIVATE nt_devapi_net nt_devapi_default)
    endif()
endif()
target_link_libraries(skeletal_showcase PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas nt_skeletal nt_skeletal_assets
    nt_sprite_renderer nt_text_renderer nt_font nt_ui nt_shape_renderer
    nt_render nt_hash nt_mem_scratch
)
target_include_directories(skeletal_showcase PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

if(EMSCRIPTEN)
    target_link_libraries(skeletal_showcase PRIVATE
        nt_platform_web nt_platform nt_gfx nt_http nt_clipboard_stub
        nt_basisu_transcoder_stub nt_meshwire
    )
else()
    target_link_libraries(skeletal_showcase PRIVATE
        nt_gfx nt_http_stub nt_fs nt_clipboard_stub nt_platform
        nt_basisu_transcoder_stub nt_meshwire
    )
endif()

nt_set_warning_flags(skeletal_showcase)
nt_set_sanitizer_flags(skeletal_showcase)
set_target_properties(skeletal_showcase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SKELETAL_SHOWCASE_OUTPUT_DIR}"
)

if(NOT EMSCRIPTEN)
    add_executable(build_skeletal_showcase_packs build_packs.c)
    target_link_libraries(build_skeletal_showcase_packs PRIVATE nt_builder nt_log nt_meshwire)
    target_compile_definitions(build_skeletal_showcase_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_skeletal_showcase_packs)
    nt_set_warning_flags(build_skeletal_showcase_packs)
    nt_set_sanitizer_flags(build_skeletal_showcase_packs)
    set_target_properties(build_skeletal_showcase_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SKELETAL_SHOWCASE_OUTPUT_DIR}"
    )
endif()

nt_example_packs(
    NAME skeletal_showcase
    TARGET skeletal_showcase
    BUILDER build_skeletal_showcase_packs
    PACK_DIR "${SKELETAL_SHOWCASE_PACK_DIR}"
    ASSETS_DIR "${SKELETAL_SHOWCASE_ASSETS_DIR}"
    PACKS skeletal_showcase.ntpack skeletal_showcase_clips.ntpack
    INPUT_DIRS raw
        "${NT_ENGINE_ROOT}/assets/shaders"
        "${NT_ENGINE_ROOT}/examples/ui_showcase/raw"
)

if(NT_CDN_URL)
    target_compile_definitions(skeletal_showcase PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

nt_configure_shell(skeletal_showcase TITLE "Skeletal showcase - Neotolis Engine")
