/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_basisu_encoder.h"
#include "nt_texture_format.h"
#include "stb_image.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#include "stb_image_resize2.h"
#pragma clang diagnostic pop
/* clang-format on */

#include <math.h>
#include <stdlib.h>

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- assert expansions inflate the count
nt_texture_pixel_format_t nt_builder_assert_texture_opts(const nt_tex_opts_t *opts) {
    nt_tex_opts_t resolved = opts ? *opts : nt_tex_opts_defaults();
    nt_texture_pixel_format_t format = resolved.format ? resolved.format : NT_TEXTURE_FORMAT_RGBA8;

    NT_BUILD_ASSERT(nt_texture_pixel_format_valid(format) && "texture opts: invalid format");
    const nt_tex_process_t *process = &resolved.process;
    NT_BUILD_ASSERT((unsigned)process->rgb <= (unsigned)NT_TEX_RGB_NORMAL && "texture opts: process.rgb out of range");
    NT_BUILD_ASSERT((unsigned)process->alpha <= (unsigned)NT_TEX_ALPHA_DATA && "texture opts: process.alpha out of range");
    NT_BUILD_ASSERT((unsigned)process->resize_filter <= (unsigned)NT_TEX_FILTER_KAISER && "texture opts: process.resize_filter out of range");
    NT_BUILD_ASSERT((unsigned)process->mip_filter <= (unsigned)NT_TEX_FILTER_KAISER && "texture opts: process.mip_filter out of range");
    NT_BUILD_ASSERT((!nt_texture_format_is_srgb(format) || process->rgb == NT_TEX_RGB_COLOR) && "texture opts: an sRGB format requires process.rgb COLOR");
    NT_BUILD_ASSERT((process->alpha != NT_TEX_ALPHA_PREMULTIPLIED || format == NT_TEXTURE_FORMAT_RGBA8) && "texture opts: process.alpha PREMULTIPLIED requires RGBA8");
    /* Negative lobes would make negative HDR colours. */
    NT_BUILD_ASSERT((format != NT_TEXTURE_FORMAT_RGB9E5 || (process->resize_filter == NT_TEX_FILTER_BOX && process->mip_filter == NT_TEX_FILTER_BOX)) &&
                    "texture opts: RGB9E5 requires BOX filters -- use .process = (nt_tex_process_t){0}");
    /* Kaiser rings in alpha, in alpha-weighted edges and in numeric data. */
    const bool kaiser = process->resize_filter == NT_TEX_FILTER_KAISER || process->mip_filter == NT_TEX_FILTER_KAISER;
    NT_BUILD_ASSERT((!kaiser || (process->rgb == NT_TEX_RGB_COLOR && !nt_texture_pixel_format_has_alpha(format))) && "texture opts: KAISER requires process.rgb COLOR and a format without alpha");
    NT_BUILD_ASSERT((process->mip_filter == NT_TEX_FILTER_BOX || resolved.mips == NT_TEX_MIPS_BAKED) && "texture opts: a mip_filter other than BOX requires mips BAKED");
    /* GPU mips would not renormalize; R8 holds no vector and RGB9E5 is colour. */
    NT_BUILD_ASSERT((process->rgb != NT_TEX_RGB_NORMAL || (format != NT_TEXTURE_FORMAT_R8 && format != NT_TEXTURE_FORMAT_RGB9E5 && resolved.mips != NT_TEX_MIPS_RUNTIME)) &&
                    "texture opts: process.rgb NORMAL requires RGBA8, RGB8 or RG8 and mips NONE or BAKED");
    NT_BUILD_ASSERT(isfinite(process->alpha_cutoff) && process->alpha_cutoff >= 0.0F && process->alpha_cutoff <= 1.0F && "texture opts: process.alpha_cutoff must be in [0, 1]");
    NT_BUILD_ASSERT((process->alpha_cutoff == 0.0F || (nt_texture_pixel_format_has_alpha(format) && resolved.mips == NT_TEX_MIPS_BAKED)) &&
                    "texture opts: process.alpha_cutoff requires a format with alpha and mips BAKED");
    NT_BUILD_ASSERT((process->rgb != NT_TEX_RGB_NORMAL || !nt_texture_pixel_format_has_alpha(format) || process->alpha == NT_TEX_ALPHA_DATA) &&
                    "texture opts: process.rgb NORMAL with an alpha format requires process.alpha DATA");
    NT_BUILD_ASSERT((unsigned)resolved.filter_min <= (unsigned)NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR && "texture opts: filter_min out of range");
    NT_BUILD_ASSERT((unsigned)resolved.filter_mag <= (unsigned)NT_TEXTURE_DEFAULT_FILTER_LINEAR && "texture opts: filter_mag must be NEAREST or LINEAR");
    NT_BUILD_ASSERT((unsigned)resolved.wrap_u <= (unsigned)NT_TEXTURE_DEFAULT_WRAP_MIRRORED_REPEAT && "texture opts: wrap_u out of range");
    NT_BUILD_ASSERT((unsigned)resolved.wrap_v <= (unsigned)NT_TEXTURE_DEFAULT_WRAP_MIRRORED_REPEAT && "texture opts: wrap_v out of range");

    const nt_basisu_encode_opts_t *compress_opts = &resolved.compress;
    bool compressed = compress_opts->codec != NT_BASISU_CODEC_NONE;
    bool filter_min_uses_mips = resolved.filter_min >= NT_TEXTURE_DEFAULT_FILTER_NEAREST_MIPMAP_NEAREST;
    NT_BUILD_ASSERT((unsigned)resolved.mips <= (unsigned)NT_TEX_MIPS_BAKED && "texture opts: mips out of range");
    NT_BUILD_ASSERT((resolved.mips != NT_TEX_MIPS_NONE || !filter_min_uses_mips) && "texture opts: a mipmap filter_min requires mips RUNTIME or BAKED");

    if (compressed) {
        NT_BUILD_ASSERT((format == NT_TEXTURE_FORMAT_RGBA8 || format == NT_TEXTURE_FORMAT_RGB8 || format == NT_TEXTURE_FORMAT_SRGB8_ALPHA8 || format == NT_TEXTURE_FORMAT_SRGB8) &&
                        "texture opts: Basis compression requires RGBA8, RGB8, SRGB8_ALPHA8 or SRGB8");
        NT_BUILD_ASSERT((compress_opts->codec == NT_BASISU_CODEC_ETC1S || compress_opts->codec == NT_BASISU_CODEC_UASTC_LDR) && "texture opts: compression mode out of range");
        /* The runtime of the same configure refuses a codec whose NT_BASISU_HAS_* is OFF. */
        NT_BUILD_ASSERT(((compress_opts->codec == NT_BASISU_CODEC_ETC1S && NT_BASISU_HAS_ETC1S) || (compress_opts->codec == NT_BASISU_CODEC_UASTC_LDR && NT_BASISU_HAS_UASTC)) &&
                        "texture opts: codec NT_BASISU_HAS_* option is OFF in this build");
        if (compress_opts->codec == NT_BASISU_CODEC_ETC1S) {
            NT_BUILD_ASSERT((compress_opts->etc1s.quality >= 1 && compress_opts->etc1s.quality <= 255) && "texture opts: ETC1S quality must be 1..255");
            NT_BUILD_ASSERT((isfinite(compress_opts->etc1s.selector_rdo_threshold) && compress_opts->etc1s.selector_rdo_threshold >= 0.0F && compress_opts->etc1s.selector_rdo_threshold <= 1.0e10F) &&
                            "texture opts: ETC1S selector RDO must be finite and in 0..1e10");
            NT_BUILD_ASSERT((isfinite(compress_opts->etc1s.endpoint_rdo_threshold) && compress_opts->etc1s.endpoint_rdo_threshold >= 0.0F && compress_opts->etc1s.endpoint_rdo_threshold <= 1.0e10F) &&
                            "texture opts: ETC1S endpoint RDO must be finite and in 0..1e10");
        } else {
            NT_BUILD_ASSERT(compress_opts->uastc.pack_level <= 4 && "texture opts: UASTC pack level must be 0..4");
            NT_BUILD_ASSERT(
                (compress_opts->uastc.rdo_lambda == 0.0F || (isfinite(compress_opts->uastc.rdo_lambda) && compress_opts->uastc.rdo_lambda >= 0.001F && compress_opts->uastc.rdo_lambda <= 50.0F)) &&
                "texture opts: UASTC RDO lambda must be 0 or in 0.001..50");
        }
    }
    /* WebGL 2 generates mips only for color-renderable uncompressed formats. */
    NT_BUILD_ASSERT((resolved.mips != NT_TEX_MIPS_RUNTIME || (!compressed && format != NT_TEXTURE_FORMAT_SRGB8 && format != NT_TEXTURE_FORMAT_RGB9E5)) &&
                    "texture opts: mips RUNTIME cannot serve Basis, SRGB8 or RGB9E5 -- use BAKED or NONE");

    return format;
}

