#ifndef NT_COLOR_H
#define NT_COLOR_H

/*
 * Header-only color math — Clay-free, UI-free, no state, no heap. Any consumer
 * (UI, renderers, materials, future gradients) can include this without pulling
 * in the UI stack. Works on packed uint32_t colors + normalized float channels.
 *
 * Packed convention: 0xAABBGGRR — R in the low byte, A in the high byte (the
 * engine's existing packed format).
 */

#include <math.h>
#include <stddef.h> /* NULL (nt_color_parse_hex) */
#include <stdbool.h>
#include <stdint.h>

/* Saturate a [0,1] channel. */
static inline float nt_color_clamp01(float c) {
    if (c < 0.0F) {
        return 0.0F;
    }
    return (c > 1.0F) ? 1.0F : c;
}

/* 0xAABBGGRR -> normalized [0,1] R,G,B,A. */
static inline void nt_color_unpack(uint32_t packed, float out_rgba[4]) {
    out_rgba[0] = (float)(packed & 0xFFU) / 255.0F;
    out_rgba[1] = (float)((packed >> 8) & 0xFFU) / 255.0F;
    out_rgba[2] = (float)((packed >> 16) & 0xFFU) / 255.0F;
    out_rgba[3] = (float)((packed >> 24) & 0xFFU) / 255.0F;
}

/* Saturate a [0,1] channel and round-to-nearest into a byte. NaN -> 0 (safe). */
static inline uint32_t nt_color_channel_to_u8(float c) {
    const float v = c * 255.0F;
    if (v <= 0.0F) {
        return 0U;
    }
    if (v >= 255.0F) {
        return 255U;
    }
    return (uint32_t)(v + 0.5F);
}

/* [0,1] R,G,B,A -> 0xAABBGGRR (clamp + round-to-nearest). Matches the engine's
 * 0..255 nt_clamp_f_to_u8 rounding exactly so packing is byte-identical. */
static inline uint32_t nt_color_pack(const float rgba[4]) {
    const uint32_t r = nt_color_channel_to_u8(rgba[0]);
    const uint32_t g = nt_color_channel_to_u8(rgba[1]);
    const uint32_t b = nt_color_channel_to_u8(rgba[2]);
    const uint32_t a = nt_color_channel_to_u8(rgba[3]);
    return r | (g << 8) | (b << 16) | (a << 24);
}

/* One hex nibble 0..15; 0xFF on a non-hex char. */
static inline uint8_t nt_color_hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (uint8_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (uint8_t)(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return (uint8_t)(c - 'A' + 10);
    }
    return 0xFFU;
}

/* Parse "#RRGGBB" (n==7, alpha forced 0xFF) or "#RRGGBBAA" (n==9) into packed 0xAABBGGRR.
 * PURE: no logging, no asserts, no side effects. Returns false (out untouched) on any malformed
 * input -- wrong length, missing '#', or a non-hex digit. Reads only [s, s+n); s may be
 * non-NUL-terminated. Callers layer their own validation/defaults/logging on top. */
static inline bool nt_color_parse_hex(const char *s, uint32_t n, uint32_t *out_packed) {
    if (s == NULL || out_packed == NULL || (n != 7U && n != 9U) || s[0] != '#') {
        return false;
    }
    uint32_t v = 0U; /* accumulates RRGGBB[AA] big-endian */
    for (uint32_t i = 1U; i < n; i++) {
        const uint8_t nib = nt_color_hex_nibble(s[i]);
        if (nib == 0xFFU) {
            return false;
        }
        v = (v << 4) | nib;
    }
    uint32_t r;
    uint32_t g;
    uint32_t b;
    uint32_t a;
    if (n == 7U) {
        r = (v >> 16) & 0xFFU;
        g = (v >> 8) & 0xFFU;
        b = v & 0xFFU;
        a = 0xFFU;
    } else {
        r = (v >> 24) & 0xFFU;
        g = (v >> 16) & 0xFFU;
        b = (v >> 8) & 0xFFU;
        a = v & 0xFFU;
    }
    *out_packed = r | (g << 8) | (b << 16) | (a << 24); /* 0xAABBGGRR */
    return true;
}

/* Björn Ottosson sRGB transfer (0..1 channel). */
static inline float nt_color_srgb_to_linear(float c) { return (c <= 0.04045F) ? (c / 12.92F) : powf((c + 0.055F) / 1.055F, 2.4F); }
static inline float nt_color_linear_to_srgb(float c) { return (c <= 0.0031308F) ? (c * 12.92F) : ((1.055F * powf(c, 1.0F / 2.4F)) - 0.055F); }

