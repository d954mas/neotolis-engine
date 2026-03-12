nt_add_module(nt_shape
    nt_shape.c
    nt_shape.h
)
target_link_libraries(nt_shape PUBLIC nt_core)
# Note: nt_gfx linkage provided by consumer (game links nt_gfx, tests link nt_gfx_stub)
