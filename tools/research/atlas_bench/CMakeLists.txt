add_executable(atlas_bench main.c ntpack_parse.c bench_json.c)
# nt_builder links only nt_log_interface (header-only) — the exe must supply the
# real nt_log implementation, mirroring the builder exe (tools/builder:119).
target_link_libraries(atlas_bench PRIVATE nt_builder nt_log)
target_include_directories(atlas_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/include   # nt_atlas_format.h, nt_pack_format.h, nt_texture_format.h
    ${CMAKE_SOURCE_DIR}/tools/builder    # nt_builder.h
)
target_compile_definitions(atlas_bench PRIVATE _CRT_SECURE_NO_WARNINGS)
nt_set_static_crt(atlas_bench)
nt_set_warning_flags(atlas_bench)
set_target_properties(atlas_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/research/${NT_PRESET_NAME}"
)
