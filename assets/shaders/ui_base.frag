precision highp float;

// One UI base material for plain sprites AND flat SDF radials, so both share one batch.
// Plain emits bake the material's attr defaults: a_layout = 0, so v_layout.y (bbox px
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
    // Both paths run unconditionally: texture() and fwidth() need uniform control flow.
    vec4 texel = texture(u_texture, v_texcoord); // premultiplied, see sprite.frag
    float coverage = radial_coverage(v_radial, v_layout.x, v_local);
    vec4 c = vec4(v_color.rgb * v_color.a, v_color.a);
    frag_color = (v_layout.y < 0.5) ? texel * c : c * coverage;
}
