precision highp float;
precision highp int;

#include "common/globals.glsl"
#include "common/ui_shape_vertex.glsl"

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;
layout(location = 4) in vec4 a_shape_layout;
layout(location = 5) in vec4 a_shape_geometry;
layout(location = 6) in vec4 a_shape_paint;
layout(location = 7) in vec4 a_shape_border;

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

uvec2 nt_ui_shape_unpack_payload(float encoded) {
    float magnitude = abs(encoded);
    uint high_flag = magnitude >= 4294967296.0 ? 2u : 0u;
    float scale = high_flag != 0u ? 2.3283064365386963e-10 : 1.0;
    uint flags = (encoded < 0.0 ? 1u : 0u) | high_flag;
    return uvec2(uint(magnitude * scale - 1.0), flags);
}

vec3 nt_ui_shape_unpack_rgb(float packed_value) {
    uint bits = uint(packed_value);
    return vec3(uvec3(bits, bits >> 8u, bits >> 16u) & uvec3(255u)) / 255.0;
}

void main() {
    gl_Position = view_proj * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
    v_layout = vec4(a_shape_layout.xy, 0.0, 0.0);
    v_geometry = a_shape_geometry;
    v_paint = vec2(0.0);
    v_widths = a_shape_border;
    v_inner_strip = vec4(0.0);
    v_expansion = vec3(0.0);
    v_clip = vec3(0.0);
    v_local = vec2(0.0);
    v_fill = vec4(0.0);
    v_endpoint = vec4(0.0);
    v_border = vec4(0.0);
    if (a_shape_paint.y == 0.0) {
        return;
    }
    uvec2 endpoint_payload = nt_ui_shape_unpack_payload(a_shape_paint.y);
    uvec2 border_payload = nt_ui_shape_unpack_payload(a_shape_paint.z);
    uvec2 alpha_payload = nt_ui_shape_unpack_payload(a_shape_paint.w);
    uint mode = endpoint_payload.y;
    bool projective = (alpha_payload.y & 2u) != 0u;
    v_layout.zw = vec2(float(mode), float(border_payload.y));
    if (mode == 1u) {
        v_paint.x = float(alpha_payload.y & 1u);
        if (v_paint.x == 0.0) {
            v_inner_strip = nt_ui_shape_inner_strip(a_shape_layout.xy, a_shape_geometry, a_shape_border);
        }
    } else if (mode == 3u) {
        v_paint = a_shape_border.xy;
    }
    // Shape quad base is aligned to four; corners are TL, TR, BR, BL.
    int corner = gl_VertexID & 3;
    vec2 corner_uv = vec2((corner == 1 || corner == 2) ? 1.0 : 0.0, corner >= 2 ? 1.0 : 0.0);
    float padding = projective ? (mode == 3u ? a_shape_border.z : 0.0) : a_shape_layout.z;
    v_local = corner_uv * (a_shape_layout.xy + 2.0 * padding) - padding;
    if (projective) {
        v_expansion = vec3(a_shape_layout.w, a_shape_paint.x, a_shape_layout.z);
        gl_Position.xy = v_expansion.z * gl_Position.xy + (1.0 - v_expansion.z) * v_expansion.xy * gl_Position.w;
        v_clip = gl_Position.xyz;
        gl_Position.z = 0.0;
    }
    if (mode == 3u) {
        float alpha = float(alpha_payload.x & 255u) / 255.0;
        v_fill = vec4(a_color.rgb * alpha, alpha) * a_color.a;
        return;
    }
    vec3 alpha = nt_ui_shape_unpack_rgb(float(alpha_payload.x));
    v_fill = vec4(a_color.rgb * alpha.x, alpha.x) * a_color.a;
    v_endpoint = vec4(nt_ui_shape_unpack_rgb(float(endpoint_payload.x)) * alpha.y, alpha.y) * a_color.a;
    v_border = vec4(nt_ui_shape_unpack_rgb(float(border_payload.x)) * alpha.z, alpha.z) * a_color.a;
}
