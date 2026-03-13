add_executable(bench_shapes main.c)
target_link_libraries(bench_shapes PRIVATE nt_core nt_app nt_input nt_window nt_shape_renderer nt_math)
if(EMSCRIPTEN)
    target_link_libraries(bench_shapes PRIVATE nt_platform_web nt_gfx)
else()
    target_link_libraries(bench_shapes PRIVATE nt_gfx)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(bench_shapes)
nt_set_sanitizer_flags(bench_shapes)

# Per-project output: build/examples/bench_shapes/[preset]/
set_target_properties(bench_shapes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/bench_shapes/${NT_PRESET_NAME}"
)

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(bench_shapes PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")

    nt_configure_shell(bench_shapes TITLE "Shape Benchmark - Neotolis Engine")

    target_link_options(bench_shapes PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(bench_shapes PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()

    if(NT_WASM_ANALYSIS)
        target_link_options(bench_shapes PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
