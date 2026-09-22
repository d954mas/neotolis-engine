#pragma once

precision highp float;
precision highp int;

layout(location = 8) in vec4 a_joints;
layout(location = 9) in vec4 a_weights;
layout(location = 10) in vec4 a_world_row0;
layout(location = 11) in vec4 a_world_row1;
layout(location = 12) in vec4 a_world_row2;
layout(location = 13) in vec4 a_color;
layout(location = 14) in vec4 a_skin_frames;
layout(location = 15) in float a_skin_alpha;

uniform highp sampler2D u_skin_matrices;

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

mat4x3 nt_skin_joint_pair(int joint) {
    ivec2 frame0 = ivec2(a_skin_frames.xy);
    ivec2 frame1 = ivec2(a_skin_frames.zw);
    mat4x3 a = nt_skin_joint_matrix(joint, frame0);
    mat4x3 b = nt_skin_joint_matrix(joint, frame1);
    return a * (1.0 - a_skin_alpha) + b * a_skin_alpha;
}

mat4x3 nt_skin_blended_matrix() {
    return a_weights.x * nt_skin_joint_pair(int(a_joints.x + 0.5)) +
           a_weights.y * nt_skin_joint_pair(int(a_joints.y + 0.5)) +
           a_weights.z * nt_skin_joint_pair(int(a_joints.z + 0.5)) +
           a_weights.w * nt_skin_joint_pair(int(a_joints.w + 0.5));
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
