/* Copyright 2019-2020 Evgeny Gorodetskiy
 * Copyright 2026 Neotolis Contributors
 * Licensed under the Apache License, Version 2.0.
 * https://www.apache.org/licenses/LICENSE-2.0
 * Distributed AS IS, without warranties or conditions of any kind.
 * Modified: MethaneKit SkyboxPS, six 2D textures substitute for TextureCube.
 * Faces keep the source +X,-X,+Y,-Y,+Z,-Z orientation and full resolution.
 * Cross-face seamless cube filtering is unavailable in this representation. */
precision highp float;
in vec3 v_uvw;
uniform sampler2D u_sky_px;
uniform sampler2D u_sky_nx;
uniform sampler2D u_sky_py;
uniform sampler2D u_sky_ny;
uniform sampler2D u_sky_pz;
uniform sampler2D u_sky_nz;
out vec4 frag_color;
void main() {
    vec3 d = v_uvw;
    vec3 a = abs(d);
    vec3 color;
    if (a.x >= a.y && a.x >= a.z) {
        if (d.x > 0.0) color = texture(u_sky_px, vec2(-d.z, -d.y) / a.x * 0.5 + 0.5).rgb;
        else color = texture(u_sky_nx, vec2(d.z, -d.y) / a.x * 0.5 + 0.5).rgb;
    } else if (a.y >= a.z) {
        if (d.y > 0.0) color = texture(u_sky_py, vec2(d.x, d.z) / a.y * 0.5 + 0.5).rgb;
        else color = texture(u_sky_ny, vec2(d.x, -d.z) / a.y * 0.5 + 0.5).rgb;
    } else {
        if (d.z > 0.0) color = texture(u_sky_pz, vec2(d.x, -d.y) / a.z * 0.5 + 0.5).rgb;
        else color = texture(u_sky_nz, vec2(-d.x, -d.y) / a.z * 0.5 + 0.5).rgb;
    }
    frag_color = vec4(color, 1.0);
}
