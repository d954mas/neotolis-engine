/* Copyright 2019-2021 Evgeny Gorodetskiy
 * Copyright 2026 Neotolis Contributors
 * Licensed under the Apache License, Version 2.0.
 * https://www.apache.org/licenses/LICENSE-2.0
 * Distributed AS IS, without warranties or conditions of any kind.
 * Modified: translated Methane Asteroids Primitives.hlsl to GLSL ES 3.00. */
#pragma once
vec3 color_linear_to_srgb(vec3 linear_color) {
    vec3 s1 = sqrt(linear_color);
    vec3 s2 = sqrt(s1);
    vec3 s3 = sqrt(s2);
    return 0.662002687 * s1 + 0.684122060 * s2 - 0.323583601 * s3 - 0.0225411470 * linear_color;
}
vec3 phong_color(vec3 texel, vec3 world_position, vec3 world_normal, vec3 eye_position, vec3 light_position) {
    vec3 to_light = normalize(light_position - world_position);
    vec3 to_eye = normalize(eye_position - world_position);
    vec3 reflected = reflect(-to_light, world_normal);
    float diffuse = clamp(dot(to_light, world_normal), 0.0, 1.0);
    float specular = pow(clamp(dot(to_eye, reflected), 0.0, 1.0), 30.0);
    return texel * 0.05 + texel * 3.0 * diffuse + texel * 3.0 * specular;
}
