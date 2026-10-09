/* Copyright 2019-2021 Evgeny Gorodetskiy
 * Copyright 2026 Neotolis Contributors
 * Licensed under the Apache License, Version 2.0.
 * https://www.apache.org/licenses/LICENSE-2.0
 * Distributed AS IS, without warranties or conditions of any kind.
 * Modified: Methane Asteroids PlanetPS translated to GLSL. The Mars texture
 * uses sRGB storage so decoding precedes filtering, as in the source. */
precision highp float;
in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
uniform sampler2D u_diffuse;
uniform vec4 u_light_position;
out vec4 frag_color;
#include "../../../assets/shaders/common/globals.glsl"
#include "primitives.glsl"
void main() {
    vec3 linear_texel = texture(u_diffuse, v_uv, -1.0).rgb;
    frag_color = vec4(color_linear_to_srgb(phong_color(linear_texel, v_world, v_normal, camera_pos.xyz, u_light_position.xyz)), 1.0);
}
