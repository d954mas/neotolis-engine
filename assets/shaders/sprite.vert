precision highp float;
precision highp int;

#include "common/globals.glsl"

// Sprite vertex shader (Phase 50 D-21 — game-shipped, not engine).
// Locations match nt_attr_location_t enum (engine/graphics/nt_gfx.h):
//   0 = NT_ATTR_POSITION  (vec3 — 12 B)
//   2 = NT_ATTR_COLOR     (vec4 from UBYTE4N — 4 B)
//   3 = NT_ATTR_TEXCOORD0 (vec2 — 8 B)
// Total stride 24 B (matches nt_sprite_vertex_t per SPRITE-05).

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
