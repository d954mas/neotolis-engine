nt_add_module(nt_material
    nt_material.c
    nt_material.h
)
target_link_libraries(nt_material PUBLIC nt_resource nt_hash nt_pool)
target_link_libraries(nt_material PRIVATE nt_log)
