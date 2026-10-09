precision highp float;
precision highp int;

// UBO contract: this shader uses "Globals" (defined in common/globals.glsl)
// for view_proj. The game declares the block in nt_gfx_desc_t.global_blocks at
// slot 0 and binds its view block with nt_gfx_bind_uniform_block(0, ...) in
// every pass before nt_sprite_renderer_draw_list; the renderer binds no uniform
// blocks itself.
#include "common/globals.glsl"

// Locations match nt_attr_location_t in engine/graphics/nt_gfx.h.
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;

out vec2 v_texcoord;
out mediump vec4 v_color;

void main() {
    gl_Position = view_proj * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
}
