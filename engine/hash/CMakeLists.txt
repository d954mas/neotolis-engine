nt_add_module(nt_hash
    nt_hash.c
    nt_hash.h
)

target_link_libraries(nt_hash PUBLIC nt_core)

# strncpy used in label table (debug feature) -- suppress MSVC deprecation
target_compile_definitions(nt_hash PRIVATE _CRT_SECURE_NO_WARNINGS)
