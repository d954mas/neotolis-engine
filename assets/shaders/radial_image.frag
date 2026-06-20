precision highp float;

// Textured radial reveal (nt_ui_radial_image). Pairs with sprite_radial.vert.
// Samples a real atlas region, then reveals the UN-SWEPT (remaining) sector via
// one of four modes while the SWEPT sector stays full color. Mathematical angle
// convention: 0 = +X, CCW positive.
//
// Wedge local coord is region-local [-1,1]: v_texcoord is normalized against the
// region's atlas UV rect (v_uvrect = {u0,v0,u1,v1}) so the wedge centers on ANY
// rectangular atlas region, packed sub-region included — no full-bleed requirement.
//
// TINT is per-vertex (a_tint via v_tint): rgb=target color, w=mix strength. Many
// tint colors share ONE material. mode + dim_factor stay material-level (u_reveal_mode).

uniform sampler2D u_texture;
// .x = reveal mode (0 DESATURATE / 1 DIM / 2 HIDE / 3 TINT), .y = dim_factor.
uniform vec4 u_reveal_mode;

in vec2 v_texcoord;
in vec4 v_color;
in vec4 v_radial;
in vec4 v_tint;
in vec4 v_uvrect;
in vec2 v_local;

out vec4 frag_color;

const float TAU = 6.28318530717958647692;

void main() {
    float angle_start = v_radial.x;
    float angle_end = v_radial.y;
    float inner = v_radial.z;
    float aspect = v_radial.w;

    // Region-local UV → [-1,1] for the angular/ring test (image path reuses the
    // UV rather than gl_VertexID). v_texcoord is normalized against the region's
    // atlas UV rect so a packed sub-region centers the wedge. aspect = w/h re-rounds
    // the angle on a non-square bbox so 0 stays +X.
    vec2 v_local_uv = (v_texcoord - v_uvrect.xy) / max(v_uvrect.zw - v_uvrect.xy, vec2(1e-6)) * 2.0 - 1.0;
    vec2 p = v_local_uv * vec2(1.0, aspect);
    float r = length(p);

    // Pixel-space coverage (slug-style, like radial.frag): a 1px box-filter so the
    // ring and reveal edges stay crisp at any size. ppu = pixels per local unit.
    // Outer edge is the textured tile quad itself (no SDF outer mask).
    float ppu = 1.0 / max(fwidth(r), 1e-6);
    float ring = (inner > 0.0) ? clamp((r - inner) * ppu + 0.5, 0.0, 1.0) : 1.0;

    // Wrap-aware angular wedge: lead/trail gate both sides so a wedge crossing 0 is
    // admitted once; arc-perpendicular distance r*angle in pixels feeds the same
    // box-filter, so the swept/un-swept boundary is anti-aliased, not a hard step.
    float ang = atan(v_local_uv.y, v_local_uv.x);
    float sweep = mod(ang - angle_start, TAU);
    float total = mod(angle_end - angle_start, TAU);
    bool full_turn = (angle_end - angle_start) >= TAU - 1e-4;
    float lead = clamp(r * sweep * ppu + 0.5, 0.0, 1.0);
    float trail = clamp(r * (total - sweep) * ppu + 0.5, 0.0, 1.0);
    float wedge_cov = full_turn ? 1.0 : (lead * trail);

    // Premultiply: texture is premultiplied; premultiply vertex color too.
    // Identical to sprite.frag.
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
        // TINT: mix RGB toward the per-vertex tint color (premultiplied by alpha), keep alpha.
        vec3 tinted = mix(lit.rgb, v_tint.rgb * lit.a, v_tint.a);
        reveal = vec4(tinted, lit.a);
    }

    vec4 outc = mix(reveal, lit, wedge_cov);
    // Apply the ring cut: inner hole fades out over 1px (0 inner = full tile).
    outc *= ring;

    if (outc.a <= 0.0) {
        // HIDE / fully-transparent: discard so depth-write materials don't punch
        // holes (mirror sprite_cutoff.frag).
        discard;
    }
    frag_color = outc;
}
