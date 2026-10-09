/* Copyright 2019-2021 Evgeny Gorodetskiy
 * Copyright 2026 Neotolis Contributors
 * Licensed under the Apache License, Version 2.0.
 * https://www.apache.org/licenses/LICENSE-2.0
 * Distributed AS IS, without warranties or conditions of any kind.
 * Modified: Methane Asteroids AsteroidVS, per-draw data in vertex attributes,
 * and explicit zero-to-one to OpenGL clip-depth conversion. Generated rocks
 * span radii [0.525, 0.9], the deep-to-shallow color range. */
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 4) in vec4 a_world_row0;
layout(location = 5) in vec4 a_world_row1;
layout(location = 6) in vec4 a_world_row2;
layout(location = 7) in vec4 a_deep;
layout(location = 8) in vec4 a_shallow;
#include "../../../assets/shaders/common/globals.glsl"
out vec3 v_world;
out vec3 v_normal;
out vec3 v_albedo;
out vec3 v_uvw;
out vec3 v_weights;
void main() {
    vec4 position = vec4(a_position, 1.0);
    v_world = vec3(dot(a_world_row0, position), dot(a_world_row1, position), dot(a_world_row2, position));
    v_normal = normalize(vec3(dot(a_world_row0.xyz, a_normal), dot(a_world_row1.xyz, a_normal), dot(a_world_row2.xyz, a_normal)));
    float depth = clamp((length(a_position) - 0.525) / (0.9 - 0.525), 0.0, 1.0);
    v_albedo = mix(a_deep.xyz, a_shallow.xyz, depth);
    v_uvw = a_position / 0.9 * 0.5 + 0.5;
    v_weights = clamp((abs(normalize(a_position)) - 0.2) * 7.0, 0.0, 1.0);
    v_weights /= v_weights.x + v_weights.y + v_weights.z;
    gl_Position = view_proj * vec4(v_world, 1.0);
    gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;
}
