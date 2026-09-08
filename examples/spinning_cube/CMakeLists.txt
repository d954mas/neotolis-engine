add_executable(spinning_cube main.c)
target_link_libraries(spinning_cube PRIVATE nt_core nt_app nt_input nt_window nt_shape_renderer nt_math nt_transform_comp nt_drawable_comp nt_log)
if(EMSCRIPTEN)
    target_link_libraries(spinning_cube PRIVATE nt_platform_web nt_gfx nt_basisu_transcoder_stub nt_meshwire_stub)
else()
    target_link_libraries(spinning_cube PRIVATE nt_gfx nt_basisu_transcoder_stub nt_meshwire_stub)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(spinning_cube)
nt_set_sanitizer_flags(spinning_cube)

# Per-project output: build/examples/spinning_cube/[preset]/
set_target_properties(spinning_cube PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/spinning_cube/${NT_PRESET_NAME}"
)

nt_configure_shell(spinning_cube TITLE "Spinning Shapes - Neotolis Engine")