uint32_t nt_builder_texture_source_size(nt_texture_pixel_format_t format, uint32_t width, uint32_t height) {
    const uint64_t pixel_bytes = format == NT_TEXTURE_FORMAT_RGB9E5 ? 3U * sizeof(float) : 4U;
    const uint64_t size = (uint64_t)width * height * pixel_bytes;
    NT_BUILD_ASSERT(size <= UINT32_MAX && "texture: decoded source exceeds 4 GiB");
    return (uint32_t)size;
}

/* Values Basis UASTC HDR stores without the HDRScale factor a shader would have to apply
 * (ASTC_HDR_MAX_VAL); the builder neither clamps nor tone-maps. */
static void assert_hdr_range(const float *rgb, uint32_t pixel_count) {
    for (size_t i = 0; i < (size_t)pixel_count * 3U; i++) {
        NT_BUILD_ASSERT(isfinite(rgb[i]) && rgb[i] >= 0.0F && rgb[i] <= 65216.0F && "texture HDR: every RGB value must be finite and in [0, 65216]");
    }
}

void nt_builder_texture_target_extent(uint32_t width, uint32_t height, uint32_t max_size, uint32_t *out_w, uint32_t *out_h) {
    if (max_size == 0 || (width <= max_size && height <= max_size)) {
        *out_w = width;
        *out_h = height;
        return;
    }
    if (width >= height) {
        *out_w = max_size;
        *out_h = height * max_size / width;
    } else {
        *out_h = max_size;
        *out_w = width * max_size / height;
    }
    *out_w = *out_w < 1 ? 1 : *out_w;
    *out_h = *out_h < 1 ? 1 : *out_h;
}

/* Strip RGBA8 source pixels to target channel count (1..3) into `out`. */
static void strip_channels(const uint8_t *rgba, uint32_t pixel_count, uint32_t target_channels, uint8_t *out) {
    NT_BUILD_ASSERT(target_channels > 0 && target_channels < 4 && "strip_channels: invalid args");
    for (uint32_t i = 0; i < pixel_count; i++) {
        for (uint32_t c = 0; c < target_channels; c++) {
            out[(i * target_channels) + c] = rgba[(i * 4) + c];
        }
    }
}

