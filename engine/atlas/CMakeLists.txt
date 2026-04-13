nt_add_module(nt_atlas LOG_DOMAIN "atlas"
    nt_atlas.c
    nt_atlas.h
)
target_link_libraries(nt_atlas PUBLIC nt_resource)
target_link_libraries(nt_atlas PRIVATE nt_hash)
