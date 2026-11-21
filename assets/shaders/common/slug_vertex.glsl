#pragma once

// Text vertex inputs of nt_text_renderer and the varyings common/slug_coverage.glsl reads.
// a_glyph_data and a_glyph_bounds hold exact integers; a_color is normalized.
layout(location = 0) in highp vec3 a_position;
layout(location = 1) in highp vec2 a_texcoord;     // em space
layout(location = 2) in highp vec2 a_glyph_data;   // band_row, band_count
layout(location = 3) in highp vec4 a_glyph_bounds; // bbox x0, y0, x1, y1 in em
layout(location = 4) in vec4 a_color;

out highp vec2 v_texcoord;
flat out highp uvec2 v_glyph;
flat out highp vec4 v_band_transform; // bbox x0, y0, band_count / width, band_count / height
out mediump vec4 v_color;

void SlugVertex() {
    v_texcoord = a_texcoord;
    v_glyph = uvec2(a_glyph_data);
    // Divide per vertex so the fragment shader picks a band without dividing.
    v_band_transform = vec4(a_glyph_bounds.xy, a_glyph_data.yy / (a_glyph_bounds.zw - a_glyph_bounds.xy));
    v_color = a_color;
}