/* --- Decode: image data -> source pixels (eager, called from add_*) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- assert expansions inflate the count
void *nt_builder_decode_texture(const uint8_t *src_data, uint32_t src_size, nt_texture_pixel_format_t format, uint32_t *out_w, uint32_t *out_h) {
    NT_BUILD_ASSERT(src_data && src_size != 0 && out_w && out_h && "texture decode: invalid args");
    /* No implicit conversion: stbi would tone-map an HDR file to 8 bits or gamma-expand an LDR one. */
    const bool hdr = format == NT_TEXTURE_FORMAT_RGB9E5;
    const bool source_hdr = stbi_is_hdr_from_memory(src_data, (int)src_size) != 0;
    NT_BUILD_ASSERT((!hdr || source_hdr) && "texture decode: RGB9E5 requires an HDR (.hdr) source");
    NT_BUILD_ASSERT((hdr || !source_hdr) && "texture decode: an HDR source requires format RGB9E5");
    int w = 0;
    int h = 0;
    int channels = 0;
    void *pixels = hdr ? (void *)stbi_loadf_from_memory(src_data, (int)src_size, &w, &h, &channels, 3) : (void *)stbi_load_from_memory(src_data, (int)src_size, &w, &h, &channels, 4);
    NT_BUILD_ASSERT(pixels && "texture decode: stbi_load_from_memory failed");
    if ((uint32_t)w > NT_BUILD_MAX_TEXTURE_SIZE || (uint32_t)h > NT_BUILD_MAX_TEXTURE_SIZE) {
        stbi_image_free(pixels);
        NT_BUILD_FAIL("texture decode: image exceeds NT_BUILD_MAX_TEXTURE_SIZE", "texture decode: %ux%u exceeds max %u", (uint32_t)w, (uint32_t)h, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
    }
    if (hdr) {
        assert_hdr_range((const float *)pixels, (uint32_t)w * (uint32_t)h);
    }

    *out_w = (uint32_t)w;
    *out_h = (uint32_t)h;
    /* stbi allocates with malloc, so the caller frees the buffer with free. */
    return pixels;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- assert expansions inflate the count
void *nt_builder_decode_texture_raw(const void *pixels, uint32_t width, uint32_t height, nt_texture_pixel_format_t format) {
    NT_BUILD_ASSERT(pixels && width != 0 && height != 0 && "texture raw: invalid args");
    if (width > NT_BUILD_MAX_TEXTURE_SIZE || height > NT_BUILD_MAX_TEXTURE_SIZE) {
        NT_BUILD_FAIL("texture raw: image exceeds NT_BUILD_MAX_TEXTURE_SIZE", "texture raw: %ux%u exceeds max %u", width, height, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
    }
    if (format == NT_TEXTURE_FORMAT_RGB9E5) {
        assert_hdr_range((const float *)pixels, width * height);
    }

    /* The caller owns the result, so the source is copied. */
    const uint32_t size = nt_builder_texture_source_size(format, width, height);
    void *result = malloc(size);
    NT_BUILD_ASSERT(result && "texture raw: malloc failed");
    memcpy(result, pixels, size);
    return result;
}

/* EXT_texture_shared_exponent reference encoder (9-bit mantissas, exponent bias 15).
 * No clamp to the format maximum: the asserted source range [0, 65216] and the box
 * filter keep every value below it. */
static uint32_t pack_rgb9e5(const float *rgb) {
    const float max_c = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));
    if (max_c == 0.0F) {
        return 0;
    }
    /* frexpf gives floor(log2) exactly where log2f misrounds next to powers of two. */
    int e = 0;
    (void)frexpf(max_c, &e);
    int exp_shared = (e - 1 < -16 ? -16 : e - 1) + 16;
    /* roundf is the reference floor(x + 0.5) without the float add, which rounds
     * 0.5 - 2^-25 up to 1. */
    if (roundf(ldexpf(max_c, 24 - exp_shared)) == 512.0F) {
        exp_shared++;
    }
    uint32_t packed = (uint32_t)exp_shared << 27U;
    for (uint32_t k = 0; k < 3; k++) {
        packed |= (uint32_t)roundf(ldexpf(rgb[k], 24 - exp_shared)) << (9U * k);
    }
    return packed;
}

// #region coverage
/* The alpha byte the shader's test passes from: the smallest b with b / 255 >= cutoff. */
static uint32_t cutoff_byte(float cutoff) {
    uint32_t b = 1;
    while (b < 255 && (float)b / 255.0F < cutoff) {
        b++;
    }
    return b;
}

/* counts[t] = texels with alpha >= t, for t in 0..256, from one histogram. */
static void alpha_counts(const uint8_t *rgba, size_t texels, uint64_t counts[257]) {
    uint64_t histogram[256] = {0};
    for (size_t i = 0; i < texels; i++) {
        histogram[rgba[(i * 4U) + 3U]]++;
    }
    counts[256] = 0;
    for (uint32_t t = 256; t-- > 0;) {
        counts[t] = counts[t + 1] + histogram[t];
    }
}

/* Scales a stored level's alpha so its coverage at `threshold` is the attainable
 * one closest to level 0's (covered0 of texels0). Candidates are the coverage at
 * every byte t, and zero; a tie keeps the current coverage, then the larger t.
 * s = (threshold - 0.5) / (t - 0.5) moves exactly the texels at or above t to at
 * or above threshold. */
static void preserve_coverage(uint8_t *rgba, size_t texels, uint32_t threshold, uint64_t covered0, size_t texels0) {
    uint64_t counts[257];
    alpha_counts(rgba, texels, counts);
    /* |counts[t] / texels - covered0 / texels0|, compared exactly in integers. */
    const int64_t target = (int64_t)(covered0 * texels);
    uint32_t best = threshold;
    int64_t best_error = llabs(((int64_t)(counts[threshold] * texels0)) - target);
    for (uint32_t t = 256; t >= 1; t--) {
        const int64_t error = llabs(((int64_t)(counts[t] * texels0)) - target);
        if (error < best_error) {
            best = t;
            best_error = error;
        }
    }
    if (best == threshold) {
        return;
    }
    uint32_t max_alpha = 0;
    for (size_t i = 0; i < texels; i++) {
        max_alpha = rgba[(i * 4U) + 3U] > max_alpha ? rgba[(i * 4U) + 3U] : max_alpha;
    }
    const float scale = ((float)threshold - 0.5F) / (best == 256 ? (float)max_alpha + 0.5F : (float)best - 0.5F);
    for (size_t i = 0; i < texels; i++) {
        uint8_t *a = &rgba[(i * 4U) + 3U];
        *a = (uint8_t)fminf(255.0F, floorf(((float)*a * scale) + 0.5F));
    }
}
// #endregion

/* Bytes of `level_count` levels of `pixel_bytes` texels, back to back. */
static size_t chain_bytes(uint32_t width, uint32_t height, uint32_t level_count, uint32_t pixel_bytes) {
    size_t total = (size_t)width * height * pixel_bytes;
    for (uint32_t level = 1; level < level_count; level++) {
        total += (size_t)nt_texture_level_extent(width, level) * nt_texture_level_extent(height, level) * pixel_bytes;
    }
    return total;
}

/* --- Level pipeline: level 0, then each level filtered from the previous core --- */

/* MIRROR filters as CLAMP: stb's REFLECT is one texel off GL's mirrored repeat. */
static stbir_edge wrap_edge(nt_texture_default_wrap_t wrap) { return wrap == NT_TEXTURE_DEFAULT_WRAP_REPEAT ? STBIR_EDGE_WRAP : STBIR_EDGE_CLAMP; }

static double bessel0(double x) {
    const double half = 0.5 * x;
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 64; k++) {
        term *= half / k;
        const double add = term * term;
        sum += add;
        if (add <= sum * 1e-16) {
            break;
        }
    }
    return sum;
}

