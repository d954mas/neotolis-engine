precision mediump float;

/* Mesh vertex attributes */
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;

/* Instance attributes (divisor=1, set by nt_mesh_renderer) */
layout(location = 4) in vec4 a_world_col0;
layout(location = 5) in vec4 a_world_col1;
layout(location = 6) in vec4 a_world_col2;
layout(location = 7) in vec4 a_world_col3;
layout(location = 8) in vec4 a_color;

/* Frame uniforms UBO (slot 0, game uploads before draw_list) */
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
out vec4 v_color;

void main() {
    mat4 world = mat4(a_world_col0, a_world_col1, a_world_col2, a_world_col3);
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = view_proj * world * vec4(a_position, 1.0);
}
