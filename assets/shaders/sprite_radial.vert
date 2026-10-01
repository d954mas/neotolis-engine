precision highp float;
precision highp int;

// UBO contract: same as sprite.vert — register "Globals" (common/globals.glsl)
// at slot 0 and update + bind the frame UBO every frame before draw_list.
#include "common/globals.glsl"

// Base locations match nt_attr_location_t in engine/graphics/nt_gfx.h; the tail is
// nt_ui_radial_image_tail_t, written by the UI walker.
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;
// x=angle_start y=angle_end z=inner_radius_norm w=bbox width/height.
layout(location = 4) in vec4 a_radial;
// rgb=reveal tint, w=tint_strength, 0..1.
layout(location = 5) in vec4 a_tint;
// Atlas UV -> source-image coordinates (x right, y down), independent of trim and D4 packing.
layout(location = 6) in vec3 a_source_u;
layout(location = 7) in vec3 a_source_v;

out vec2 v_texcoord;
out vec4 v_color;
out vec4 v_radial;
out vec4 v_tint;
out vec2 v_local_uv;

void main() {
    gl_Position = view_proj * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
    v_radial = a_radial;
    v_tint = a_tint;
    vec3 uv1 = vec3(a_texcoord, 1.0);
    v_local_uv = vec2(dot(a_source_u, uv1), dot(a_source_v, uv1)) * 2.0 - 1.0;
}
