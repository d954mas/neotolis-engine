add_executable(hello main.c)
target_link_libraries(hello PRIVATE nt_core nt_app nt_input nt_window nt_log)
if(EMSCRIPTEN)
    target_link_libraries(hello PRIVATE nt_platform_web)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(hello)
nt_set_sanitizer_flags(hello)

# Per-project output: build/examples/hello/[preset]/
set_target_properties(hello PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/hello/${NT_PRESET_NAME}"
)

nt_configure_shell(hello TITLE "Hello - Neotolis Engine")
