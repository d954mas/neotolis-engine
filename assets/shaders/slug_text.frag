precision highp float;
precision highp int;

// Slug GPU vector text fragment shader
// Ported from HLSL reference (github.com/EricLengyel/Slug, MIT license)
// Computes per-pixel winding numbers via band/curve texelFetch

uniform sampler2D u_curve_texture; // RGBA16F -- curve control points as float16
uniform usampler2D u_band_texture; // RG16UI -- (curve_start, curve_count) per band
uniform int u_curve_tex_width;     // For linear-to-2D addressing

in vec2 v_texcoord;
flat in uvec4 v_glyph;       // curve_offset, band_row, curve_count, band_count
flat in vec4 v_glyph_bounds; // bbox (x0, y0, x1, y1) in em-space
in vec4 v_color;

out vec4 frag_color;

// Convert linear texel offset to 2D texture coordinates for texelFetch
ivec2 CurveTexCoord(uint offset) { return ivec2(int(offset) % u_curve_tex_width, int(offset) / u_curve_tex_width); }

// Solve horizontal ray crossing for one quadratic Bezier curve.
// p0, p1, p2 are control points relative to the current fragment position.
// Returns signed coverage contribution for horizontal winding.
float SolveQuadratic(vec2 p0, vec2 p1, vec2 p2, float pixelSize) {
    // Quadratic: a*t^2 - 2*b*t + c = 0 (parametric y-solve)
    vec2 a = p0 - 2.0 * p1 + p2;
    vec2 b = p0 - p1;

    float result = 0.0;

    // Degenerate check: near-linear curve
    if (abs(a.y) < 1.0e-5) {
        // Linear fallback
        if (abs(b.y) < 1.0e-5)
            return 0.0;
        float t = p0.y / (2.0 * b.y);
        if (t >= 0.0 && t <= 1.0) {
            // Evaluate x at root t: x(t) = (1-t)^2*p0.x + 2*(1-t)*t*p1.x + t^2*p2.x
            float omt = 1.0 - t;
            float x = omt * omt * p0.x + 2.0 * omt * t * p1.x + t * t * p2.x;
            // Winding direction: sign of y derivative at root
            float dy = 2.0 * (a.y * t - b.y);
            float weight = smoothstep(-pixelSize, pixelSize, x);
            result = (dy > 0.0) ? weight : -weight;
        }
        return result;
    }

    // Discriminant
    float disc = b.y * b.y - a.y * p0.y;
    if (disc < 0.0)
        return 0.0;

    float d = sqrt(disc);
    float ra = 1.0 / a.y;

    // Two roots
    float t1 = (b.y - d) * ra;
    float t2 = (b.y + d) * ra;

    // Evaluate at first root
    if (t1 >= 0.0 && t1 <= 1.0) {
        float omt = 1.0 - t1;
        float x = omt * omt * p0.x + 2.0 * omt * t1 * p1.x + t1 * t1 * p2.x;
        float dy = 2.0 * (a.y * t1 - b.y);
        float weight = smoothstep(-pixelSize, pixelSize, x);
        result += (dy > 0.0) ? weight : -weight;
    }

    // Evaluate at second root
    if (t2 >= 0.0 && t2 <= 1.0) {
        float omt = 1.0 - t2;
        float x = omt * omt * p0.x + 2.0 * omt * t2 * p1.x + t2 * t2 * p2.x;
        float dy = 2.0 * (a.y * t2 - b.y);
        float weight = smoothstep(-pixelSize, pixelSize, x);
        result += (dy > 0.0) ? weight : -weight;
    }

    return result;
}

