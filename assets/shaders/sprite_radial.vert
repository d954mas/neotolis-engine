precision highp float;
precision highp int;

// UBO contract: same as sprite.vert — register "Globals" (common/globals.glsl)
// at slot 0 and update + bind the frame UBO every frame before draw_list.
#include "common/globals.glsl"

// Base locations match nt_attr_location_t in engine/graphics/nt_gfx.h.
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;
// Custom per-vertex attr (material attr_map a_radial @ loc 4): x=angle_start
// y=angle_end z=inner_radius_norm w=aspect. Uniform across the widget's verts.
layout(location = 4) in vec4 a_radial;

out vec2 v_texcoord;
out vec4 v_color;
out vec4 v_radial;
out vec2 v_local;

void main() {
    gl_Position = view_proj * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
    v_radial = a_radial;
    // The widget emits a 4-corner quad TL/TR/BR/BL; derive the [-1,1] local coord
    // from gl_VertexID so no extra per-vertex attribute is needed (the 16 B custom
    // block is fully spent on a_radial). The 16-bit quad indices cycle 0..3 per
    // widget, so gl_VertexID & 3 is the corner within each batched quad.
    int corner = gl_VertexID & 3;
    // 0:TL(-1,-1) 1:TR(+1,-1) 2:BR(+1,+1) 3:BL(-1,+1)
    float lx = (corner == 1 || corner == 2) ? 1.0 : -1.0;
    float ly = (corner == 2 || corner == 3) ? 1.0 : -1.0;
    v_local = vec2(lx, ly);
}
