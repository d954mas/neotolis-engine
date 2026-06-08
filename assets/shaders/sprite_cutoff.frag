precision mediump float;

uniform sampler2D u_texture;
uniform vec4 u_alpha_cutoff; // .x = alpha discard threshold (set per material; 0 disables)

in vec2 v_texcoord;
in vec4 v_color;

out vec4 frag_color;

/* sprite.frag + an alpha discard so near-transparent fragments don't write depth. Needed when a
 * sprite material has depth_write ON (e.g. overlapping world-space UI panels): without it a button's
 * transparent corners punch holes into the depth buffer. cutoff 0 = no discard (== sprite.frag). */
void main() {
    // Premultiplied alpha, same as sprite.frag (texture is premultiplied; premultiply vertex color).
    vec4 tex = texture(u_texture, v_texcoord);
    vec4 c = vec4(v_color.rgb * v_color.a, v_color.a);
    vec4 px = tex * c;
    if (px.a < u_alpha_cutoff.x) {
        discard;
    }
    frag_color = px;
}
