add_executable(builder main.c)
target_link_libraries(builder PRIVATE nt_engine)

# Strict warning and sanitizer policy
nt_set_warning_flags(builder)
nt_set_sanitizer_flags(builder)

# Per-project output: build/tools/builder/[preset]/
set_target_properties(builder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)
