# Frame inspector: a native app that reads capture files (render architecture, "Capture text").
set(FRAME_INSPECTOR_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/tools/frame_inspector/${NT_PRESET_NAME}")
set(FRAME_INSPECTOR_PACK_DIR "${CMAKE_SOURCE_DIR}/build/tools/frame_inspector")

# The model has no UI and no gfx backend, so the unit tests link it alone.
add_library(fi_model STATIC fi_model.c fi_model.h)
target_include_directories(fi_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(fi_model PUBLIC nt_gfx_capture_text nt_fs nt_core nt_log)
target_compile_definitions(fi_model PRIVATE _CRT_SECURE_NO_WARNINGS)
nt_set_warning_flags(fi_model)
nt_set_sanitizer_flags(fi_model)

add_executable(frame_inspector main.c)
target_include_directories(frame_inspector PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")
target_link_libraries(frame_inspector PRIVATE
    fi_model nt_version
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas nt_ui nt_sprite_renderer nt_text_renderer nt_font
    nt_render nt_hash nt_mem_scratch
    nt_gfx nt_http_stub nt_fs nt_file_dialog nt_clipboard nt_platform nt_basisu_transcoder_stub nt_meshwire_stub
)
nt_set_warning_flags(frame_inspector)
nt_set_sanitizer_flags(frame_inspector)
set_target_properties(frame_inspector PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${FRAME_INSPECTOR_OUTPUT_DIR}")

add_executable(build_frame_inspector_packs build_packs.c)
target_link_libraries(build_frame_inspector_packs PRIVATE nt_builder nt_log nt_meshwire)
target_compile_definitions(build_frame_inspector_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
nt_set_static_crt(build_frame_inspector_packs)
nt_set_warning_flags(build_frame_inspector_packs)
nt_set_sanitizer_flags(build_frame_inspector_packs)
set_target_properties(build_frame_inspector_packs PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${FRAME_INSPECTOR_OUTPUT_DIR}")

nt_example_packs(
    NAME frame_inspector
    TARGET frame_inspector
    BUILDER build_frame_inspector_packs
    PACK_DIR "${FRAME_INSPECTOR_PACK_DIR}"
    ASSETS_DIR "${FRAME_INSPECTOR_OUTPUT_DIR}/assets"
    GENERATED_DIR generated
    PACKS frame_inspector.ntpack
)
