precision highp float;
precision highp int;

// UBO contract: same as sprite.vert — register "Globals" (common/globals.glsl)
// at slot 0 and update + bind the frame UBO every frame before draw_list.
#include "common/globals.glsl"

// Base locations match nt_attr_location_t in engine/graphics/nt_gfx.h. The custom
// attrs below are bound per-material by attr_map presence (NOT a flag) — a material
// that omits one leaves its location unbound, reading the disabled-attr default.
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;
// loc 4 (a_radial): x=angle_start y=angle_end z=inner_radius_norm w=0.
layout(location = 4) in vec4 a_radial;
// loc 5 (a_tint): rgb=reveal tint, w=tint_strength, 0..1. radial_image only.
layout(location = 5) in vec4 a_tint;
// loc 6 (a_uvrect): region min/max atlas UV {u0,v0,u1,v1}. radial_image only.
layout(location = 6) in vec4 a_uvrect;
// loc 7 (a_layout): walker-injected x=aspect, yz=bbox px size, w=atlas D4 value.
layout(location = 7) in vec4 a_layout;

out vec2 v_texcoord;
out vec4 v_color;
out vec4 v_radial;
out vec4 v_tint;
out vec4 v_layout;
out vec2 v_local_uv;

void main() {
    gl_Position = view_proj * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
    v_radial = a_radial;
    v_tint = a_tint;
    v_layout = a_layout;

    // Builder maps source -> atlas as diagonal, flipH, flipV. Undo in reverse.
    vec2 uv = (a_texcoord - a_uvrect.xy) / max(a_uvrect.zw - a_uvrect.xy, vec2(1e-6));
    int d4 = int(a_layout.w + 0.5);
    if ((d4 & 1) != 0) uv.x = 1.0 - uv.x;
    if ((d4 & 2) != 0) uv.y = 1.0 - uv.y;
    if ((d4 & 4) != 0) uv = uv.yx;
    v_local_uv = uv * 2.0 - 1.0;
}
