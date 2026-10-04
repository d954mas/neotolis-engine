precision highp float;
precision highp int;

#include "common/slug_coverage.glsl"

in vec2 v_texcoord;
flat in uvec2 v_glyph;         // band_row, band_count
flat in vec4 v_band_transform; // bbox x0, y0, band_count / width, band_count / height (em-space)
in vec4 v_color;

out vec4 frag_color;

// For text that writes depth: empty quad pixels must not occlude what is behind them.
// Kept out of slug_text.frag because discard costs early depth / hidden-surface removal on tile GPUs.
void main() {
    float coverage = SlugRender(v_texcoord, v_glyph, v_band_transform);

    if (coverage < 1.0 / 255.0)
        discard;

    // Premultiplied alpha output
    float alpha = coverage * v_color.a;
    frag_color = vec4(v_color.rgb * alpha, alpha);
}
