#pragma once

/* Linear light to sRGB, clamped: the one output conversion of a shader that lights
 * in linear space. sRGB textures already sample linear. */
vec3 nt_linear_to_srgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
