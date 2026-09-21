nt_add_module(nt_skin_comp LOG_DOMAIN "skin_comp"
    nt_skin_comp.c
    nt_skin_comp.h
)
# nt_skeletal_gpu only for the nt_deformation_binding_t type.
target_link_libraries(nt_skin_comp PUBLIC nt_comp_storage nt_skeletal_gpu)
