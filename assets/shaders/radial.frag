precision highp float;

// Flat SDF arc/sector/ring/oval on a plain quad. No texture sample — the shape is
// per-pixel. v_radial = {angle_start, angle_end, inner_radius_norm, 0}; aspect comes
// from v_layout.x (walker-injected bbox w/h). v_local is the [-1,1] quad-local coord.
#include "common/radial_sdf.glsl"

in vec2 v_texcoord;
in vec4 v_color;
in vec4 v_radial;
in vec4 v_layout;
in vec2 v_local;

out vec4 frag_color;

void main() {
    float a = radial_coverage(v_radial, v_layout.x, v_local) * v_color.a;
    if (a <= 0.0) {
        discard;
    }
    // Premultiply: the sprite pipeline blends (ONE, ONE_MINUS_SRC_ALPHA), which
    // expects src.rgb pre-scaled by src.a.
    frag_color = vec4(v_color.rgb * a, a);
}
