precision highp float;

// Flat SDF arc/sector/ring/oval on a plain quad. No texture sample — the shape is
// per-pixel. v_radial = {angle_start, angle_end, inner_radius_norm, 0}; aspect comes
// from v_layout.x (walker-injected bbox w/h). v_local is the [-1,1] quad-local coord.
// Mathematical convention: 0 = +X, CCW positive. Crisp AA via fwidth-derived 1px
// pixel coverage, NO facets.

in vec2 v_texcoord;
in vec4 v_color;
in vec4 v_radial;
in vec4 v_layout;
in vec2 v_local;

out vec4 frag_color;

const float TAU = 6.28318530717958647692;

void main() {
    float angle_start = v_radial.x;
    float angle_end = v_radial.y;
    float inner = v_radial.z;
    float aspect = v_layout.x;

    // Oval squash: aspect = w/h re-rounds the test so 0 stays +X on a non-square
    // bbox. r in [0,1] across the disc.
    vec2 p = v_local * vec2(1.0, aspect);
    float r = length(p);

    // Edge coverage in PIXELS (mirrors slug_text.frag pixelsPerEm): a 1px linear
    // box-filter centred on each edge, so AA stays crisp at any radius and never
    // bleeds outward into a halo. ppu = pixels per r-unit.
    float ppu = 1.0 / max(fwidth(r), 1e-6);
    float outer = clamp((1.0 - r) * ppu + 0.5, 0.0, 1.0);
    float ring = (inner > 0.0) ? clamp((r - inner) * ppu + 0.5, 0.0, 1.0) : 1.0;
    float radial_mask = outer * ring;

    // Wrap-aware angular wedge: lead/trail gate both sides so a wedge crossing 0 is
    // admitted once. Arc-perpendicular distance r*angle, in pixels via ppu, feeds the
    // same 1px box-filter so angular edges match the radial crispness.
    float ang = atan(v_local.y, v_local.x);
    float sweep = mod(ang - angle_start, TAU);
    float total = mod(angle_end - angle_start, TAU);
    bool full_turn = (angle_end - angle_start) >= TAU - 1e-4;
    float lead = clamp(r * sweep * ppu + 0.5, 0.0, 1.0);
    float trail = clamp(r * (total - sweep) * ppu + 0.5, 0.0, 1.0);
    float wedge = full_turn ? 1.0 : (lead * trail);

    float a = radial_mask * wedge * v_color.a;
    if (a <= 0.0) {
        discard;
    }
    // Premultiply: the sprite pipeline blends (ONE, ONE_MINUS_SRC_ALPHA), which
    // expects src.rgb pre-scaled by src.a.
    frag_color = vec4(v_color.rgb * a, a);
}
