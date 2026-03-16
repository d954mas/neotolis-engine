nt_add_module(nt_transform_comp
    nt_transform_comp.c
    nt_transform_comp.h
)
target_link_libraries(nt_transform_comp PUBLIC nt_comp_storage nt_math)
