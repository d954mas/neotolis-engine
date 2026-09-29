precision highp float;

/* Mesh vertex attributes */
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;

#include "common/instance.glsl"
#include "common/globals.glsl"

out vec2 v_uv;
out vec4 v_color;

void main() {
    nt_instance_t inst = nt_instance();
    mat4 world = inst.world;
    v_uv = a_uv;
    v_color = inst.color;
    gl_Position = view_proj * world * vec4(a_position, 1.0);
}
