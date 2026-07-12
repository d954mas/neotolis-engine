add_executable(atlas_bench main.c ntpack_parse.c bench_json.c)
# The executable selects the real logger; nt_builder keeps an interface-only dependency.
target_link_libraries(atlas_bench PRIVATE nt_builder nt_log)
target_include_directories(atlas_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/include   # nt_atlas_format.h, nt_pack_format.h, nt_texture_format.h
    ${CMAKE_SOURCE_DIR}/tools/builder    # nt_builder.h
)
target_compile_definitions(atlas_bench PRIVATE _CRT_SECURE_NO_WARNINGS)
nt_set_static_crt(atlas_bench)
nt_set_warning_flags(atlas_bench)
# Links the sanitizer-instrumented nt_builder in Debug — must match its ASan/UBSan flags.
nt_set_sanitizer_flags(atlas_bench)
set_target_properties(atlas_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/research/${NT_PRESET_NAME}"
)
