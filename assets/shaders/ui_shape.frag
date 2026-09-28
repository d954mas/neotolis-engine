precision highp float;
precision highp int;

#include "common/ui_shape.glsl"
#include "common/ui_shape_projective.glsl"

in vec2 v_local;
flat in vec4 v_layout;
flat in vec4 v_geometry;
flat in vec2 v_paint;
flat in vec4 v_fill;
flat in vec4 v_endpoint;
flat in vec4 v_border;
flat in vec4 v_widths;
flat in vec4 v_inner_strip;

in vec3 v_clip;
flat in vec3 v_expansion;

out vec4 frag_color;

void main() {
    gl_FragDepth = gl_FragCoord.z;
    // Flat per-primitive mode keeps derivative control flow uniform within a quad.
    uint mode = uint(v_layout.z);
    if (v_expansion.z > 0.0) {
        frag_color = nt_ui_shape_projective_color(v_local, v_clip, v_expansion, v_layout, v_geometry, v_widths, v_paint, v_inner_strip, v_fill, v_endpoint, v_border, int(mode), gl_FragDepth);
    } else if (mode == 3u) {
        frag_color = nt_ui_shape_shadow(v_local, v_layout, v_geometry, v_paint, v_fill);
    } else if (mode == 2u) {
        frag_color = nt_ui_shape_radial(v_local, v_layout, v_geometry, v_fill, v_endpoint);
    } else {
        frag_color = nt_ui_shape_box(v_local, v_layout, v_geometry, v_widths, v_paint.x, v_inner_strip, v_fill, v_endpoint, v_border);
    }
    if (frag_color.a <= 0.0) {
        discard;
    }
}