/* basisu's kaiser_filter: a windowed sinc over support 3 with 40 dB attenuation. */
static float kaiser_kernel(float x, float scale, void *user_data) {
    (void)scale;
    (void)user_data;
    const double t = fabs((double)x);
    if (t >= 3.0) {
        return 0.0F;
    }
    const double att = 40.0;
    const double alpha = exp(log(0.58417 * (att - 20.96)) * 0.4) + (0.07886 * (att - 20.96));
    const double ratio = t / 3.0;
    const double window = bessel0(alpha * sqrt(1.0 - (ratio * ratio))) / bessel0(alpha);
    const double pi_t = 3.14159265358979323846 * t;
    const double sinc = t < 1e-6 ? 1.0 : sin(pi_t) / pi_t;
    return (float)(sinc * window);
}

static float kaiser_support(float scale, void *user_data) {
    (void)scale;
    (void)user_data;
    return 3.0F;
}

/* One stb resize with the texture's filter and per-axis edges. */
static void resize_level(const void *in, uint32_t in_w, uint32_t in_h, stbir_datatype in_type, stbir_pixel_layout in_layout, void *out, uint32_t out_w, uint32_t out_h, stbir_datatype out_type,
                         stbir_pixel_layout out_layout, nt_tex_filter_t filter, const nt_tex_opts_t *opts) {
    STBIR_RESIZE resize;
    stbir_resize_init(&resize, in, (int)in_w, (int)in_h, 0, out, (int)out_w, (int)out_h, 0, in_layout, in_type);
    stbir_set_datatypes(&resize, in_type, out_type);
    (void)stbir_set_pixel_layouts(&resize, in_layout, out_layout);
    /* stb's wrapped Kaiser breaks at a 2-px output along a wrapped axis (a buffer
       overlap across, a crash down); levels that small gain nothing from it. */
    const bool kaiser_breaks = (opts->wrap_u == NT_TEXTURE_DEFAULT_WRAP_REPEAT && out_w <= 2) || (opts->wrap_v == NT_TEXTURE_DEFAULT_WRAP_REPEAT && out_h <= 2);
    if (filter == NT_TEX_FILTER_KAISER && kaiser_breaks) {
        filter = NT_TEX_FILTER_BOX;
    }
    /* A downscaling box never reaches past an edge, and stb's wrapped box returns
       garbage at some ratios (70 -> 2), so it always runs clamped. */
    const bool box = filter == NT_TEX_FILTER_BOX;
    (void)stbir_set_edgemodes(&resize, box ? STBIR_EDGE_CLAMP : wrap_edge(opts->wrap_u), box ? STBIR_EDGE_CLAMP : wrap_edge(opts->wrap_v));
    if (filter == NT_TEX_FILTER_KAISER) {
        (void)stbir_set_filter_callbacks(&resize, kaiser_kernel, kaiser_support, kaiser_kernel, kaiser_support);
    } else {
        const stbir_filter stb_filter = filter == NT_TEX_FILTER_MITCHELL ? STBIR_FILTER_MITCHELL : STBIR_FILTER_BOX;
        (void)stbir_set_filters(&resize, stb_filter, stb_filter);
    }
    const int resized = stbir_resize_extended(&resize);
    NT_BUILD_ASSERT(resized && "texture resize: stbir_resize_extended failed");
}

static float half_to_float(uint16_t half) {
    const uint32_t exponent = (half >> 10U) & 0x1FU;
    const uint32_t mantissa = half & 0x3FFU;
    NT_BUILD_ASSERT(exponent != 0x1FU && "texture levels: non-finite half-float core");
    const float magnitude = exponent == 0 ? ldexpf((float)mantissa, -24) : ldexpf((float)(mantissa | 0x400U), (int)exponent - 25);
    return (half & 0x8000U) ? -magnitude : magnitude;
}

static float srgb_to_linear(float v) { return v <= 0.04045F ? v / 12.92F : powf((v + 0.055F) / 1.055F, 2.4F); }

static float linear_to_srgb(float v) { return v <= 0.0031308F ? v * 12.92F : (1.055F * powf(v, 1.0F / 2.4F)) - 0.055F; }

static uint8_t unit_to_byte(float v) {
    const float clamped = fminf(fmaxf(v, 0.0F), 1.0F);
    return (uint8_t)floorf((clamped * 255.0F) + 0.5F);
}

/* A stored straight-alpha level from a half-float linear core. A premultiplied
 * core is divided by its alpha only here, so a near-zero alpha never feeds the
 * next level; ringing is clamped only here too. */
static void quantize_core(const uint16_t *core, size_t texels, bool srgb, bool premultiplied, uint8_t *out) {
    for (size_t i = 0; i < texels; i++) {
        const float a = half_to_float(core[(i * 4U) + 3U]);
        for (uint32_t c = 0; c < 3; c++) {
            float v = half_to_float(core[(i * 4U) + c]);
            if (premultiplied) {
                v = a > 0.0F ? v / a : 0.0F;
            }
            out[(i * 4U) + c] = unit_to_byte(srgb ? linear_to_srgb(fmaxf(v, 0.0F)) : v);
        }
        out[(i * 4U) + 3U] = unit_to_byte(a);
    }
}

/* Round-to-nearest float to half, for values in [0, 1]. */
static uint16_t float_to_half(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    const int32_t exponent = (int32_t)((bits >> 23U) & 0xFFU) - 127 + 15;
    uint32_t mantissa = bits & 0x7FFFFFU;
    NT_BUILD_ASSERT(value >= 0.0F && value <= 1.0F && "texture levels: half-float core value outside [0, 1]");
    if (exponent <= 0) {
        if (exponent < -10) {
            return 0;
        }
        mantissa |= 0x800000U;
        const uint32_t shift = (uint32_t)(14 - exponent);
        return (uint16_t)((mantissa >> shift) + ((mantissa >> (shift - 1U)) & 1U));
    }
    return (uint16_t)((((uint32_t)exponent << 10U) | (mantissa >> 13U)) + ((mantissa >> 12U) & 1U));
}

/* Encoded normal (c * 2 - 1) to its unit vector, encoded again. An average
 * shorter than about two byte steps (opposite normals cancelling; a byte cannot
 * hold an exact 0) becomes +Z. */
