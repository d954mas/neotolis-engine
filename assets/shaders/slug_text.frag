precision highp float;
precision highp int;

#include "common/slug_coverage.glsl"

in vec2 v_texcoord;
flat in uvec2 v_glyph;         // band_row, band_count
flat in vec4 v_band_transform; // bbox x0, y0, band_count / width, band_count / height (em-space)
in vec4 v_color;

out vec4 frag_color;

// No discard: an empty premultiplied pixel already changes nothing, and discard blocks
// hidden-surface removal on some tile GPUs. Depth-writing text uses slug_text_depth.frag.
void main() {
    float coverage = SlugRender(v_texcoord, v_glyph, v_band_transform);

    // Premultiplied alpha output
    float alpha = coverage * v_color.a;
    frag_color = vec4(v_color.rgb * alpha, alpha);
}
