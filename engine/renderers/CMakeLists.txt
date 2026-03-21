nt_add_module(nt_shape_renderer LOG_DOMAIN "shape_renderer"
    nt_shape_renderer.c
    nt_shape_renderer.h
)
target_link_libraries(nt_shape_renderer PUBLIC nt_core nt_math)
# Note: nt_gfx linkage provided by consumer (game links nt_gfx, tests link nt_gfx_stub)

nt_add_module(nt_mesh_renderer LOG_DOMAIN "mesh_renderer"
    nt_mesh_renderer.c
    nt_mesh_renderer.h
)
target_link_libraries(nt_mesh_renderer PUBLIC
    nt_core nt_render nt_entity nt_transform_comp nt_mesh_comp nt_material_comp nt_drawable_comp nt_material
)
# Note: nt_gfx linkage provided by consumer (game links nt_gfx, tests link nt_gfx_stub)
