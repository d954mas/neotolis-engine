precision mediump float;

// Inline rich-text image (nt_ui_rich_text <img>). Pairs with sprite_radial.vert
// (reused for its a_tint @ loc 5 custom attr; a_radial/a_uvrect/a_layout stay
// unused here). Samples a real atlas region and multiplies by the per-vertex
// a_tint float4 (the run's lossless <color>, straight alpha) — so a tinted inline
// icon keeps full precision instead of the u8 vertex-color path.

uniform sampler2D u_texture;

in vec2 v_texcoord;
in vec4 v_tint;

out vec4 frag_color;

void main() {
    // Texture is premultiplied; premultiply the straight-alpha tint to match
    // (ONE, ONE_MINUS_SRC_ALPHA), identical to sprite.frag.
    vec4 tex = texture(u_texture, v_texcoord);
    vec4 c = vec4(v_tint.rgb * v_tint.a, v_tint.a);
    frag_color = tex * c;
}
