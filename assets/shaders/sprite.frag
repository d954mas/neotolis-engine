precision mediump float;

uniform sampler2D u_texture;

in vec2 v_texcoord;
in vec4 v_color;

out vec4 frag_color;

void main() {
    /* Texture comes premultiplied (RGB already × A at builder time). The
     * sprite pipeline uses (ONE, ONE_MINUS_SRC_ALPHA), which expects src.RGB
     * pre-scaled by src.A. Vertex color carries straight alpha (e.g. from
     * nt_drawable_comp_set_alpha), so premultiply it here too — otherwise a
     * faded sprite leaves RGB at full intensity while alpha drops, producing
     * bright halos under blending. */
    vec4 tex = texture(u_texture, v_texcoord);
    vec4 c = vec4(v_color.rgb * v_color.a, v_color.a);
    frag_color = tex * c;
}
