precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_tangent;

layout(location = 4) in vec4 a_world_col0;
layout(location = 5) in vec4 a_world_col1;
layout(location = 6) in vec4 a_world_col2;
layout(location = 7) in vec4 a_world_col3;
layout(location = 8) in vec4 a_color;

layout(std140) uniform Globals {
    mat4 view_proj;
    mat4 view;
    mat4 proj;
    vec4 camera_pos;
    vec4 time;
    vec4 resolution;
    vec4 near_far;
};

out vec2 v_uv;
out vec3 v_world_pos;
out mat3 v_tbn;
out vec4 v_color;

void main() {
    mat4 world = mat4(a_world_col0, a_world_col1, a_world_col2, a_world_col3);
    mat3 world3 = mat3(world);
    vec3 N = normalize(world3 * a_normal);
    vec3 T = normalize(world3 * a_tangent.xyz);
    vec3 B = cross(N, T) * a_tangent.w;
    v_tbn = mat3(T, B, N);
    v_uv = a_uv;
    v_world_pos = (world * vec4(a_position, 1.0)).xyz;
    v_color = a_color;
    gl_Position = view_proj * world * vec4(a_position, 1.0);
}