static void unit_normal(float v[3]) {
    for (uint32_t c = 0; c < 3; c++) {
        v[c] = (v[c] * 2.0F) - 1.0F;
    }
    const float length = sqrtf((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
    if (length < 0.02F) {
        v[0] = 0.5F;
        v[1] = 0.5F;
        v[2] = 1.0F;
        return;
    }
    for (uint32_t c = 0; c < 3; c++) {
        v[c] = (v[c] / length * 0.5F) + 0.5F;
    }
}

static void normalize_core(uint16_t *core, size_t texels) {
    for (size_t i = 0; i < texels; i++) {
        float v[3];
        for (uint32_t c = 0; c < 3; c++) {
            v[c] = half_to_float(core[(i * 4U) + c]);
        }
        unit_normal(v);
        for (uint32_t c = 0; c < 3; c++) {
            core[(i * 4U) + c] = float_to_half(fminf(fmaxf(v[c], 0.0F), 1.0F));
        }
    }
}

/* RG8 normals store XY only, so Z is rebuilt for filtering and normalization. */
static void rebuild_normal_z(uint8_t *rgba, size_t texels) {
    for (size_t i = 0; i < texels; i++) {
        const float x = ((float)rgba[i * 4U] / 127.5F) - 1.0F;
        const float y = ((float)rgba[(i * 4U) + 1U] / 127.5F) - 1.0F;
        rgba[(i * 4U) + 2U] = unit_to_byte((sqrtf(fmaxf(0.0F, 1.0F - (x * x) - (y * y))) * 0.5F) + 0.5F);
    }
}

// #region dilation
/* Texels at or below this alpha are holes: alpha-weighted filters divide ringing
 * by a tiny A, so their RGB is garbage and gets replaced. */
#define DILATE_HOLE_ALPHA 8U

typedef struct {
    float lin[256]; /* stored byte -> linear value */
    bool srgb;
    bool wrap_x;
    bool wrap_y;
} dilate_ctx_t;

static uint8_t encode_linear(const dilate_ctx_t *dc, float v) { return unit_to_byte(dc->srgb ? linear_to_srgb(fmaxf(v, 0.0F)) : v); }

/* Neighbour coordinate by the axis edge rule; -1 when CLAMP leaves the level. */
static int32_t edge_coord(int32_t c, uint32_t n, bool wrap) {
    if (c >= 0 && c < (int32_t)n) {
        return c;
    }
    return wrap ? ((c % (int32_t)n) + (int32_t)n) % (int32_t)n : -1;
}

/* Alpha-weighted mean colour of the visible 8-neighbours of (x, y), in linear
 * light; returns their mean alpha, 0 when none is visible. */
static float ring_mean(const dilate_ctx_t *dc, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t x, uint32_t y, float rgb[3]) {
    float sum[3] = {0};
    float weight = 0.0F;
    uint32_t donors = 0;
    for (int32_t d = 0; d < 9; d++) {
        const int32_t nx = edge_coord((int32_t)x + (d % 3) - 1, w, dc->wrap_x);
        const int32_t ny = edge_coord((int32_t)y + (d / 3) - 1, h, dc->wrap_y);
        const uint8_t *n = nx < 0 || ny < 0 ? NULL : &rgba[(((size_t)ny * w) + (uint32_t)nx) * 4U];
        if (!n || n[3] <= DILATE_HOLE_ALPHA) {
            continue;
        }
        for (uint32_t c = 0; c < 3; c++) {
            sum[c] += (float)n[3] * dc->lin[n[c]];
        }
        weight += (float)n[3];
        donors++;
    }
    if (donors == 0) {
        return 0.0F;
    }
    for (uint32_t c = 0; c < 3; c++) {
        rgb[c] = sum[c] / weight;
    }
    return weight / (float)donors;
}

/* Each hole touching visible texels takes ring_mean; `ring_a` records the mean
 * alpha as the hole's push-pull weight. Holes are read as holes throughout, so the
 * pass never chains. */
static void dilate_ring(const dilate_ctx_t *dc, uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *ring_a) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *px = &rgba[(((size_t)y * w) + x) * 4U];
            float rgb[3] = {0};
            const float mean_a = px[3] <= DILATE_HOLE_ALPHA ? ring_mean(dc, rgba, w, h, x, y, rgb) : 0.0F;
            if (mean_a == 0.0F) {
                continue;
            }
            for (uint32_t c = 0; c < 3; c++) {
                px[c] = encode_linear(dc, rgb[c]);
            }
            ring_a[((size_t)y * w) + x] = (uint8_t)(mean_a + 0.5F);
        }
    }
}

/* Push-pull pyramid: level 0 is the stored level itself, levels 1.. are
 * (r, g, b, weight) floats by ceil-halving. */
typedef struct {
    const dilate_ctx_t *dc;
    uint8_t *rgba;
    const uint8_t *ring_a;
    uint32_t dims[34][2]; /* [0] is the stored level */
    size_t offsets[34];   /* into `data`, for levels 1.. */
    uint32_t count;       /* levels including the stored one */
    float *data;
} dilate_pyr_t;

/* Linear RGB and weight of texel `f` of pyramid level `k`. */
static float pyr_texel(const dilate_pyr_t *p, uint32_t k, size_t f, float rgb[3]) {
    if (k > 0) {
        const float *t = &p->data[p->offsets[k] + (f * 4U)];
        memcpy(rgb, t, 3U * sizeof(float));
        return t[3];
    }
    const uint8_t *px = &p->rgba[f * 4U];
    for (uint32_t c = 0; c < 3; c++) {
        rgb[c] = p->dc->lin[px[c]];
    }
    return (float)(px[3] > DILATE_HOLE_ALPHA ? px[3] : p->ring_a[f]) / 255.0F;
}

/* Level k: the alpha-weighted mean of its up to four children, weight capped at 1. */
static void pull_level(dilate_pyr_t *p, uint32_t k) {
    const uint32_t fw = p->dims[k - 1][0];
    const uint32_t fh = p->dims[k - 1][1];
    for (uint32_t y = 0; y < p->dims[k][1]; y++) {
        for (uint32_t x = 0; x < p->dims[k][0]; x++) {
            float sum[4] = {0};
            for (uint32_t child = 0; child < 4; child++) {
                const uint32_t fx = (2 * x) + (child & 1U);
                const uint32_t fy = (2 * y) + (child >> 1U);
                if (fx >= fw || fy >= fh) {
                    continue;
                }
                float rgb[3];
                const float weight = pyr_texel(p, k - 1, ((size_t)fy * fw) + fx, rgb);
                for (uint32_t c = 0; c < 3; c++) {
                    sum[c] += weight * rgb[c];
                }
                sum[3] += weight;
            }
            float *t = &p->data[p->offsets[k] + ((((size_t)y * p->dims[k][0]) + x) * 4U)];
            for (uint32_t c = 0; c < 3 && sum[3] > 0.0F; c++) {
                t[c] = sum[c] / sum[3];
            }
            t[3] = fminf(sum[3], 1.0F);
        }
    }
}

