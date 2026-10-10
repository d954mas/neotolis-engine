/* Copyright 2019-2021 Evgeny Gorodetskiy
 * Copyright 2026 Neotolis Contributors
 * Licensed under the Apache License, Version 2.0.
 * https://www.apache.org/licenses/LICENSE-2.0
 * Distributed AS IS, without warranties or conditions of any kind.
 * Modified: Methane Asteroids PlanetVS translated to GLSL and OpenGL depth. */
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 4) in vec4 a_world_row0;
layout(location = 5) in vec4 a_world_row1;
layout(location = 6) in vec4 a_world_row2;
#include "../../../assets/shaders/common/globals.glsl"
out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;
void main() {
    vec4 position = vec4(a_position, 1.0);
    v_world = vec3(dot(a_world_row0, position), dot(a_world_row1, position), dot(a_world_row2, position));
    v_normal = normalize(vec3(dot(a_world_row0.xyz, a_normal), dot(a_world_row1.xyz, a_normal), dot(a_world_row2.xyz, a_normal)));
    v_uv = a_uv;
    gl_Position = view_proj * vec4(v_world, 1.0);
    gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;
}
