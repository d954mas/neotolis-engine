add_executable(rtt_showcase main.c)
target_link_libraries(rtt_showcase PRIVATE nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_shape_renderer nt_material nt_postfx_blur)
if(EMSCRIPTEN)
    target_link_libraries(rtt_showcase PRIVATE nt_platform_web nt_gfx)
else()
    target_link_libraries(rtt_showcase PRIVATE nt_gfx)
endif()

nt_set_warning_flags(rtt_showcase)
nt_set_sanitizer_flags(rtt_showcase)

set_target_properties(rtt_showcase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/rtt_showcase/${NT_PRESET_NAME}"
)

if(EMSCRIPTEN)
    set_target_properties(rtt_showcase PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(rtt_showcase TITLE "RTT Showcase - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")

    target_link_options(rtt_showcase PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(rtt_showcase PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()

    if(NT_WASM_ANALYSIS)
        target_link_options(rtt_showcase PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
