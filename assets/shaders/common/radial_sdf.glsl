// Flat SDF arc/sector/ring/oval coverage on a plain quad. radial = {angle_start, angle_end,
// inner_radius_norm, 0}; aspect = bbox w/h; local = the [-1,1] quad-local coord.
// Mathematical convention: 0 = +X, CCW positive. Crisp AA via fwidth-derived 1px
// pixel coverage, NO facets.

const float TAU = 6.28318530717958647692;

float radial_coverage(vec4 radial, float aspect, vec2 local) {
    float angle_start = radial.x;
    float angle_end = radial.y;
    float inner = radial.z;

    // Oval squash: aspect = w/h re-rounds the test so 0 stays +X on a non-square
    // bbox. r in [0,1] across the disc.
    vec2 p = local * vec2(1.0, aspect);
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
    float ang = atan(p.y, p.x); // aspect-corrected space (matches r) so the wedge aligns with the oval
    float sweep = mod(ang - angle_start, TAU);
    float total = mod(angle_end - angle_start, TAU);
    bool full_turn = abs(angle_end - angle_start) >= TAU - 1e-4; // |sweep|: a reverse (CCW) full turn is full too
    float lead = clamp(r * sweep * ppu + 0.5, 0.0, 1.0);
    float trail = clamp(r * (total - sweep) * ppu + 0.5, 0.0, 1.0);
    float wedge = full_turn ? 1.0 : (lead * trail);

    return radial_mask * wedge;
}
