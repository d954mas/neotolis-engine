nt_add_module(nt_skeletal_gpu LOG_DOMAIN "skeletal_gpu"
    nt_skeletal_gpu.c
    nt_skeletal_gpu.h
)
# nt_skeletal only for the nt_skeletal_mat34_t layout; no kernel is called.
target_link_libraries(nt_skeletal_gpu PUBLIC nt_core nt_skeletal nt_gfx_interface)
# Note: nt_gfx linkage provided by consumer (game links nt_gfx, tests link nt_gfx_fake)
