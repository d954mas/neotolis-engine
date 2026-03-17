add_executable(builder_demo main.c)
target_link_libraries(builder_demo PRIVATE nt_builder)
target_compile_definitions(builder_demo PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(builder_demo PRIVATE -U_DLL)
nt_set_warning_flags(builder_demo)
nt_set_sanitizer_flags(builder_demo)
set_target_properties(builder_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/builder_demo/${NT_PRESET_NAME}"
)
