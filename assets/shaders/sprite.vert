precision highp float;
precision highp int;

#include "common/globals.glsl"

// Locations match nt_attr_location_t in engine/graphics/nt_gfx.h.
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;

out vec2 v_texcoord;
out vec4 v_color;

void main() {
    gl_Position = view_proj * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
}
