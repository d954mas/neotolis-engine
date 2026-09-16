nt_add_module(nt_skeletal LOG_DOMAIN "skeletal"
    nt_skeletal.c
    nt_skeletal.h
)

target_link_libraries(nt_skeletal PUBLIC nt_core nt_hash)

target_compile_definitions(nt_skeletal PUBLIC NT_SKELETAL_CHECKS=$<BOOL:${NT_SKELETAL_CHECKS}>)
