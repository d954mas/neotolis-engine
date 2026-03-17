add_executable(build_assets build_assets.c)
target_link_libraries(build_assets PRIVATE nt_builder)
target_compile_definitions(build_assets PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(build_assets PRIVATE -U_DLL)
nt_set_warning_flags(build_assets)
nt_set_sanitizer_flags(build_assets)
set_target_properties(build_assets PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/builder_demo/${NT_PRESET_NAME}"
)
