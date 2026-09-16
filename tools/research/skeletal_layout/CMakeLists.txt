add_executable(skeletal_layout main.c)
target_link_libraries(skeletal_layout PRIVATE nt_skeletal nt_hash nt_core nt_log_stub)
if(NOT WIN32)
    target_link_libraries(skeletal_layout PRIVATE m)
endif()
target_compile_definitions(skeletal_layout PRIVATE _CRT_SECURE_NO_WARNINGS)
nt_set_static_crt(skeletal_layout)
nt_set_warning_flags(skeletal_layout)
# Apply the same Debug sanitizer policy as the engine modules.
nt_set_sanitizer_flags(skeletal_layout)
set_target_properties(skeletal_layout PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/research/${NT_PRESET_NAME}"
)
