precision highp float;
precision highp int;

#include "common/globals.glsl"

// CPU-SIDE PACKING FORMAT (nt_text vertex buffer contract):
// Stride: 64 bytes per vertex, 4 vertices per glyph quad (2 triangles = 6 indices)
// location 0: vec3  a_position     - world-space quad corner (float32 x3, full 3D)
// location 1: vec2  a_texcoord     - em-space coordinate (float32 x2)
// location 2: vec2  a_glyph_data   - packed as floatBitsToUint: (band_row, band_count)
// location 3: vec4  a_glyph_bounds - bbox x0/y0/x1/y1 in em-space (float32 x4)
// location 4: vec4  a_color        - text color RGBA (float32 x4)
// location 5: float a_depth_bias   - per-glyph NDC depth bias toward the near plane (float32)

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec2 a_glyph_data;
layout(location = 3) in vec4 a_glyph_bounds;
layout(location = 4) in vec4 a_color;
layout(location = 5) in float a_depth_bias;

out vec2 v_texcoord;
flat out uvec2 v_glyph;
flat out vec4 v_band_transform; // bbox x0, y0, band_count / width, band_count / height
out vec4 v_color;

void main() {
    v_glyph = floatBitsToUint(a_glyph_data);
    v_texcoord = a_texcoord;
    // Per-vertex divisions so the fragment shader picks a band with one multiply.
    vec2 bands = vec2(float(v_glyph.y));
    v_band_transform = vec4(a_glyph_bounds.xy, bands / (a_glyph_bounds.zw - a_glyph_bounds.xy));
    v_color = a_color;

    gl_Position = view_proj * vec4(a_position, 1.0);
    // Per-glyph bias toward the near plane (view-independent) so depth-writing glyph
    // quads don't z-fight at overlapping AA fringes. 0 = no shift.
    gl_Position.z -= a_depth_bias * gl_Position.w;
}
