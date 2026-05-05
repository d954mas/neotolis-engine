precision mediump float;

// Sprite fragment shader (Phase 50 D-21 — game-shipped).
// Atlas page sample × per-vertex color (drawable_comp tint).
// Builder pre-multiplies PNG alpha (atlas opts.premultiplied = true);
// material uses NT_BLEND_MODE_ALPHA which the renderer maps to
// (ONE, ONE_MINUS_SRC_ALPHA) — matches the premultiplied recipe (D-24).

uniform sampler2D u_texture;

in vec2 v_texcoord;
in vec4 v_color;

out vec4 frag_color;

void main() {
    vec4 tex = texture(u_texture, v_texcoord);
    frag_color = tex * v_color;
}
