precision highp float;

// Flat SDF arc/sector/ring/oval on a plain quad (D-66-08/11). No texture sample
// — the shape is per-pixel. v_radial = {angle_start, angle_end, inner_radius_norm,
// aspect}; v_local is the [-1,1] quad-local coord. Mathematical convention:
// 0 = +X, CCW positive (D-66-10). Crisp AA via fwidth/smoothstep, NO facets.

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

    // Oval squash: aspect = w/h re-rounds the test so 0 stays +X on a non-square
    // bbox. r in [0,1] across the disc.
    vec2 p = v_local * vec2(1.0, aspect);
    float r = length(p);

    // Radial ring band: smooth outer edge at r=1 and inner edge at r=inner.
    float r_aa = fwidth(r);
    float outer = 1.0 - smoothstep(1.0 - r_aa, 1.0 + r_aa, r);
    float ring = (inner > 0.0) ? smoothstep(inner - r_aa, inner + r_aa, r) : 1.0;
    float radial_mask = outer * ring;

    // Wrap-aware angular wedge (Pitfall 4): mod the CCW sweep start→fragment and
    // start→end into [0,TAU); the fragment is inside when its sweep <= the total.
    float ang = atan(v_local.y, v_local.x);
    float sweep = mod(ang - angle_start, TAU);
    float total = mod(angle_end - angle_start, TAU);
    // A full turn (end == start + TAU) collapses to total==0 under mod — treat it
    // as the whole disc.
    bool full_turn = (angle_end - angle_start) >= TAU - 1e-4;
    // Angular AA scaled by r so the soft edge is a uniform arc-length width along
    // the radius (D-66-11 — no facets). Guard r→0 at the center.
    float ang_aa = fwidth(sweep) + 1e-4;
    float lead = smoothstep(-ang_aa, ang_aa, sweep);
    float trail = smoothstep(-ang_aa, ang_aa, total - sweep);
    float wedge = full_turn ? 1.0 : (lead * trail);

    float a = radial_mask * wedge * v_color.a;
    if (a <= 0.0) {
        discard;
    }
    // Premultiply (Pitfall 5): the sprite pipeline blends (ONE, ONE_MINUS_SRC_ALPHA),
    // which expects src.rgb pre-scaled by src.a.
    frag_color = vec4(v_color.rgb * a, a);
}
