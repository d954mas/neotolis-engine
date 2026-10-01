precision highp float;
precision highp int;

// Game-owned paint over the engine shape instance: the engine BOX plus a white
// checker inside it. style.user = {cell px, checker alpha, 0, 0}.
#include "../../../../assets/shaders/common/ui_shape.glsl"

in vec2 v_local;
flat in vec4 v_layout;
flat in vec4 v_geometry;
flat in vec2 v_paint;
flat in vec4 v_fill;
flat in vec4 v_endpoint;
flat in vec4 v_border;
flat in vec4 v_widths;
flat in vec4 v_inner_strip;
flat in vec4 v_user;

out vec4 frag_color;

void main() {
    vec4 color = nt_ui_shape_box(v_local, v_layout, v_geometry, v_widths, v_paint.x, v_inner_strip, v_fill, v_endpoint, v_border);
    vec2 cell = floor(v_local / max(v_user.x, 1.0));
    float on = mod(cell.x + cell.y, 2.0) * v_user.y;
    // Premultiplied white over the shape, limited to its coverage.
    frag_color = vec4(mix(color.rgb, vec3(color.a), on), color.a);
    if (frag_color.a <= 0.0) {
        discard;
    }
}
