precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 4) in vec4 a_world_row0;
layout(location = 5) in vec4 a_world_row1;
layout(location = 6) in vec4 a_world_row2;
layout(location = 7) in vec4 a_color;

out vec3 v_position_probe;
out vec3 v_normal_probe;
out vec3 v_tangent_probe;

vec3 world_position(vec3 position) {
    vec4 p = vec4(position, 1.0);
    return vec3(dot(a_world_row0, p), dot(a_world_row1, p), dot(a_world_row2, p));
}

void main() {
    vec3 position = world_position(a_position);
    v_position_probe = position * 0.25 + 0.5;
    v_normal_probe = a_normal * 0.25 + 0.5;
    v_tangent_probe = a_tangent.xyz * 0.25 + 0.5;
    gl_Position = vec4(position, 1.0);
}
