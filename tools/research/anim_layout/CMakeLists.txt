add_executable(anim_layout main.c)
target_link_libraries(anim_layout PRIVATE nt_anim nt_hash nt_core nt_log_stub)
target_compile_definitions(anim_layout PRIVATE _CRT_SECURE_NO_WARNINGS)
nt_set_static_crt(anim_layout)
nt_set_warning_flags(anim_layout)
# Links the sanitizer-instrumented engine modules in Debug — must match their flags.
nt_set_sanitizer_flags(anim_layout)
set_target_properties(anim_layout PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/research/${NT_PRESET_NAME}"
)
