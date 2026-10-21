precision highp float;

#include "nt/locations.glsl"
layout(location = NT_LOC_POSITION) in vec3 a_position;
layout(location = NT_LOC_NORMAL) in vec3 a_normal;
layout(location = NT_LOC_TANGENT) in vec4 a_tangent;
layout(location = NT_INST_LOC_WORLD_ROW0) in vec4 a_world_row0;
layout(location = NT_INST_LOC_WORLD_ROW1) in vec4 a_world_row1;
layout(location = NT_INST_LOC_WORLD_ROW2) in vec4 a_world_row2;
layout(location = NT_INST_LOC_COLOR) in vec4 a_color;

out vec3 v_position_probe;
out vec3 v_normal_probe;
out vec3 v_tangent_probe;
out vec4 v_color_probe;
out vec4 v_param_probe;

vec3 world_position(vec3 position) {
    vec4 p = vec4(position, 1.0);
    return vec3(dot(a_world_row0, p), dot(a_world_row1, p), dot(a_world_row2, p));
}

void main() {
    vec3 position = world_position(a_position);
    v_position_probe = position * 0.25 + 0.5;
    v_normal_probe = a_normal * 0.25 + 0.5;
    v_tangent_probe = a_tangent.xyz * 0.25 + 0.5;
    v_color_probe = a_color;
    v_param_probe = vec4(0.0);
    gl_Position = vec4(position, 1.0);
}
