precision highp float;
precision highp int;

// Slug GPU vector text fragment shader
// Ported from HLSL reference (github.com/EricLengyel/Slug, MIT license)
// Uses CalcRootCode, reference solvers, and CalcCoverage formula verbatim.

uniform sampler2D u_curve_texture;       // RGBA16F -- curve control points as float16
uniform highp usampler2D u_band_texture; // RG16UI -- (curve_start, curve_count) per band
uniform int u_curve_tex_width;           // For linear-to-2D addressing
uniform vec4 u_alpha_cutoff;             // .x = coverage discard threshold (set per material; 0 disables)

in vec2 v_texcoord;
flat in uvec4 v_glyph;       // curve_offset_y, band_row, curve_offset_x, band_count
flat in vec4 v_glyph_bounds; // bbox (x0, y0, x1, y1) in em-space
in vec4 v_color;

out vec4 frag_color;

// Linear fallback for truly degenerate (a.y == 0) curves; near-tangential
// cases are handled by the Citardauq stable form in the solvers below.
#ifndef SLUG_LINEAR_FALLBACK_EPSILON
#define SLUG_LINEAR_FALLBACK_EPSILON (1.0 / 65536.0)
#endif

ivec2 CurveTexCoord(uint offset) { return ivec2(int(offset) % u_curve_tex_width, int(offset) / u_curve_tex_width); }

// Determine root eligibility from signs of control point coordinates.
// Returns eligibility in bits 0 (root 1) and 8 (root 2).
// Reference: SlugPixelShader.hlsl CalcRootCode()
uint CalcRootCode(float y1, float y2, float y3) {
    uint i1 = floatBitsToUint(y1) >> 31u;
    uint i2 = floatBitsToUint(y2) >> 30u;
    uint i3 = floatBitsToUint(y3) >> 29u;
    uint shift = (i2 & 2u) | (i1 & ~2u);
    shift = (i3 & 4u) | (shift & ~4u);
    return ((0x2E74u >> shift) & 0x0101u);
}

// Solve for x where curve crosses y=0. Reference SolveHorizPoly() with the
// Citardauq stable form: q = b + sign(b)*sqrt(D) is cancellation-free;
// the other root via Vieta (t1*t2 = p0/a). Avoids the precision loss of
// `b - sqrt(D)` on near-tangential curves.
vec2 SolveHorizPoly(vec2 p0, vec2 p1, vec2 p2) {
    vec2 a = p0 - p1 * 2.0 + p2;
    vec2 b = p0 - p1;
    float d = sqrt(max(b.y * b.y - a.y * p0.y, 0.0));
    float q = b.y + (b.y >= 0.0 ? d : -d);
    float t_add = q / a.y;
    float t_mul = (abs(q) > 1e-6) ? (p0.y / q) : t_add;
    // Map back to reference t1=(b-d)/a, t2=(b+d)/a ordering by sign(b.y).
    float t1 = (b.y >= 0.0) ? t_mul : t_add;
    float t2 = (b.y >= 0.0) ? t_add : t_mul;

    if (abs(a.y) < SLUG_LINEAR_FALLBACK_EPSILON) {
        t1 = p0.y * (0.5 / b.y);
        t2 = t1;
    }

    return vec2((a.x * t1 - b.x * 2.0) * t1 + p0.x, (a.x * t2 - b.x * 2.0) * t2 + p0.x);
}

// Solve for y where curve crosses x=0. Same Citardauq stabilization.
vec2 SolveVertPoly(vec2 p0, vec2 p1, vec2 p2) {
    vec2 a = p0 - p1 * 2.0 + p2;
    vec2 b = p0 - p1;
    float d = sqrt(max(b.x * b.x - a.x * p0.x, 0.0));
    float q = b.x + (b.x >= 0.0 ? d : -d);
    float t_add = q / a.x;
    float t_mul = (abs(q) > 1e-6) ? (p0.x / q) : t_add;
    float t1 = (b.x >= 0.0) ? t_mul : t_add;
    float t2 = (b.x >= 0.0) ? t_add : t_mul;

    if (abs(a.x) < SLUG_LINEAR_FALLBACK_EPSILON) {
        t1 = p0.x * (0.5 / b.x);
        t2 = t1;
    }

    return vec2((a.y * t1 - b.y * 2.0) * t1 + p0.y, (a.y * t2 - b.y * 2.0) * t2 + p0.y);
}

