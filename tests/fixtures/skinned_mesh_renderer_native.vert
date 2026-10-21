precision highp float;
precision highp int;

#include "../../assets/shaders/common/skin.glsl"

#include "nt/locations.glsl"
layout(location = NT_LOC_POSITION) in vec3 a_position;
layout(location = NT_LOC_NORMAL) in vec3 a_normal;
layout(location = NT_LOC_TANGENT) in vec4 a_tangent;
layout(location = 14) in vec4 a_param; /* a game layout's per-instance value; only probe mode 4 outputs it, always drawn with that layout */

out vec3 v_position_probe;
out vec3 v_normal_probe;
out vec3 v_tangent_probe;
out vec4 v_color_probe;
out vec4 v_param_probe;

mat3 world_linear() {
    return mat3(
        vec3(a_world_row0.x, a_world_row1.x, a_world_row2.x),
        vec3(a_world_row0.y, a_world_row1.y, a_world_row2.y),
        vec3(a_world_row0.z, a_world_row1.z, a_world_row2.z)
    );
}

vec3 world_position(vec3 position) {
    vec4 p = vec4(position, 1.0);
    return vec3(dot(a_world_row0, p), dot(a_world_row1, p), dot(a_world_row2, p));
}

void main() {
    mat4x3 skin = nt_skin_blended_matrix();
    vec3 skinned_position = nt_skin_transform_position(skin, a_position);
    vec3 position = world_position(skinned_position);
    mat3 linear = world_linear();
    vec3 normal = nt_skin_transform_normal(skin, linear, a_normal);
    vec3 tangent = nt_skin_transform_tangent(skin, linear, a_tangent.xyz, normal);

    v_position_probe = position * 0.25 + 0.5;
    v_normal_probe = normal * 0.25 + 0.5;
    v_tangent_probe = tangent * 0.25 + 0.5;
    v_color_probe = a_color;
    v_param_probe = mix(vec4(1.0, 0.0, 0.0, 1.0), vec4(0.0, 0.0, 1.0, 1.0), a_param.x);
    gl_Position = vec4(position, 1.0);
}
