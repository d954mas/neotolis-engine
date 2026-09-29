precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;

#include "../../assets/shaders/common/instance.glsl"

out vec3 v_position_probe;
out vec3 v_normal_probe;
out vec3 v_tangent_probe;
out vec4 v_color_probe;

vec3 world_position(mat4 world, vec3 position) {
    return (world * vec4(position, 1.0)).xyz;
}

void main() {
    nt_instance_t inst = nt_instance();
    vec3 position = world_position(inst.world, a_position);
    v_position_probe = position * 0.25 + 0.5;
    v_normal_probe = a_normal * 0.25 + 0.5;
    v_tangent_probe = a_tangent.xyz * 0.25 + 0.5;
    v_color_probe = inst.color;
    gl_Position = vec4(position, 1.0);
}
