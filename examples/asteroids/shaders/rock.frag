/* Copyright 2019-2021 Evgeny Gorodetskiy
 * Copyright 2026 Neotolis Contributors
 * Licensed under the Apache License, Version 2.0.
 * https://www.apache.org/licenses/LICENSE-2.0
 * Distributed AS IS, without warranties or conditions of any kind.
 * Modified: Methane Asteroids AsteroidPS; the source's three identical
 * Texture2DArray layers become one R8 noise texture sampled per axis. */
precision highp float;
in vec3 v_world;
in vec3 v_normal;
in vec3 v_albedo;
in vec3 v_uvw;
in vec3 v_weights;
uniform sampler2D u_noise;
uniform vec4 u_light_position;
out vec4 frag_color;
#include "../../../assets/shaders/common/globals.glsl"
#include "primitives.glsl"
/* DirectX/Vulkan map the source's default Mip::NotMipmapped to nearest mip.
 * A separable edge weight gives black-border bilinear filtering using the
 * backend's clamp-to-edge sample, including partial edge-texel footprints. */
float sample_noise(vec2 uv) {
    vec2 dx = dFdx(uv) * 256.0;
    vec2 dy = dFdy(uv) * 256.0;
    float lod = clamp(floor(0.5 * log2(max(1.0, max(dot(dx, dx), dot(dy, dy)))) + 0.5), 0.0, 8.0);
    vec2 dimensions = vec2(textureSize(u_noise, int(lod)));
    vec2 footprint = uv * dimensions;
    vec2 coverage = clamp(footprint + 0.5, 0.0, 1.0) * clamp(dimensions + 0.5 - footprint, 0.0, 1.0);
    return textureLod(u_noise, uv, lod).r * coverage.x * coverage.y;
}
void main() {
    float texel = v_weights.x * sample_noise(v_uvw.yz);
    texel += v_weights.y * sample_noise(v_uvw.zx);
    texel += v_weights.z * sample_noise(v_uvw.xy);
    vec3 linear_color = phong_color(texel * v_albedo, v_world, v_normal, camera_pos.xyz, u_light_position.xyz);
    float fading = clamp(gl_FragCoord.z * 8000.0, 0.0, 1.0);
    frag_color = vec4(color_linear_to_srgb(linear_color * fading), 1.0);
}
