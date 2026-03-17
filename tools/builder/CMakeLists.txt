# nt_builder static library
add_library(nt_builder STATIC
    nt_builder.c
    nt_builder_hash.c
    nt_builder_dump.c
    nt_builder_mesh.c
    nt_builder_texture.c
    nt_builder_shader.c
    nt_builder_glob.c
)
target_include_directories(nt_builder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nt_builder PUBLIC nt_shared nt_core cgltf stb_image)
# Builder uses standard C I/O (fopen, strdup, strncpy) -- suppress MSVC deprecation
target_compile_definitions(nt_builder PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
nt_set_warning_flags(nt_builder)
nt_set_sanitizer_flags(nt_builder)
set_target_properties(nt_builder PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)

# Builder executable (placeholder, uses nt_builder library)
add_executable(builder main.c)
target_link_libraries(builder PRIVATE nt_builder)
nt_set_warning_flags(builder)
nt_set_sanitizer_flags(builder)
set_target_properties(builder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)