// Solve vertical ray crossing for one quadratic Bezier curve (swap x/y).
// Returns signed coverage contribution for vertical winding.
float SolveQuadraticVert(vec2 p0, vec2 p1, vec2 p2, float pixelSize) {
    // Same as SolveQuadratic but with x and y roles swapped
    vec2 a = p0 - 2.0 * p1 + p2;
    vec2 b = p0 - p1;

    float result = 0.0;

    // Degenerate check
    if (abs(a.x) < 1.0e-5) {
        if (abs(b.x) < 1.0e-5)
            return 0.0;
        float t = p0.x / (2.0 * b.x);
        if (t >= 0.0 && t <= 1.0) {
            float omt = 1.0 - t;
            float y = omt * omt * p0.y + 2.0 * omt * t * p1.y + t * t * p2.y;
            float dx = 2.0 * (a.x * t - b.x);
            float weight = smoothstep(-pixelSize, pixelSize, y);
            result = (dx > 0.0) ? -weight : weight;
        }
        return result;
    }

    float disc = b.x * b.x - a.x * p0.x;
    if (disc < 0.0)
        return 0.0;

    float d = sqrt(disc);
    float ra = 1.0 / a.x;

    float t1 = (b.x - d) * ra;
    float t2 = (b.x + d) * ra;

    if (t1 >= 0.0 && t1 <= 1.0) {
        float omt = 1.0 - t1;
        float y = omt * omt * p0.y + 2.0 * omt * t1 * p1.y + t1 * t1 * p2.y;
        float dx = 2.0 * (a.x * t1 - b.x);
        float weight = smoothstep(-pixelSize, pixelSize, y);
        result += (dx > 0.0) ? -weight : weight;
    }

    if (t2 >= 0.0 && t2 <= 1.0) {
        float omt = 1.0 - t2;
        float y = omt * omt * p0.y + 2.0 * omt * t2 * p1.y + t2 * t2 * p2.y;
        float dx = 2.0 * (a.x * t2 - b.x);
        float weight = smoothstep(-pixelSize, pixelSize, y);
        result += (dx > 0.0) ? -weight : weight;
    }

    return result;
}

// Main coverage computation via Y-band + X-band curve lookup
float CalcCoverage(vec2 coord) {
    uint curve_offset_y = v_glyph.x;
    uint band_row       = v_glyph.y;
    uint curve_offset_x = v_glyph.z;
    uint band_count     = v_glyph.w;

    vec2 pixelSize = fwidth(coord);
    float bbox_height = v_glyph_bounds.w - v_glyph_bounds.y;
    float bbox_width  = v_glyph_bounds.z - v_glyph_bounds.x;

    // ---- Y-band: horizontal winding (ray cast +X) ----
    float band_y = (coord.y - v_glyph_bounds.y) / bbox_height * float(band_count);
    int yband_idx = clamp(int(band_y), 0, int(band_count) - 1);
    uvec4 yband = texelFetch(u_band_texture, ivec2(yband_idx, int(band_row)), 0);

    float windingH = 0.0;
    uint ycurveBase = curve_offset_y + yband.r * 2u;
    for (uint i = 0u; i < yband.g; i++) {
        uint ti = ycurveBase + i * 2u;
        vec4 d0 = texelFetch(u_curve_texture, CurveTexCoord(ti), 0);
        vec4 d1 = texelFetch(u_curve_texture, CurveTexCoord(ti + 1u), 0);
        vec2 p0 = d0.xy - coord; vec2 p1 = d0.zw - coord; vec2 p2 = d1.xy - coord;
        windingH += SolveQuadratic(p0, p1, p2, pixelSize.x);
    }

    // ---- X-band: vertical winding (ray cast +Y) ----
    float band_x = (coord.x - v_glyph_bounds.x) / bbox_width * float(band_count);
    int xband_idx = clamp(int(band_x), 0, int(band_count) - 1);
    uvec4 xband = texelFetch(u_band_texture, ivec2(int(band_count) + xband_idx, int(band_row)), 0);

    float windingV = 0.0;
    uint xcurveBase = curve_offset_x + xband.r * 2u;
    for (uint i = 0u; i < xband.g; i++) {
        uint ti = xcurveBase + i * 2u;
        vec4 d0 = texelFetch(u_curve_texture, CurveTexCoord(ti), 0);
        vec4 d1 = texelFetch(u_curve_texture, CurveTexCoord(ti + 1u), 0);
        vec2 p0 = d0.xy - coord; vec2 p1 = d0.zw - coord; vec2 p2 = d1.xy - coord;
        windingV += SolveQuadraticVert(p0, p1, p2, pixelSize.y);
    }

    // Combined 2D anti-aliased fill coverage
    return min(abs(windingH), 1.0) * min(abs(windingV), 1.0);
}

void main() {
    float coverage = CalcCoverage(v_texcoord);

    // Skip fully transparent pixels
    if (coverage < 1.0 / 255.0)
        discard;

    // Premultiplied alpha output (D-27, SHD-04)
    // Blend mode: ONE, ONE_MINUS_SRC_ALPHA
    float alpha = coverage * v_color.a;
    frag_color = vec4(v_color.rgb * alpha, alpha);
}
