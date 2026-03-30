# font_bench — font format size benchmark tool
add_executable(font_bench font_bench.c)
target_link_libraries(font_bench PRIVATE nt_builder)
target_compile_definitions(font_bench PRIVATE _CRT_SECURE_NO_WARNINGS NT_LOG_DOMAIN_DEFAULT="bench")
target_compile_options(font_bench PRIVATE -U_DLL)
nt_set_warning_flags(font_bench)
nt_set_sanitizer_flags(font_bench)
set_target_properties(font_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/benchmark/${NT_PRESET_NAME}"
)

# contour_verify — round-trip verification of contour data
add_executable(contour_verify contour_verify.c)
target_link_libraries(contour_verify PRIVATE nt_builder)
target_compile_definitions(contour_verify PRIVATE _CRT_SECURE_NO_WARNINGS NT_LOG_DOMAIN_DEFAULT="verify")
target_compile_options(contour_verify PRIVATE -U_DLL)
nt_set_warning_flags(contour_verify)
nt_set_sanitizer_flags(contour_verify)
set_target_properties(contour_verify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/benchmark/${NT_PRESET_NAME}"
)

# gpos_diag — GPOS table diagnostic tool
add_executable(gpos_diag gpos_diag.c)
target_link_libraries(gpos_diag PRIVATE nt_builder)
target_compile_definitions(gpos_diag PRIVATE _CRT_SECURE_NO_WARNINGS NT_LOG_DOMAIN_DEFAULT="diag")
target_compile_options(gpos_diag PRIVATE -U_DLL)
nt_set_warning_flags(gpos_diag)
nt_set_sanitizer_flags(gpos_diag)
set_target_properties(gpos_diag PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/benchmark/${NT_PRESET_NAME}"
)
