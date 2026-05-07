precision highp float;
precision highp int;

// UBO contract: this shader uses "Globals" (defined in common/globals.glsl)
// for view_proj. Game must register the block via
//   nt_gfx_register_global_block("Globals", 0)
// at startup, and update + bind the frame UBO to slot 0 every frame before
// nt_sprite_renderer_draw_list. nt_sprite_renderer doesn't bind UBOs itself —
// that's the game's responsibility per the shader contract.
#include "common/globals.glsl"

// Locations match nt_attr_location_t in engine/graphics/nt_gfx.h.
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;

out vec2 v_texcoord;
out vec4 v_color;

void main() {
    gl_Position = view_proj * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
}