typedef struct {
    float l, a, b, alpha; /* OKLab L,a,b + LINEAR (0..1) alpha */
} nt_oklab_t;

/* The OKLab forward/backward transforms are 3x3 matrix multiplies; per-term parens would only obscure
 * the dot-products (readability-math-missing-parentheses fights the matrix form here). */
// NOLINTBEGIN(readability-math-missing-parentheses)

/* Linear-RGB -> OKLab (L,a,b); alpha is carried through verbatim. */
static inline nt_oklab_t nt_color_linear_rgb_to_oklab(float r, float g, float b, float alpha) {
    const float lc = 0.4122214708F * r + 0.5363325363F * g + 0.0514459929F * b;
    const float mc = 0.2119034982F * r + 0.6806995451F * g + 0.1073969566F * b;
    const float sc = 0.0883024619F * r + 0.2817188376F * g + 0.6299787005F * b;
    const float L = cbrtf(lc);
    const float M = cbrtf(mc);
    const float S = cbrtf(sc);
    return (nt_oklab_t){
        .l = 0.2104542553F * L + 0.7936177850F * M - 0.0040720468F * S,
        .a = 1.9779984951F * L - 2.4285922050F * M + 0.4505937099F * S,
        .b = 0.0259040371F * L + 0.7827717662F * M - 0.8086757660F * S,
        .alpha = alpha,
    };
}

/* OKLab (L,a,b) -> linear-RGB (no gamut clamp; caller transfers + clamps). */
static inline void nt_color_oklab_to_linear_rgb(nt_oklab_t o, float out_rgb[3]) {
    const float l_ = o.l + 0.3963377774F * o.a + 0.2158037573F * o.b;
    const float m_ = o.l - 0.1055613458F * o.a - 0.0638541728F * o.b;
    const float s_ = o.l - 0.0894841775F * o.a - 1.2914855480F * o.b;
    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;
    out_rgb[0] = 4.0767416621F * l - 3.3077115913F * m + 0.2309699292F * s;
    out_rgb[1] = -1.2684380046F * l + 2.6097574011F * m - 0.3413193965F * s;
    out_rgb[2] = -0.0041960863F * l - 0.7034186147F * m + 1.7076147010F * s;
}
// NOLINTEND(readability-math-missing-parentheses)

/* Packed sRGB 0xAABBGGRR -> OKLab (L,a,b) + linear alpha. `with_alpha`==false yields the
 * colour's L,a,b at alpha 0 so an appearing/disappearing channel cross-fades its alpha. */
static inline nt_oklab_t nt_color_packed_to_oklab(uint32_t packed, bool with_alpha) {
    float rgba[4];
    nt_color_unpack(packed, rgba);
    const float r = nt_color_srgb_to_linear(rgba[0]);
    const float g = nt_color_srgb_to_linear(rgba[1]);
    const float b = nt_color_srgb_to_linear(rgba[2]);
    nt_oklab_t o = nt_color_linear_rgb_to_oklab(r, g, b, with_alpha ? rgba[3] : 0.0F);
    return o;
}

/* OKLab (L,a,b) + linear alpha -> packed 0xAABBGGRR. Out-of-sRGB interpolants are gamut-clamped
 * after the sRGB transfer (matches the prior oklab_to_color clamp). */
static inline uint32_t nt_color_oklab_to_packed(nt_oklab_t o) {
    float rgb[3];
    nt_color_oklab_to_linear_rgb(o, rgb);
    const float rgba[4] = {
        nt_color_clamp01(nt_color_linear_to_srgb(rgb[0])),
        nt_color_clamp01(nt_color_linear_to_srgb(rgb[1])),
        nt_color_clamp01(nt_color_linear_to_srgb(rgb[2])),
        nt_color_clamp01(o.alpha),
    };
    return nt_color_pack(rgba);
}

/* cur += (tgt - cur) * t for all four lanes (L,a,b eased perceptually; alpha eased linearly). */
static inline void nt_color_oklab_lerp(nt_oklab_t *cur, nt_oklab_t tgt, float t) {
    cur->l += (tgt.l - cur->l) * t;
    cur->a += (tgt.a - cur->a) * t;
    cur->b += (tgt.b - cur->b) * t;
    cur->alpha += (tgt.alpha - cur->alpha) * t;
}

#endif /* NT_COLOR_H */
