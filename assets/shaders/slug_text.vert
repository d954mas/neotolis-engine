precision highp float;
precision highp int;

#include "common/globals.glsl"

// CPU-SIDE PACKING FORMAT (Phase 45 nt_text vertex buffer contract):
// Stride: 48 bytes per vertex, 4 vertices per glyph quad (2 triangles = 6 indices)
// location 0: vec2  a_position    - screen/world quad corner (float32 x2)
// location 1: vec2  a_texcoord    - em-space coordinate (float32 x2)
// location 2: vec4  a_glyph_data  - packed as floatBitsToUint: (curve_offset, band_row, curve_count, band_count)
// location 3: vec4  a_glyph_bounds - bbox x0/y0/x1/y1 in em-space (float32 x4)
// location 4: vec4  a_color       - text color RGBA (float32 x4)

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec4 a_glyph_data;
layout(location = 3) in vec4 a_glyph_bounds;
layout(location = 4) in vec4 a_color;

out vec2 v_texcoord;
flat out uvec4 v_glyph;
flat out vec4 v_glyph_bounds;
out vec4 v_color;

uniform float u_dilation; // Em-space dilation amount (0.5 * pixel_size_in_em)

void main() {
    // Unpack integer glyph data from float bits (GLSL ES 3.00 floatBitsToUint)
    v_glyph = floatBitsToUint(a_glyph_data);

    // Pass through interpolated values
    v_texcoord = a_texcoord;
    v_glyph_bounds = a_glyph_bounds;
    v_color = a_color;

    // Apply dilation: expand quad outward to prevent edge clipping
    vec2 pos = a_position;
    vec2 tc = a_texcoord;
    if (u_dilation > 0.0) {
        // Compute expansion direction from texcoord relative to glyph center
        vec2 center = (a_glyph_bounds.xy + a_glyph_bounds.zw) * 0.5;
        vec2 dir = sign(a_texcoord - center);
        tc += dir * u_dilation;

        // Scale position by same ratio
        vec2 bbox_size = a_glyph_bounds.zw - a_glyph_bounds.xy;
        vec2 scale = vec2(1.0);
        if (bbox_size.x > 0.0) scale.x = 1.0 + (2.0 * u_dilation) / bbox_size.x;
        if (bbox_size.y > 0.0) scale.y = 1.0 + (2.0 * u_dilation) / bbox_size.y;
        vec2 pos_center = (a_glyph_bounds.xy + a_glyph_bounds.zw) * 0.5;
        pos = pos_center + (a_position - pos_center) * scale;
    }

    v_texcoord = tc;
    gl_Position = view_proj * vec4(pos, 0.0, 1.0);
}
