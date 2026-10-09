/* Copyright 2019-2020 Evgeny Gorodetskiy
 * Copyright 2026 Neotolis Contributors
 * Licensed under the Apache License, Version 2.0.
 * https://www.apache.org/licenses/LICENSE-2.0
 * Distributed AS IS, without warranties or conditions of any kind.
 * Modified: MethaneKit SkyboxVS translated to GLSL; reversed zero depth maps
 * to -w in OpenGL. Direction vectors deliberately exclude camera translation. */
precision highp float;
layout(location = 0) in vec3 a_position;
#include "../../../assets/shaders/common/globals.glsl"
out vec3 v_uvw;
void main() {
    gl_Position = view_proj * vec4(a_position * 1500.0, 0.0);
    gl_Position.z = -gl_Position.w;
    v_uvw = a_position;
}