/* A filter tap: wrapped, or clamped to the nearest edge texel. */
static int32_t tap_coord(int32_t c, uint32_t n, bool wrap) {
    const int32_t e = edge_coord(c, n, wrap);
    if (e >= 0) {
        return e;
    }
    return c < 0 ? 0 : (int32_t)n - 1;
}

/* Bilinear (tent) sample of filled pyramid level k at the parent position of
 * texel (x, y) of level k - 1, by the axis edge rule. */
static void tent_sample(const dilate_pyr_t *p, uint32_t k, uint32_t x, uint32_t y, float out[3]) {
    const uint32_t cw = p->dims[k][0];
    const uint32_t ch = p->dims[k][1];
    const float u = ((float)x * 0.5F) - 0.25F;
    const float v = ((float)y * 0.5F) - 0.25F;
    const int32_t x0 = (int32_t)floorf(u);
    const int32_t y0 = (int32_t)floorf(v);
    const float wx[2] = {1.0F - (u - (float)x0), u - (float)x0};
    const float wy[2] = {1.0F - (v - (float)y0), v - (float)y0};
    const int32_t xs[2] = {tap_coord(x0, cw, p->dc->wrap_x), tap_coord(x0 + 1, cw, p->dc->wrap_x)};
    const int32_t ys[2] = {tap_coord(y0, ch, p->dc->wrap_y), tap_coord(y0 + 1, ch, p->dc->wrap_y)};
    out[0] = out[1] = out[2] = 0.0F;
    for (uint32_t tap = 0; tap < 4; tap++) {
        const uint32_t i = tap & 1U;
        const uint32_t j = tap >> 1U;
        const float *t = &p->data[p->offsets[k] + ((((size_t)ys[j] * cw) + (size_t)xs[i]) * 4U)];
        for (uint32_t c = 0; c < 3; c++) {
            out[c] += wx[i] * wy[j] * t[c];
        }
    }
}

/* Level k >= 1 blends each partly known texel toward its filled parent. */
static void push_level(dilate_pyr_t *p, uint32_t k) {
    for (uint32_t y = 0; y < p->dims[k][1]; y++) {
        for (uint32_t x = 0; x < p->dims[k][0]; x++) {
            float *t = &p->data[p->offsets[k] + ((((size_t)y * p->dims[k][0]) + x) * 4U)];
            float parent[3];
            tent_sample(p, k + 1, x, y, parent);
            for (uint32_t c = 0; c < 3; c++) {
                t[c] = (t[3] * t[c]) + ((1.0F - t[3]) * parent[c]);
            }
            t[3] = 1.0F;
        }
    }
}

/* The stored level's holes the ring pass left take the filled level 1 by tent. */
static void fill_holes(const dilate_pyr_t *p) {
    const uint32_t w = p->dims[0][0];
    for (uint32_t y = 0; y < p->dims[0][1]; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *px = &p->rgba[(((size_t)y * w) + x) * 4U];
            if (px[3] > DILATE_HOLE_ALPHA || p->ring_a[((size_t)y * w) + x] != 0) {
                continue;
            }
            float parent[3];
            tent_sample(p, 1, x, y, parent);
            for (uint32_t c = 0; c < 3; c++) {
                px[c] = encode_linear(p->dc, parent[c]);
            }
        }
    }
}

/* Fills the RGB of every hole of one stored level: a ring pass, then push-pull
 * over alpha-weighted float sums (pull by ceil-halving, push by tent). The level
 * must have a visible texel. */
static void dilate_level(const dilate_ctx_t *dc, uint8_t *rgba, uint32_t w, uint32_t h) {
    uint8_t *ring_a = (uint8_t *)calloc((size_t)w * h, 1);
    NT_BUILD_ASSERT(ring_a && "texture dilation: malloc failed");
    dilate_ring(dc, rgba, w, h, ring_a);

    dilate_pyr_t p = {.dc = dc, .rgba = rgba, .ring_a = ring_a, .dims = {{w, h}}, .count = 1};
    size_t total = 0;
    while (p.dims[p.count - 1][0] > 1 || p.dims[p.count - 1][1] > 1) {
        p.dims[p.count][0] = (p.dims[p.count - 1][0] + 1) / 2;
        p.dims[p.count][1] = (p.dims[p.count - 1][1] + 1) / 2;
        p.offsets[p.count] = total;
        total += (size_t)p.dims[p.count][0] * p.dims[p.count][1] * 4U;
        p.count++;
    }
    if (p.count > 1) { /* a 1x1 level with a visible texel has no hole */
        p.data = (float *)calloc(total, sizeof(float));
        NT_BUILD_ASSERT(p.data && "texture dilation: malloc failed");
        for (uint32_t k = 1; k < p.count; k++) {
            pull_level(&p, k);
        }
        for (uint32_t k = p.count - 2; k >= 1; k--) {
            push_level(&p, k);
        }
        fill_holes(&p);
        free(p.data);
    }
    free(ring_a);
}

/* Whether a stored level has a visible texel and whether it has a hole. */
static void scan_level(const uint8_t *rgba, size_t texels, bool *visible, bool *hole) {
    *visible = false;
    *hole = false;
    for (size_t i = 0; i < texels && !(*visible && *hole); i++) {
        const bool texel_visible = rgba[(i * 4U) + 3U] > DILATE_HOLE_ALPHA;
        *visible = *visible || texel_visible;
        *hole = *hole || !texel_visible;
    }
}

/* A level with nothing visible takes the colour of a box filter of the previous
 * level (area weights, linear light for sRGB) and keeps its own alpha. */
static void fill_empty_level(uint8_t *rgba, uint32_t w, uint32_t h, const uint8_t *prev, uint32_t pw, uint32_t ph, stbir_datatype byte_type, const nt_tex_opts_t *opts) {
    const size_t texels = (size_t)w * h;
    uint8_t *box = (uint8_t *)malloc(texels * 4U);
    NT_BUILD_ASSERT(box && "texture dilation: malloc failed");
    resize_level(prev, pw, ph, byte_type, STBIR_RGBA_NO_AW, box, w, h, byte_type, STBIR_RGBA_NO_AW, NT_TEX_FILTER_BOX, opts);
    for (size_t i = 0; i < texels; i++) {
        memcpy(&rgba[i * 4U], &box[i * 4U], 3);
    }
    free(box);
}
// #endregion

