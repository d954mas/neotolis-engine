add_executable(spinning_cube main.c)
target_link_libraries(spinning_cube PRIVATE nt_core nt_app nt_input nt_window nt_shape_renderer nt_math)
if(EMSCRIPTEN)
    target_link_libraries(spinning_cube PRIVATE nt_platform_web nt_gfx)
else()
    target_link_libraries(spinning_cube PRIVATE nt_gfx)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(spinning_cube)
nt_set_sanitizer_flags(spinning_cube)

# Per-project output: build/examples/spinning_cube/[preset]/
set_target_properties(spinning_cube PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/spinning_cube/${NT_PRESET_NAME}"
)

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(spinning_cube PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")

    nt_configure_shell(spinning_cube TITLE "Spinning Shapes - Neotolis Engine")

    target_link_options(spinning_cube PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(spinning_cube PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()

    if(NT_WASM_ANALYSIS)
        target_link_options(spinning_cube PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
