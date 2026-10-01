precision highp float;
precision highp int;

#include "common/globals.glsl"
#include "common/ui_shape_vertex.glsl"

// One nt_ui_shape_instance_t per instance; locations follow its field order.
layout(location = 0) in vec4 a_origin_width;
layout(location = 1) in vec4 a_axis_x_height;
layout(location = 2) in vec4 a_axis_y_pad;
layout(location = 3) in vec4 a_geometry;
layout(location = 4) in vec4 a_widths;
layout(location = 5) in vec4 a_user;
layout(location = 6) in vec4 a_color;
layout(location = 7) in vec4 a_endpoint;
layout(location = 8) in vec4 a_border;
layout(location = 9) in vec4 a_control;

out vec2 v_local;
flat out vec4 v_layout;
flat out vec4 v_geometry;
flat out vec2 v_paint;
flat out vec4 v_fill;
flat out vec4 v_endpoint;
flat out vec4 v_border;
flat out vec4 v_widths;
flat out vec4 v_inner_strip;
flat out vec4 v_user;

void main() {
    // Indexed quad: gl_VertexID is the corner TL, TR, BR, BL.
    int corner = gl_VertexID;
    vec2 corner_uv = vec2((corner == 1 || corner == 2) ? 1.0 : 0.0, corner >= 2 ? 1.0 : 0.0);
    vec2 size = vec2(a_origin_width.w, a_axis_x_height.w);
    float pad = a_axis_y_pad.w;
    v_local = corner_uv * (size + 2.0 * pad) - pad;
    vec3 world = a_origin_width.xyz + v_local.x * a_axis_x_height.xyz + v_local.y * a_axis_y_pad.xyz;
    gl_Position = view_proj * vec4(world, 1.0);

    uint mode = uint(a_control.y);
    v_layout = vec4(size, a_control.yz);
    v_geometry = a_geometry;
    v_widths = a_widths;
    v_user = a_user;
    v_paint = vec2(0.0);
    v_inner_strip = vec4(0.0);
    if (mode == 1u) {
        v_inner_strip = nt_ui_shape_inner_strip(size, a_geometry, a_widths);
    } else if (mode == 3u) {
        v_paint = a_widths.xy;
    }
    float fill_alpha = a_control.x / 255.0;
    v_fill = vec4(a_color.rgb * fill_alpha, fill_alpha) * a_color.a;
    v_endpoint = vec4(a_endpoint.rgb * a_endpoint.a, a_endpoint.a) * a_color.a;
    v_border = vec4(a_border.rgb * a_border.a, a_border.a) * a_color.a;
}
