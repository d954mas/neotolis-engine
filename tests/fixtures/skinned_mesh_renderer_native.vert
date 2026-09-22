precision highp float;
precision highp int;

#include "../../assets/shaders/common/skin.glsl"

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 10) in vec4 a_world_row0;
layout(location = 11) in vec4 a_world_row1;
layout(location = 12) in vec4 a_world_row2;
layout(location = 13) in vec4 a_color;

out vec3 v_position_probe;
out vec3 v_normal_probe;
out vec3 v_tangent_probe;
out vec4 v_color_probe;

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
    gl_Position = vec4(position, 1.0);
}
