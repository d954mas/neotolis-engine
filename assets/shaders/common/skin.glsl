#pragma once

precision highp float;
precision highp int;

#include "instance.glsl"

layout(location = 8) in vec4 a_joints;
layout(location = 9) in vec4 a_weights;

uniform highp sampler2D u_skin_matrices;

/* Skinned renderer payload (nt_skinned_instance_t): affine world rows, the two
 * frame origins in u_skin_matrices, their blend alpha, drawable colour. */
struct nt_skinned_instance_t {
    mat4 world;
    vec4 frames;
    float alpha;
    vec4 color;
};

nt_skinned_instance_t nt_skinned_instance() {
    mat4 world = nt_instance_world(nt_instance_texel(6, 0), nt_instance_texel(6, 1), nt_instance_texel(6, 2));
    return nt_skinned_instance_t(world, nt_instance_texel(6, 3), nt_instance_texel(6, 4).x, nt_instance_texel(6, 5));
}

const float NT_SKIN_VECTOR_EPSILON = 1e-12;
const float NT_SKIN_VECTOR_BIG = 1e30;

mat4x3 nt_skin_joint_matrix(int joint, ivec2 origin) {
    int x = origin.x + joint * 3;
    vec4 row0 = texelFetch(u_skin_matrices, ivec2(x, origin.y), 0);
    vec4 row1 = texelFetch(u_skin_matrices, ivec2(x + 1, origin.y), 0);
    vec4 row2 = texelFetch(u_skin_matrices, ivec2(x + 2, origin.y), 0);
    return mat4x3(
        vec3(row0.x, row1.x, row2.x),
        vec3(row0.y, row1.y, row2.y),
        vec3(row0.z, row1.z, row2.z),
        vec3(row0.w, row1.w, row2.w)
    );
}

mat4x3 nt_skin_joint_pair(int joint, vec4 frames, float alpha) {
    mat4x3 a = nt_skin_joint_matrix(joint, ivec2(frames.xy));
    mat4x3 b = nt_skin_joint_matrix(joint, ivec2(frames.zw));
    return a * (1.0 - alpha) + b * alpha;
}

mat4x3 nt_skin_blended_matrix(nt_skinned_instance_t inst) {
    return a_weights.x * nt_skin_joint_pair(int(a_joints.x + 0.5), inst.frames, inst.alpha) +
           a_weights.y * nt_skin_joint_pair(int(a_joints.y + 0.5), inst.frames, inst.alpha) +
           a_weights.z * nt_skin_joint_pair(int(a_joints.z + 0.5), inst.frames, inst.alpha) +
           a_weights.w * nt_skin_joint_pair(int(a_joints.w + 0.5), inst.frames, inst.alpha);
}

mat3 nt_skin_linear(mat4x3 transform) {
    return mat3(transform[0], transform[1], transform[2]);
}

vec3 nt_skin_transform_position(mat4x3 skin, vec3 position) {
    return skin * vec4(position, 1.0);
}

vec3 nt_skin_safe_normalize(vec3 value, vec3 fallback) {
    float len2 = dot(value, value);
    if (len2 > NT_SKIN_VECTOR_EPSILON && len2 < NT_SKIN_VECTOR_BIG) {
        return value * inversesqrt(len2);
    }
    return fallback;
}

vec3 nt_skin_transform_normal(mat4x3 skin, mat3 world, vec3 normal) {
    return nt_skin_safe_normalize(world * nt_skin_linear(skin) * normal, vec3(0.0, 1.0, 0.0));
}

vec3 nt_skin_orthogonal_fallback(vec3 normal) {
    vec3 axis = abs(normal.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    return nt_skin_safe_normalize(cross(axis, normal), vec3(0.0, 0.0, 1.0));
}

vec3 nt_skin_transform_tangent(mat4x3 skin, mat3 world, vec3 tangent, vec3 final_normal) {
    vec3 transformed = world * nt_skin_linear(skin) * tangent;
    vec3 orthogonal = transformed - final_normal * dot(final_normal, transformed);
    return nt_skin_safe_normalize(orthogonal, nt_skin_orthogonal_fallback(final_normal));
}
