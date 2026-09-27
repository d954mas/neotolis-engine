precision highp float;

#include "common/ui_radial.glsl"

// Flat SDF arc/sector/ring/oval on a plain quad. No texture sample — the shape is
// per-pixel. v_radial = {angle_start, angle_end, inner_radius_norm, 0}; aspect comes
// from v_layout.x (walker-injected bbox w/h). v_local is the [-1,1] quad-local coord.
// Mathematical convention: 0 = +X, CCW positive. Crisp AA via fwidth-derived 1px
// pixel coverage, NO facets.

in vec2 v_texcoord;
in vec4 v_color;
in vec4 v_radial;
in vec4 v_layout;
in vec2 v_local;

out vec4 frag_color;

void main() {
    // Oval squash: aspect = w/h re-rounds the test so 0 stays +X on a non-square
    // bbox. r in [0,1] across the disc.
    vec2 p = v_local * vec2(1.0, v_layout.x);
    float a = nt_ui_radial_coverage(p, v_radial.xyz) * v_color.a;
    if (a <= 0.0) {
        discard;
    }
    // Premultiply: the sprite pipeline blends (ONE, ONE_MINUS_SRC_ALPHA), which
    // expects src.rgb pre-scaled by src.a.
    frag_color = vec4(v_color.rgb * a, a);
}
