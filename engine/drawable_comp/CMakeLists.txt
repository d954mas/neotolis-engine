nt_add_module(nt_drawable_comp
    nt_drawable_comp.c
    nt_drawable_comp.h
)
target_link_libraries(nt_drawable_comp PUBLIC nt_comp_storage nt_hash)
