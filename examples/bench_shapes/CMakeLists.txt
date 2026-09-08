add_executable(bench_shapes main.c)
target_link_libraries(bench_shapes PRIVATE nt_core nt_app nt_input nt_window nt_shape_renderer nt_math nt_log)
if(EMSCRIPTEN)
    target_link_libraries(bench_shapes PRIVATE nt_platform_web nt_gfx nt_basisu_transcoder_stub nt_meshwire_stub)
else()
    target_link_libraries(bench_shapes PRIVATE nt_gfx nt_basisu_transcoder_stub nt_meshwire_stub)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(bench_shapes)
nt_set_sanitizer_flags(bench_shapes)

# Per-project output: build/examples/bench_shapes/[preset]/
set_target_properties(bench_shapes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/bench_shapes/${NT_PRESET_NAME}"
)

nt_configure_wasm_app(bench_shapes TITLE "Shape Benchmark - Neotolis Engine")
