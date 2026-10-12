precision highp float;

#include "nt/locations.glsl"
layout(location = NT_LOC_POSITION) in vec3 a_position;
layout(location = NT_LOC_UV0) in vec2 a_uv;

out vec2 v_uv;

uniform mat4 u_mvp;

void main() {
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
