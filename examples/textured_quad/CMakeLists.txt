add_executable(textured_quad main.c)
target_link_libraries(textured_quad PRIVATE nt_core nt_app nt_input nt_window nt_math)
if(EMSCRIPTEN)
    target_link_libraries(textured_quad PRIVATE nt_platform_web nt_gfx)
else()
    target_link_libraries(textured_quad PRIVATE nt_gfx)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(textured_quad)
nt_set_sanitizer_flags(textured_quad)

# Per-project output: build/examples/textured_quad/[preset]/
set_target_properties(textured_quad PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/textured_quad/${NT_PRESET_NAME}"
)

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(textured_quad PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")

    nt_configure_shell(textured_quad TITLE "Textured Quad - Neotolis Engine")

    target_link_options(textured_quad PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(textured_quad PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()

    if(NT_WASM_ANALYSIS)
        target_link_options(textured_quad PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
