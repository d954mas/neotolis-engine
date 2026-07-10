add_executable(hash_benchmark main.c)
target_include_directories(hash_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_SOURCE_DIR}/deps/xxhash
)
target_compile_definitions(hash_benchmark PRIVATE _CRT_SECURE_NO_WARNINGS)
nt_set_static_crt(hash_benchmark)
nt_set_warning_flags(hash_benchmark)
set_target_properties(hash_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/research/${NT_PRESET_NAME}"
)
