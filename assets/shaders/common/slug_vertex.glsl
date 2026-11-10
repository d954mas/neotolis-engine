#pragma once

// Text vertex inputs and the varyings common/slug_coverage.glsl consumes, shared by slug_text.vert and
// game text vertex variants. A variant includes this file, calls SlugVertex() and writes gl_Position
// from a_position; it never redeclares or decodes the inputs, so a layout change stays in this file.
//
// nt_text_renderer writes 36 bytes per vertex, 4 vertices per glyph quad:
// location 0: vec3 a_position     - world-space quad corner (float32 x3)
// location 1: vec2 a_texcoord     - em-space coordinate (float32 x2)
// location 2: vec2 a_glyph_data   - band_row, band_count: exact integers (uint16 x2, not normalized)
// location 3: vec4 a_glyph_bounds - bbox x0/y0/x1/y1 in em: exact integers (int16 x4, not normalized)
// location 4: vec4 a_color        - RGBA (uint8 x4, normalized)
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec2 a_glyph_data;
layout(location = 3) in vec4 a_glyph_bounds;
layout(location = 4) in vec4 a_color;

out vec2 v_texcoord;
flat out uvec2 v_glyph;
flat out vec4 v_band_transform; // bbox x0, y0, band_count / width, band_count / height
out mediump vec4 v_color;

void SlugVertex() {
    v_texcoord = a_texcoord;
    v_glyph = uvec2(a_glyph_data);
    // Divide per vertex so the fragment shader picks a band without dividing.
    v_band_transform = vec4(a_glyph_bounds.xy, a_glyph_data.yy / (a_glyph_bounds.zw - a_glyph_bounds.xy));
    v_color = a_color;
}
