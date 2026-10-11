precision highp float;

// One UI base material for plain sprites AND flat SDF radials, so both share one batch.
// Requires the material's a_layout default = 0: plain emits bake it, so v_layout.y (bbox px
// width, walker-injected for custom widgets) is 0 and the texel path runs.
#include "common/radial_sdf.glsl"

uniform sampler2D u_texture;

in vec2 v_texcoord;
in vec4 v_color;
in vec4 v_radial;
in vec4 v_layout;
in vec2 v_local;

out vec4 frag_color;

void main() {
    // texture() and fwidth() stay in uniform control flow; only the derivative-free wedge
    // math branches, per emit (v_layout is constant across an emit), so plain sprites skip it.
    vec4 texel = texture(u_texture, v_texcoord); // premultiplied, see sprite.frag
    vec2 p = v_local * vec2(1.0, v_layout.x);
    float r = length(p);
    float dr = fwidth(r);
    vec4 c = vec4(v_color.rgb * v_color.a, v_color.a);
    if (v_layout.y < 0.5) {
        frag_color = texel * c;
    } else {
        frag_color = c * radial_coverage_at(v_radial, p, r, 1.0 / max(dr, 1e-6));
    }
}
