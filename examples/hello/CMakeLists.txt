add_executable(hello main.c)
target_link_libraries(hello PRIVATE nt_core cglm_headers)

# Strict warning and sanitizer policy
nt_set_warning_flags(hello)
nt_set_sanitizer_flags(hello)

# Per-project output: build/examples/hello/[preset]/
set_target_properties(hello PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/hello/${NT_PRESET_NAME}"
)

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(hello PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")

    # Shell configuration (replaces hardcoded --shell-file)
    nt_configure_shell(hello TITLE "Hello - Neotolis Engine")

    target_link_options(hello PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )

    # Debug-only WASM link options
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(hello PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()

    # Analysis build: preserve human-readable function names for size reporting
    if(NT_WASM_ANALYSIS)
        target_link_options(hello PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
