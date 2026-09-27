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
// loc 4 (a_radial): x=angle_start y=angle_end z=inner_radius_norm.
layout(location = 4) in vec4 a_radial;
// loc 5 (a_tint): rgb=reveal tint, w=tint_strength, 0..1. radial_image only.
layout(location = 5) in vec4 a_tint;
// loc 6: source-image coordinates from the sprite renderer, independent of atlas packing.
layout(location = 6) in vec2 a_source_uv;
// loc 7: walker-injected bounding-box aspect.
layout(location = 7) in float a_aspect;

out vec2 v_texcoord;
out vec4 v_color;
out vec4 v_radial;
out vec4 v_tint;
out vec2 v_local_uv;
out float v_aspect;

void main() {
    gl_Position = view_proj * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
    v_radial = a_radial;
    v_tint = a_tint;
    v_aspect = a_aspect;
    v_local_uv = a_source_uv * 2.0 - 1.0;
}
