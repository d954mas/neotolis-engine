precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

#include "common/instance.glsl"
#include "common/globals.glsl"

out vec2 v_uv;
out vec3 v_world_normal;
out vec3 v_world_pos;
out vec4 v_color;

void main() {
    mat4 world = mat4(
        vec4(a_world_row0.x, a_world_row1.x, a_world_row2.x, 0.0),
        vec4(a_world_row0.y, a_world_row1.y, a_world_row2.y, 0.0),
        vec4(a_world_row0.z, a_world_row1.z, a_world_row2.z, 0.0),
        vec4(a_world_row0.w, a_world_row1.w, a_world_row2.w, 1.0)
    );
    v_world_normal = normalize(mat3(world) * a_normal);
    v_uv = a_uv;
    v_world_pos = (world * vec4(a_position, 1.0)).xyz;
    v_color = a_color;
    gl_Position = view_proj * world * vec4(a_position, 1.0);
}