/* Stored RGBA8 levels of a w x h texture, back to back. Level 0 is the source
 * (opaque for a format without alpha), copied or resized with resize_filter.
 * Level n filters level n-1's core with mip_filter: level 0's bytes, then
 * half-float linear values, so quantization and sRGB rounding never accumulate.
 * Dilation and premultiply touch only the stored copies. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one pass per pipeline stage
static uint8_t *build_ldr_levels(const uint8_t *source, uint32_t src_w, uint32_t src_h, uint32_t w, uint32_t h, uint32_t level_count, const nt_tex_opts_t *opts) {
    const nt_tex_process_t *process = &opts->process;
    const bool alpha = nt_texture_pixel_format_has_alpha(opts->format);
    const bool srgb = nt_texture_format_is_srgb(opts->format);
    const stbir_datatype byte_type = srgb ? STBIR_TYPE_UINT8_SRGB : STBIR_TYPE_UINT8;
    /* Alpha-weighted layouts filter straight bytes into a premultiplied core. */
    const bool weighted = alpha && process->alpha != NT_TEX_ALPHA_DATA;
    const bool normal = process->rgb == NT_TEX_RGB_NORMAL;
    const stbir_pixel_layout layout = weighted ? STBIR_RGBA : STBIR_RGBA_NO_AW;
    const stbir_pixel_layout core_layout = weighted ? STBIR_RGBA_PM : STBIR_RGBA_NO_AW;
    uint8_t *chain = (uint8_t *)malloc(chain_bytes(w, h, level_count, 4));
    NT_BUILD_ASSERT(chain && "texture levels: malloc failed");

    // #region level 0
    const size_t src_texels = (size_t)src_w * src_h;
    uint8_t *opaque = NULL;
    if (!alpha) {
        opaque = (uint8_t *)malloc(src_texels * 4U);
        NT_BUILD_ASSERT(opaque && "texture levels: malloc failed");
        memcpy(opaque, source, src_texels * 4U);
        for (size_t i = 0; i < src_texels; i++) {
            opaque[(i * 4U) + 3U] = 255;
        }
        if (normal && opts->format == NT_TEXTURE_FORMAT_RG8) {
            rebuild_normal_z(opaque, src_texels);
        }
        source = opaque;
    }
    if (src_w == w && src_h == h) {
        memcpy(chain, source, src_texels * 4U);
    } else if (normal) {
        /* Normalized in float before the bytes, like every mip. */
        uint16_t *resized = (uint16_t *)malloc((size_t)w * h * 4U * sizeof(uint16_t));
        NT_BUILD_ASSERT(resized && "texture levels: malloc failed");
        resize_level(source, src_w, src_h, byte_type, layout, resized, w, h, STBIR_TYPE_HALF_FLOAT, core_layout, process->resize_filter, opts);
        normalize_core(resized, (size_t)w * h);
        quantize_core(resized, (size_t)w * h, srgb, weighted, chain);
        free(resized);
    } else {
        resize_level(source, src_w, src_h, byte_type, layout, chain, w, h, byte_type, layout, process->resize_filter, opts);
    }
    free(opaque);
    // #endregion

    // #region chain
    if (level_count > 1) {
        const size_t core_halves = (size_t)nt_texture_level_extent(w, 1) * nt_texture_level_extent(h, 1) * 4U;
        uint16_t *core = (uint16_t *)malloc(core_halves * sizeof(uint16_t));
        uint16_t *next = (uint16_t *)malloc(core_halves * sizeof(uint16_t));
        NT_BUILD_ASSERT(core && next && "texture levels: malloc failed");
        uint8_t *stored = chain;
        for (uint32_t level = 1; level < level_count; level++) {
            const uint32_t pw = nt_texture_level_extent(w, level - 1);
            const uint32_t ph = nt_texture_level_extent(h, level - 1);
            const uint32_t lw = nt_texture_level_extent(w, level);
            const uint32_t lh = nt_texture_level_extent(h, level);
            if (level == 1) {
                resize_level(chain, pw, ph, byte_type, layout, next, lw, lh, STBIR_TYPE_HALF_FLOAT, core_layout, process->mip_filter, opts);
            } else {
                resize_level(core, pw, ph, STBIR_TYPE_HALF_FLOAT, core_layout, next, lw, lh, STBIR_TYPE_HALF_FLOAT, core_layout, process->mip_filter, opts);
            }
            uint16_t *swap = core;
            core = next;
            next = swap;
            if (normal) {
                normalize_core(core, (size_t)lw * lh);
            }
            stored += (size_t)pw * ph * 4U;
            quantize_core(core, (size_t)lw * lh, srgb, weighted, stored);
        }
        free(core);
        free(next);
    }
    // #endregion

    // #region stored copies
    bool visible = false;
    bool hole = false;
    scan_level(chain, (size_t)w * h, &visible, &hole);
    const bool dilate = alpha && process->alpha == NT_TEX_ALPHA_STRAIGHT && visible;
    dilate_ctx_t dc = {.srgb = srgb, .wrap_x = opts->wrap_u == NT_TEXTURE_DEFAULT_WRAP_REPEAT, .wrap_y = opts->wrap_v == NT_TEXTURE_DEFAULT_WRAP_REPEAT};
    for (uint32_t i = 0; i < 256; i++) {
        dc.lin[i] = srgb ? srgb_to_linear((float)i / 255.0F) : (float)i / 255.0F;
    }
    const uint32_t threshold = process->alpha_cutoff > 0.0F ? cutoff_byte(process->alpha_cutoff) : 0;
    uint64_t counts0[257];
    alpha_counts(chain, (size_t)w * h, counts0);
    uint8_t *level_px = chain;
    const uint8_t *prev = NULL;
    for (uint32_t level = 0; level < level_count; level++) {
        const uint32_t lw = nt_texture_level_extent(w, level);
        const uint32_t lh = nt_texture_level_extent(h, level);
        const size_t texels = (size_t)lw * lh;
        if (threshold > 0 && level > 0) {
            preserve_coverage(level_px, texels, threshold, counts0[threshold], (size_t)w * h);
        }
        if (dilate) {
            scan_level(level_px, texels, &visible, &hole);
            if (!visible) {
                fill_empty_level(level_px, lw, lh, prev, nt_texture_level_extent(w, level - 1), nt_texture_level_extent(h, level - 1), byte_type, opts);
            } else if (hole) {
                dilate_level(&dc, level_px, lw, lh);
            }
        }
        if (process->alpha == NT_TEX_ALPHA_PREMULTIPLIED) {
            for (size_t i = 0; i < texels; i++) {
                uint8_t *px = &level_px[i * 4U];
                for (uint32_t c = 0; c < 3; c++) {
                    px[c] = (uint8_t)((((uint32_t)px[c] * px[3]) + 127U) / 255U);
                }
            }
        }
        prev = level_px;
        level_px += texels * 4U;
    }
    // #endregion
    return chain;
}

