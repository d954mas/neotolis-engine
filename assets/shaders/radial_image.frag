precision highp float;

// Textured radial reveal (nt_ui_radial_image, D-66-12). Pairs with
// sprite_radial.vert. Samples a real atlas region, then reveals the UN-SWEPT
// (remaining) sector via one of four modes while the SWEPT sector stays full
// color. Mathematical angle convention: 0 = +X, CCW positive (D-66-10).
//
// Wedge local coord is derived from the region UV remapped to [-1,1]
// (v_local_uv = v_texcoord*2-1) — no extra per-vertex attr on the image path
// (RESEARCH §SDF Math). NOTE: this assumes the region UV spans [0,1] over the
// quad; a sub-region atlas tile re-centers the wedge — flagged for the plan-05
// visual-QA gate.

uniform sampler2D u_texture;
// .x = reveal mode (0 DESATURATE / 1 DIM / 2 HIDE / 3 TINT), .y = dim_factor.
uniform vec4 u_reveal_mode;
// .rgb = TINT target color (0..1), .a = TINT mix strength (0..1).
uniform vec4 u_reveal_tint;

in vec2 v_texcoord;
in vec4 v_color;
in vec4 v_radial;
in vec2 v_local;

out vec4 frag_color;

const float TAU = 6.28318530717958647692;

void main() {
    float angle_start = v_radial.x;
    float angle_end = v_radial.y;
    float inner = v_radial.z;
    float aspect = v_radial.w;

    // Region UV → [-1,1] local for the angular/ring test (image path reuses the
    // UV rather than gl_VertexID). aspect = w/h re-rounds the angle on a
    // non-square bbox so 0 stays +X.
    vec2 v_local_uv = (v_texcoord * 2.0) - 1.0;
    vec2 p = v_local_uv * vec2(1.0, aspect);
    float r = length(p);

    // Ring band: optional inner cut (0 = solid disc). Outer edge is implicit in
    // the quad — a full textured tile keeps its corners (no SDF outer mask).
    float ring = (inner > 0.0) ? step(inner, r) : 1.0;

    // Wrap-aware angular wedge (Pitfall 4): mod the CCW sweep start→fragment and
    // start→end into [0,TAU); inside when the fragment's sweep <= the total.
    float ang = atan(v_local_uv.y, v_local_uv.x);
    float sweep = mod(ang - angle_start, TAU);
    float total = mod(angle_end - angle_start, TAU);
    bool full_turn = (angle_end - angle_start) >= TAU - 1e-4;
    bool in_wedge = full_turn || (sweep <= total);

    // Premultiply (Pitfall 5): texture is premultiplied; premultiply vertex color
    // too. Identical to sprite.frag.
    vec4 tex = texture(u_texture, v_texcoord);
    vec4 c = vec4(v_color.rgb * v_color.a, v_color.a);
    vec4 lit = tex * c;

    // Swept sector (and the ring cut-out region) renders at full color; the
    // un-swept remainder gets the reveal composite.
    vec4 reveal = lit;
    int mode = int(u_reveal_mode.x + 0.5);
    if (mode == 0) {
        // DESATURATE: mix to luma, preserve (premultiplied) alpha.
        float luma = dot(lit.rgb, vec3(0.299, 0.587, 0.114));
        reveal = vec4(vec3(luma), lit.a);
    } else if (mode == 1) {
        // DIM: scale brightness (RGB only; premultiplied so RGB carries alpha).
        reveal = vec4(lit.rgb * u_reveal_mode.y, lit.a);
    } else if (mode == 2) {
        // HIDE: fully hidden (alpha 0 → discarded below).
        reveal = vec4(0.0);
    } else {
        // TINT: mix RGB toward the tint color (premultiplied by alpha), keep alpha.
        vec3 tinted = mix(lit.rgb, u_reveal_tint.rgb * lit.a, u_reveal_tint.a);
        reveal = vec4(tinted, lit.a);
    }

    vec4 outc = in_wedge ? lit : reveal;
    // Apply the ring cut: inner hole is fully transparent (0 inner = full tile).
    outc *= ring;

    if (outc.a <= 0.0) {
        // HIDE / fully-transparent: discard so depth-write materials don't punch
        // holes (mirror sprite_cutoff.frag).
        discard;
    }
    frag_color = outc;
}
