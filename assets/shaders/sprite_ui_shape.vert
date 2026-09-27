precision highp float;
precision highp int;

#include "common/globals.glsl"
#include "common/ui_shape_vertex.glsl"

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;
layout(location = 4) in vec4 a_shape_layout;
layout(location = 5) in vec4 a_shape_geometry;
layout(location = 6) in vec4 a_shape_widths;
layout(location = 7) in float a_shape_center_y;
layout(location = 8) in vec4 a_shape_endpoint;
layout(location = 9) in vec4 a_shape_border;
layout(location = 10) in vec4 a_shape_control;

out vec2 v_texcoord;
out vec4 v_color;
out vec2 v_local;
flat out vec4 v_layout;
flat out vec4 v_geometry;
flat out vec2 v_paint;
flat out vec4 v_fill;
flat out vec4 v_endpoint;
flat out vec4 v_border;
flat out vec4 v_widths;
flat out vec4 v_inner_strip;
flat out vec3 v_expansion;
out vec3 v_clip;

void main() {
    gl_Position = view_proj * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
    v_layout = vec4(a_shape_layout.xy, 0.0, 0.0);
    v_geometry = a_shape_geometry;
    v_paint = vec2(0.0);
    v_widths = a_shape_widths;
    v_inner_strip = vec4(0.0);
    v_expansion = vec3(0.0);
    v_clip = vec3(0.0);
    v_local = vec2(0.0);
    v_fill = vec4(0.0);
    v_endpoint = vec4(0.0);
    v_border = vec4(0.0);
    if (a_shape_control.y == 0.0) {
        return;
    }
    uint mode = uint(a_shape_control.y);
    uint flags = uint(a_shape_control.w);
    bool projective = (flags & 2u) != 0u;
    v_layout.zw = a_shape_control.yz;
    if (mode == 1u) {
        v_paint.x = float(flags & 1u);
        if (v_paint.x == 0.0) {
            v_inner_strip = nt_ui_shape_inner_strip(a_shape_layout.xy, a_shape_geometry, a_shape_widths);
        }
    } else if (mode == 3u) {
        v_paint = a_shape_widths.xy;
    }
    // Shape quad base is aligned to four; corners are TL, TR, BR, BL.
    int corner = gl_VertexID & 3;
    vec2 corner_uv = vec2((corner == 1 || corner == 2) ? 1.0 : 0.0, corner >= 2 ? 1.0 : 0.0);
    float padding = projective ? (mode == 3u ? a_shape_widths.z : 0.0) : a_shape_layout.z;
    v_local = corner_uv * (a_shape_layout.xy + 2.0 * padding) - padding;
    if (projective) {
        v_expansion = vec3(a_shape_layout.w, a_shape_center_y, a_shape_layout.z);
        gl_Position.xy = v_expansion.z * gl_Position.xy + (1.0 - v_expansion.z) * v_expansion.xy * gl_Position.w;
        v_clip = gl_Position.xyz;
        gl_Position.z = 0.0;
    }
    float fill_alpha = a_shape_control.x / 255.0;
    v_fill = vec4(a_color.rgb * fill_alpha, fill_alpha) * a_color.a;
    if (mode == 3u) {
        return;
    }
    v_endpoint = vec4(a_shape_endpoint.rgb * a_shape_endpoint.a, a_shape_endpoint.a) * a_color.a;
    v_border = vec4(a_shape_border.rgb * a_shape_border.a, a_shape_border.a) * a_color.a;
}
