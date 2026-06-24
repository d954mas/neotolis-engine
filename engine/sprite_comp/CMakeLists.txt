nt_add_module(nt_sprite_comp LOG_DOMAIN "sprite_comp"
    nt_sprite_comp.c
    nt_sprite_comp.h
)
target_link_libraries(nt_sprite_comp PUBLIC nt_comp_storage)
target_link_libraries(nt_sprite_comp PRIVATE nt_atlas)
# Dev-only: sprite_describe resolves region_hash -> name via the nt_hash label registry. Gated.
if(NT_INTROSPECT_ENABLED)
    target_link_libraries(nt_sprite_comp PRIVATE nt_hash)
endif()
