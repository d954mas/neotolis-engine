#pragma once

// Slug GPU vector text coverage, shared by slug_text.frag and slug_text_depth.frag.
// Ported from HLSL reference (github.com/EricLengyel/Slug, MIT license)
// Uses CalcRootCode and CalcCoverage verbatim; the solvers use a cancellation-free root form (below).

// RGBA16F; row = glyph band_row: band_count header texels (y_start, y_count, x_start, x_count), then curves.
uniform sampler2D u_curve_texture;

// Linear fallback for truly degenerate (a.y == 0) curves; near-tangential
// cases are handled by the Citardauq stable form in the solvers below.
#ifndef SLUG_LINEAR_FALLBACK_EPSILON
#define SLUG_LINEAR_FALLBACK_EPSILON (1.0 / 65536.0)
#endif

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

// Main coverage: Y-band horizontal ray + X-band vertical ray.
// glyph = (band_row, band_count); band_transform = (bbox x0, bbox y0, band_count / w, band_count / h).
float SlugRender(vec2 coord, uvec2 glyph, vec4 band_transform) {
    // Decoration sentinel: underline/strike/solid quads ride the text batch with band_count==0 AND
    // zeroed glyph bounds. The glyph path would read a 0/0 band scale and clamp with hi<lo
    // (band_count-1 == -1, UB), so this branch both forces solid coverage and skips that garbage.
    if (glyph.y == 0u)
        return 1.0;

    int band_row = int(glyph.x);
    uint band_count = glyph.y;

    vec2 pixelsPerEm = 1.0 / max(fwidth(coord), vec2(1.0e-6));
    vec2 halfPixelBehind = -0.5 / pixelsPerEm; // early-out threshold: half a pixel behind the sample, in em

    // ---- Y-band: horizontal ray (+X) ----
    float band_y = (coord.y - band_transform.y) * band_transform.w;
    int yband_idx = clamp(int(band_y), 0, int(band_count) - 1);
    uvec2 yband = uvec2(texelFetch(u_curve_texture, ivec2(yband_idx, band_row), 0).xy);

    float xcov = 0.0;
    float xwgt = 0.0;

    for (uint i = 0u; i < yband.y; i++) {
        uint ti = yband.x + i * 2u;
        vec4 d0 = texelFetch(u_curve_texture, ivec2(int(ti), band_row), 0);
        vec4 d1 = texelFetch(u_curve_texture, ivec2(int(ti) + 1, band_row), 0);
        vec2 p0 = d0.xy - coord;
        vec2 p1 = d0.zw - coord;
        vec2 p2 = d1.xy - coord;

        // Curves are sorted by descending max x: once one lies more than half a pixel left, so do the rest.
        if (max(max(p0.x, p1.x), p2.x) < halfPixelBehind.x)
            break;

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
    float band_x = (coord.x - band_transform.x) * band_transform.z;
    int xband_idx = clamp(int(band_x), 0, int(band_count) - 1);
    uvec2 xband = uvec2(texelFetch(u_curve_texture, ivec2(xband_idx, band_row), 0).zw);

    float ycov = 0.0;
    float ywgt = 0.0;

    for (uint i = 0u; i < xband.y; i++) {
        uint ti = xband.x + i * 2u;
        vec4 d0 = texelFetch(u_curve_texture, ivec2(int(ti), band_row), 0);
        vec4 d1 = texelFetch(u_curve_texture, ivec2(int(ti) + 1, band_row), 0);
        vec2 p0 = d0.xy - coord;
        vec2 p1 = d0.zw - coord;
        vec2 p2 = d1.xy - coord;

        // Sorted by descending max y: the first curve more than half a pixel below ends the band.
        if (max(max(p0.y, p1.y), p2.y) < halfPixelBehind.y)
            break;

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
