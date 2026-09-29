precision highp float;
precision highp int;

#include "../../assets/shaders/common/skin.glsl"

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;

out vec3 v_position_probe;
out vec3 v_normal_probe;
out vec3 v_tangent_probe;
out vec4 v_color_probe;

mat3 world_linear(mat4 world) {
    return mat3(world);
}

vec3 world_position(mat4 world, vec3 position) {
    return (world * vec4(position, 1.0)).xyz;
}

void main() {
    nt_skinned_instance_t inst = nt_skinned_instance();
    mat4x3 skin = nt_skin_blended_matrix(inst);
    vec3 skinned_position = nt_skin_transform_position(skin, a_position);
    vec3 position = world_position(inst.world, skinned_position);
    mat3 linear = world_linear(inst.world);
    vec3 normal = nt_skin_transform_normal(skin, linear, a_normal);
    vec3 tangent = nt_skin_transform_tangent(skin, linear, a_tangent.xyz, normal);

    v_position_probe = position * 0.25 + 0.5;
    v_normal_probe = normal * 0.25 + 0.5;
    v_tangent_probe = tangent * 0.25 + 0.5;
    v_color_probe = inst.color;
    gl_Position = vec4(position, 1.0);
}
