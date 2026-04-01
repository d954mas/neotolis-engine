precision highp float;
precision highp int;

#include "common/globals.glsl"

// CPU-SIDE PACKING FORMAT (Phase 45 nt_text vertex buffer contract):
// Stride: 68 bytes per vertex, 4 vertices per glyph quad (2 triangles = 6 indices)
// location 0: vec3  a_position     - world-space quad corner (float32 x3, full 3D)
// location 1: vec2  a_texcoord     - em-space coordinate (float32 x2)
// location 2: vec4  a_glyph_data   - packed as floatBitsToUint: (curve_offset_y, band_row, curve_offset_x, band_count)
// location 3: vec4  a_glyph_bounds - bbox x0/y0/x1/y1 in em-space (float32 x4)
// location 4: vec4  a_color        - text color RGBA (float32 x4)

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec4 a_glyph_data;
layout(location = 3) in vec4 a_glyph_bounds;
layout(location = 4) in vec4 a_color;

out vec2 v_texcoord;
flat out uvec4 v_glyph;
flat out vec4 v_glyph_bounds;
out vec4 v_color;

void main() {
    v_glyph = floatBitsToUint(a_glyph_data);
    v_texcoord = a_texcoord;
    v_glyph_bounds = a_glyph_bounds;
    v_color = a_color;

    gl_Position = view_proj * vec4(a_position, 1.0);
}
