add_executable(bench_stream main.c)
target_link_libraries(bench_stream PRIVATE nt_core nt_frame_arena nt_app nt_input nt_window nt_math nt_log nt_time nt_hash)
if(EMSCRIPTEN)
    target_link_libraries(bench_stream PRIVATE nt_platform_web nt_gfx nt_http nt_basisu_transcoder_stub nt_meshwire_stub)
else()
    # Native reads the config and writes results through stdio; nt_http is only linked for the shared call sites.
    target_link_libraries(bench_stream PRIVATE nt_gfx nt_http_stub nt_basisu_transcoder_stub nt_meshwire_stub)
    target_compile_definitions(bench_stream PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

nt_set_warning_flags(bench_stream)
nt_set_sanitizer_flags(bench_stream)

# Per-project output: build/examples/bench_stream/[preset]/
set_target_properties(bench_stream PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/bench_stream/${NT_PRESET_NAME}"
)

nt_configure_shell(bench_stream TITLE "Stream Upload Benchmark - Neotolis Engine")