// Reference: SlugPixelShader.hlsl CalcCoverage()
float CalcCoverage(float xcov, float ycov, float xwgt, float ywgt) {
    float coverage = max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0), min(abs(xcov), abs(ycov)));
    return clamp(coverage, 0.0, 1.0);
}

// Main coverage: Y-band horizontal ray + X-band vertical ray
float SlugRender(vec2 coord) {
    uint curve_offset_y = v_glyph.x;
    uint band_row = v_glyph.y;
    uint curve_offset_x = v_glyph.z;
    uint band_count = v_glyph.w;

    vec2 pixelsPerEm = 1.0 / max(fwidth(coord), vec2(1.0e-6));
    float bbox_height = v_glyph_bounds.w - v_glyph_bounds.y;
    float bbox_width = v_glyph_bounds.z - v_glyph_bounds.x;

    // ---- Y-band: horizontal ray (+X) ----
    float band_y = (coord.y - v_glyph_bounds.y) / bbox_height * float(band_count);
    int yband_idx = clamp(int(band_y), 0, int(band_count) - 1);
    uvec4 yband = texelFetch(u_band_texture, ivec2(yband_idx, int(band_row)), 0);

    float xcov = 0.0;
    float xwgt = 0.0;
    uint ycurveBase = curve_offset_y + yband.r * 2u;

    for (uint i = 0u; i < yband.g; i++) {
        uint ti = ycurveBase + i * 2u;
        vec4 d0 = texelFetch(u_curve_texture, CurveTexCoord(ti), 0);
        vec4 d1 = texelFetch(u_curve_texture, CurveTexCoord(ti + 1u), 0);
        vec2 p0 = d0.xy - coord;
        vec2 p1 = d0.zw - coord;
        vec2 p2 = d1.xy - coord;

        uint code = CalcRootCode(p0.y, p1.y, p2.y);
        if (code != 0u) {
            vec2 r = SolveHorizPoly(p0, p1, p2) * pixelsPerEm.x;
            if ((code & 1u) != 0u) {
                xcov += clamp(r.x + 0.5, 0.0, 1.0);
                xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
            }
            if (code > 1u) {
                xcov -= clamp(r.y + 0.5, 0.0, 1.0);
                xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
            }
        }
    }

    // ---- X-band: vertical ray (+Y) ----
    float band_x = (coord.x - v_glyph_bounds.x) / bbox_width * float(band_count);
    int xband_idx = clamp(int(band_x), 0, int(band_count) - 1);
    uvec4 xband = texelFetch(u_band_texture, ivec2(int(band_count) + xband_idx, int(band_row)), 0);

    float ycov = 0.0;
    float ywgt = 0.0;
    uint xcurveBase = curve_offset_x + xband.r * 2u;

    for (uint i = 0u; i < xband.g; i++) {
        uint ti = xcurveBase + i * 2u;
        vec4 d0 = texelFetch(u_curve_texture, CurveTexCoord(ti), 0);
        vec4 d1 = texelFetch(u_curve_texture, CurveTexCoord(ti + 1u), 0);
        vec2 p0 = d0.xy - coord;
        vec2 p1 = d0.zw - coord;
        vec2 p2 = d1.xy - coord;

        uint code = CalcRootCode(p0.x, p1.x, p2.x);
        if (code != 0u) {
            vec2 r = SolveVertPoly(p0, p1, p2) * pixelsPerEm.y;
            if ((code & 1u) != 0u) {
                ycov -= clamp(r.x + 0.5, 0.0, 1.0);
                ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
            }
            if (code > 1u) {
                ycov += clamp(r.y + 0.5, 0.0, 1.0);
                ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
            }
        }
    }

    return CalcCoverage(xcov, ycov, xwgt, ywgt);
}

void main() {
    float coverage = SlugRender(v_texcoord);

    if (coverage < u_alpha_cutoff.x)
        discard;

    // Premultiplied alpha output
    float alpha = coverage * v_color.a;
    frag_color = vec4(v_color.rgb * alpha, alpha);
}
