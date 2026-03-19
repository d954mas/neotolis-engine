nt_add_module(nt_render
    nt_render_items.c
    nt_render_items.h
    nt_render_defs.h
    nt_render_util.c
    nt_render_util.h
)
target_link_libraries(nt_render PUBLIC nt_core nt_entity nt_drawable_comp nt_transform_comp)
# Note: nt_render does NOT link nt_gfx -- consumer provides (same pattern as nt_shape_renderer)
