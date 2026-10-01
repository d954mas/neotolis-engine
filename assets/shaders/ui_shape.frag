precision highp float;
precision highp int;

#include "common/ui_shape.glsl"

in vec2 v_local;
flat in vec4 v_layout;
flat in vec4 v_geometry;
flat in vec2 v_paint;
flat in vec4 v_fill;
flat in vec4 v_endpoint;
flat in vec4 v_border;
flat in vec4 v_widths;
flat in vec4 v_inner_strip;

out vec4 frag_color;

void main() {
    // Flat per-primitive mode keeps derivative control flow uniform within a quad.
    uint mode = uint(v_layout.z);
    if (mode == 3u) {
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