/* HDR levels as float RGB: box filtered, since negative lobes would make negative colours. */
static float *build_hdr_levels(const float *source, uint32_t src_w, uint32_t src_h, uint32_t w, uint32_t h, uint32_t level_count, const nt_tex_opts_t *opts) {
    float *chain = (float *)malloc(chain_bytes(w, h, level_count, 3U * sizeof(float)));
    NT_BUILD_ASSERT(chain && "texture levels: malloc failed");
    if (src_w == w && src_h == h) {
        memcpy(chain, source, (size_t)w * h * 3U * sizeof(float));
    } else {
        resize_level(source, src_w, src_h, STBIR_TYPE_FLOAT, STBIR_RGB, chain, w, h, STBIR_TYPE_FLOAT, STBIR_RGB, NT_TEX_FILTER_BOX, opts);
    }
    float *prev = chain;
    for (uint32_t level = 1; level < level_count; level++) {
        const uint32_t pw = nt_texture_level_extent(w, level - 1);
        const uint32_t ph = nt_texture_level_extent(h, level - 1);
        float *dst = prev + ((size_t)pw * ph * 3U);
        resize_level(prev, pw, ph, STBIR_TYPE_FLOAT, STBIR_RGB, dst, nt_texture_level_extent(w, level), nt_texture_level_extent(h, level), STBIR_TYPE_FLOAT, STBIR_RGB, NT_TEX_FILTER_BOX, opts);
        prev = dst;
    }
    return chain;
}

static void pack_rgb9e5_texels(const float *rgb, size_t texels, uint8_t *dst) {
    for (size_t i = 0; i < texels; i++) {
        const uint32_t packed = pack_rgb9e5(&rgb[i * 3]);
        memcpy(dst + (i * sizeof(packed)), &packed, sizeof(packed));
    }
}

static NtTextureAssetHeader texture_header(const nt_tex_opts_t *opts, uint32_t width, uint32_t height, uint32_t level_count, size_t data_size) {
    NT_BUILD_ASSERT(data_size <= UINT32_MAX - sizeof(NtTextureAssetHeader) && "texture encode: levels overflow the 32-bit TTEX data_size");
    const bool compressed = opts->compress.codec != NT_BASISU_CODEC_NONE;
    return (NtTextureAssetHeader){
        .magic = NT_TEXTURE_MAGIC,
        .version = NT_TEXTURE_VERSION,
        .format = (uint16_t)opts->format,
        .width = width,
        .height = height,
        .mip_count = (uint16_t)level_count,
        .compression = (uint8_t)(compressed ? NT_TEXTURE_COMPRESSION_BASIS : NT_TEXTURE_COMPRESSION_NONE),
        .flags = (uint8_t)((opts->process.alpha == NT_TEX_ALPHA_PREMULTIPLIED ? NT_TEXTURE_FLAG_PREMULTIPLIED : 0U) | (opts->mips == NT_TEX_MIPS_RUNTIME ? NT_TEXTURE_FLAG_GEN_MIPMAPS : 0U)),
        .default_min_filter = (uint8_t)opts->filter_min,
        .default_mag_filter = (uint8_t)opts->filter_mag,
        .default_wrap_u = (uint8_t)opts->wrap_u,
        .default_wrap_v = (uint8_t)opts->wrap_v,
        .data_size = (uint32_t)data_size,
    };
}

/* --- Encode: source pixels -> independent buffer (thread-safe, no shared state) --- */

void nt_builder_encode_texture_to_buf(const void *pixels, uint32_t width, uint32_t height, const nt_tex_opts_t *opts, uint32_t encode_threads, uint8_t **out_data, uint32_t *out_size) {
    const bool compressed = opts->compress.codec != NT_BASISU_CODEC_NONE;
    uint32_t w = 0;
    uint32_t h = 0;
    nt_builder_texture_target_extent(width, height, opts->max_size, &w, &h);
    const uint32_t level_count = opts->mips == NT_TEX_MIPS_BAKED ? nt_texture_full_chain_levels(w, h) : 1;
    const size_t texels = chain_bytes(w, h, level_count, 1);

    float *hdr = NULL;
    uint8_t *levels = NULL;
    nt_basisu_encode_result_t enc = {0};
    size_t data_size = 0;
    if (opts->format == NT_TEXTURE_FORMAT_RGB9E5) {
        hdr = build_hdr_levels((const float *)pixels, width, height, w, h, level_count, opts);
        data_size = texels * sizeof(uint32_t);
    } else {
        levels = build_ldr_levels((const uint8_t *)pixels, width, height, w, h, level_count, opts);
        if (compressed) {
            const uint8_t *level_ptrs[32];
            const uint8_t *level = levels;
            for (uint32_t l = 0; l < level_count; l++) {
                level_ptrs[l] = level;
                level += (size_t)nt_texture_level_extent(w, l) * nt_texture_level_extent(h, l) * 4U;
            }
            enc = nt_basisu_encode(encode_threads, level_ptrs, level_count, w, h, opts->format, &opts->compress, opts->process.rgb == NT_TEX_RGB_COLOR);
            data_size = enc.size;
        } else {
            data_size = texels * nt_texture_bpp(opts->format);
        }
    }

    const NtTextureAssetHeader tex_hdr = texture_header(opts, w, h, level_count, data_size);
    const uint32_t total_asset_size = (uint32_t)sizeof(tex_hdr) + tex_hdr.data_size;
    uint8_t *buf = (uint8_t *)malloc(total_asset_size);
    NT_BUILD_ASSERT(buf && "texture encode: malloc failed");
    memcpy(buf, &tex_hdr, sizeof(tex_hdr));
    uint8_t *dst = buf + sizeof(tex_hdr);
    if (hdr) {
        pack_rgb9e5_texels(hdr, texels, dst);
    } else if (compressed) {
        memcpy(dst, enc.data, enc.size);
    } else if (nt_texture_bpp(opts->format) == 4) {
        memcpy(dst, levels, texels * 4U);
    } else {
        strip_channels(levels, (uint32_t)texels, nt_texture_bpp(opts->format), dst);
    }

    nt_basisu_encode_free(&enc);
    free(hdr);
    free(levels);
    *out_data = buf;
    *out_size = total_asset_size;
}
