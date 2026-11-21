precision highp float;
precision highp int;

#include "common/globals.glsl"
#include "common/slug_vertex.glsl"

void main() {
    SlugVertex();
    gl_Position = view_proj * vec4(a_position, 1.0);
}
