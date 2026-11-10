precision highp float;
precision highp int;

#include "common/slug_coverage.glsl"

out vec4 frag_color;

// No discard: an empty premultiplied pixel already changes nothing, and discard blocks
// hidden-surface removal on some tile GPUs. A depth-writing variant must discard empty pixels.
void main() {
    float coverage = SlugCoverage();

    // Premultiplied alpha output
    float alpha = coverage * v_color.a;
    frag_color = vec4(v_color.rgb * alpha, alpha);
}
