nt_add_module(nt_material_comp
    nt_material_comp.c
    nt_material_comp.h
)
target_link_libraries(nt_material_comp PUBLIC nt_comp_storage)
