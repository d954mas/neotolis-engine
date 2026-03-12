nt_add_module(nt_shape_renderer
    nt_shape_renderer.c
    nt_shape_renderer.h
)
target_link_libraries(nt_shape_renderer PUBLIC nt_core nt_math)
# Note: nt_gfx linkage provided by consumer (game links nt_gfx, tests link nt_gfx_stub)
