add_executable(bench_capsule main.c)
target_link_libraries(bench_capsule PRIVATE nt_core nt_app nt_input nt_window nt_shape_renderer nt_math)
if(EMSCRIPTEN)
    target_link_libraries(bench_capsule PRIVATE nt_platform_web nt_gfx)
else()
    target_link_libraries(bench_capsule PRIVATE nt_gfx_stub)
endif()

nt_set_warning_flags(bench_capsule)
nt_set_sanitizer_flags(bench_capsule)

set_target_properties(bench_capsule PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/bench_capsule/${NT_PRESET_NAME}"
)

if(EMSCRIPTEN)
    set_target_properties(bench_capsule PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(bench_capsule TITLE "Capsule Instancing Benchmark - Neotolis Engine")
    target_link_options(bench_capsule PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(bench_capsule PRIVATE "SHELL:-sASSERTIONS=2" "SHELL:-gsource-map")
    endif()
    if(NT_WASM_ANALYSIS)
        target_link_options(bench_capsule PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
