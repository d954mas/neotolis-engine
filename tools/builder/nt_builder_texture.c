/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_basisu_encoder.h"
#include "nt_texture_format.h"
#include "stb_image.h"
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
    NT_BUILD_ASSERT(resolved.max_size % 4 == 0 && "texture opts: max_size must be 0 or a multiple of 4");
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

/* The shorter side scaled with the longer one to max_size, rounded down to the 4x4 block (at least one block). */
static uint32_t scaled_short_side(uint32_t short_side, uint32_t long_side, uint32_t max_size) {
    const uint32_t side = (short_side * max_size / long_side) & ~3U;
    return side < 4 ? 4 : side;
}

void nt_builder_texture_target_extent(uint32_t width, uint32_t height, uint32_t max_size, uint32_t *out_w, uint32_t *out_h) {
    if (max_size == 0 || (width <= max_size && height <= max_size)) {
        *out_w = width;
        *out_h = height;
        return;
    }
    *out_w = width >= height ? max_size : scaled_short_side(width, height, max_size);
    *out_h = width >= height ? scaled_short_side(height, width, max_size) : max_size;
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
uint32_t nt_builder_texture_cutoff_byte(float cutoff) {
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
 * one closest to level 0's (covered0 of texels0). Candidates split the level at
 * every byte t, or cover nothing; a tie keeps the current coverage, then the
 * larger t. The scale puts threshold - 0.5 midway between the highest excluded
 * and the lowest included alpha, so block compression has margin on both sides. */
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
    uint32_t included = best; /* lowest alpha present at or above best, 256 for none */
    while (included < 256 && counts[included] == counts[included + 1]) {
        included++;
    }
    uint32_t excluded = best - 1; /* highest alpha present below best; 0 always maps to 0 */
    while (excluded > 0 && counts[excluded] == counts[excluded + 1]) {
        excluded--;
    }
    const float scale = ((float)threshold - 0.5F) / ((float)(included + excluded) * 0.5F);
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

// #region resampler
/* A separable downscaler: per axis a table of taps, edges by the sampler wrap,
 * then one output row at a time (input rows weighted into one row, then across).
 * Rows are converted to linear float RGBA as they are read. */

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
static double kaiser(double x) {
    const double t = fabs(x);
    if (t >= 3.0) {
        return 0.0;
    }
    const double att = 40.0;
    const double alpha = exp(log(0.58417 * (att - 20.96)) * 0.4) + (0.07886 * (att - 20.96));
    const double ratio = t / 3.0;
    const double window = bessel0(alpha * sqrt(1.0 - (ratio * ratio))) / bessel0(alpha);
    const double pi_t = 3.14159265358979323846 * t;
    return (t < 1e-6 ? 1.0 : sin(pi_t) / pi_t) * window;
}

/* Mitchell-Netravali with B = C = 1/3, support 2. */
static double mitchell(double x) {
    const double t = fabs(x);
    if (t < 1.0) {
        return (16.0 + (t * t * ((21.0 * t) - 36.0))) / 18.0;
    }
    if (t < 2.0) {
        return (32.0 + (t * (-60.0 + (t * (36.0 - (7.0 * t)))))) / 18.0;
    }
    return 0.0;
}

/* Texel of tap `i` on an axis of `n` texels, as the GL sampler wraps it. */
static uint32_t wrap_tap(int64_t i, uint32_t n, nt_texture_default_wrap_t wrap) {
    const int64_t len = n;
    if (wrap == NT_TEXTURE_DEFAULT_WRAP_REPEAT) {
        return (uint32_t)(((i % len) + len) % len);
    }
    if (wrap == NT_TEXTURE_DEFAULT_WRAP_MIRRORED_REPEAT) {
        const int64_t m = ((i % (2 * len)) + (2 * len)) % (2 * len);
        return (uint32_t)(m < len ? m : (2 * len) - 1 - m);
    }
    if (i < 0) {
        return 0;
    }
    return (uint32_t)(i >= len ? len - 1 : i);
}

/* Taps of one axis: output texel o reads index[o * taps + k] with weight[...]. */
typedef struct {
    uint32_t taps;
    uint32_t *index;
    float *weight;
} axis_taps_t;

/* Weight of input texel `i` for an output texel centred at `center` (input units).
 * BOX weighs the texel by its overlap with the output footprint; the other filters
 * sample their kernel, stretched by the ratio, at the texel centre. */
static double tap_weight(nt_tex_filter_t filter, double i, double center, double scale) {
    if (filter == NT_TEX_FILTER_BOX) {
        return fmax(0.0, fmin(i + 1.0, center + (0.5 * scale)) - fmax(i, center - (0.5 * scale)));
    }
    const double x = (i + 0.5 - center) / scale;
    return filter == NT_TEX_FILTER_MITCHELL ? mitchell(x) : kaiser(x);
}

static axis_taps_t axis_taps(uint32_t in, uint32_t out, nt_tex_filter_t filter, nt_texture_default_wrap_t wrap) {
    NT_BUILD_ASSERT(out >= 1 && "texture resize: empty axis");
    static const double supports[3] = {[NT_TEX_FILTER_BOX] = 0.5, [NT_TEX_FILTER_MITCHELL] = 2.0, [NT_TEX_FILTER_KAISER] = 3.0};
    const double scale = (double)in / out;
    /* Kernels widen with a downscale; an upscale (a short side rounded up to the 4x4 block) keeps them at one texel. */
    const double stretch = fmax(scale, 1.0);
    const double support = supports[filter];
    axis_taps_t a = {.taps = (uint32_t)ceil(2.0 * support * stretch) + 2};
    a.index = (uint32_t *)malloc((size_t)out * a.taps * sizeof(uint32_t));
    a.weight = (float *)malloc((size_t)out * a.taps * sizeof(float));
    NT_BUILD_ASSERT(a.index && a.weight && "texture resize: malloc failed");
    for (uint32_t o = 0; o < out; o++) {
        const double center = (o + 0.5) * scale;
        const int64_t first = (int64_t)floor(center - (support * stretch));
        double sum = 0.0;
        for (uint32_t k = 0; k < a.taps; k++) {
            const double w = tap_weight(filter, (double)(first + k), center, stretch);
            a.weight[((size_t)o * a.taps) + k] = (float)w;
            sum += w;
            a.index[((size_t)o * a.taps) + k] = wrap_tap(first + k, in, wrap);
        }
        for (uint32_t k = 0; k < a.taps; k++) {
            a.weight[((size_t)o * a.taps) + k] = (float)((double)a.weight[((size_t)o * a.taps) + k] / sum);
        }
    }
    return a;
}

/* A resample input: RGBA bytes (read through `lin`, optionally premultiplied) or
 * float RGBA, or float RGB for HDR (alpha 1). */
typedef struct {
    const void *pixels;
    uint32_t w;
    uint32_t h;
    const float *lin; /* byte -> linear value; NULL for float input */
    uint32_t channels;
    bool premultiply;
} resample_src_t;

static void load_row(const resample_src_t *s, uint32_t y, float *row) {
    if (s->lin == NULL) {
        const float *f = (const float *)s->pixels + ((size_t)y * s->w * s->channels);
        for (uint32_t x = 0; x < s->w; x++) {
            for (uint32_t c = 0; c < 4; c++) {
                row[(x * 4U) + c] = c < s->channels ? f[(x * s->channels) + c] : 1.0F;
            }
        }
        return;
    }
    const uint8_t *p = (const uint8_t *)s->pixels + ((size_t)y * s->w * 4U);
    for (uint32_t x = 0; x < s->w; x++) {
        const float a = (float)p[(x * 4U) + 3U] / 255.0F;
        for (uint32_t c = 0; c < 3; c++) {
            row[(x * 4U) + c] = s->lin[p[(x * 4U) + c]] * (s->premultiply ? a : 1.0F);
        }
        row[(x * 4U) + 3U] = a;
    }
}

/* Downscales `s` into ow x oh float RGBA. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one separable filter, rows then columns
static void resample(const resample_src_t *s, float *out, uint32_t ow, uint32_t oh, nt_tex_filter_t filter, const nt_tex_opts_t *opts) {
    axis_taps_t ax = axis_taps(s->w, ow, filter, opts->wrap_u);
    axis_taps_t ay = axis_taps(s->h, oh, filter, opts->wrap_v);
    float *row = (float *)malloc((size_t)s->w * 4U * sizeof(float));
    float *acc = (float *)malloc((size_t)s->w * 4U * sizeof(float));
    NT_BUILD_ASSERT(row && acc && "texture resize: malloc failed");
    for (uint32_t oy = 0; oy < oh; oy++) {
        memset(acc, 0, (size_t)s->w * 4U * sizeof(float));
        for (uint32_t k = 0; k < ay.taps; k++) {
            const float wy = ay.weight[((size_t)oy * ay.taps) + k];
            if (wy == 0.0F) {
                continue;
            }
            load_row(s, ay.index[((size_t)oy * ay.taps) + k], row);
            for (size_t i = 0; i < (size_t)s->w * 4U; i++) {
                acc[i] += wy * row[i];
            }
        }
        for (uint32_t ox = 0; ox < ow; ox++) {
            float *dst = &out[(((size_t)oy * ow) + ox) * 4U];
            dst[0] = dst[1] = dst[2] = dst[3] = 0.0F;
            for (uint32_t k = 0; k < ax.taps; k++) {
                const float wx = ax.weight[((size_t)ox * ax.taps) + k];
                const float *src = &acc[(size_t)ax.index[((size_t)ox * ax.taps) + k] * 4U];
                for (uint32_t c = 0; c < 4; c++) {
                    dst[c] += wx * src[c];
                }
            }
        }
    }
    free(row);
    free(acc);
    free(ax.index);
    free(ax.weight);
    free(ay.index);
    free(ay.weight);
}
// #endregion

static float srgb_to_linear(float v) { return v <= 0.04045F ? v / 12.92F : powf((v + 0.055F) / 1.055F, 2.4F); }

static float linear_to_srgb(float v) { return v <= 0.0031308F ? v * 12.92F : (1.055F * powf(v, 1.0F / 2.4F)) - 0.055F; }

/* A linear value as a stored byte; ringing is clamped here. */
static uint8_t encode_unit(float v, bool srgb) {
    const float clamped = fminf(fmaxf(v, 0.0F), 1.0F);
    return (uint8_t)floorf(((srgb ? linear_to_srgb(clamped) : clamped) * 255.0F) + 0.5F);
}

/* A stored straight-alpha level from a float linear core. A premultiplied core is
 * divided by its alpha only here, so a near-zero alpha never feeds the next level. */
static void quantize_core(const float *core, size_t texels, bool srgb, bool premultiplied, uint8_t *out) {
    for (size_t i = 0; i < texels; i++) {
        const float a = core[(i * 4U) + 3U];
        for (uint32_t c = 0; c < 3; c++) {
            float v = core[(i * 4U) + c];
            if (premultiplied) {
                v = a > 0.0F ? v / a : 0.0F;
            }
            out[(i * 4U) + c] = encode_unit(v, srgb);
        }
        out[(i * 4U) + 3U] = encode_unit(a, false);
    }
}

/* Encoded normals (c * 2 - 1) to unit vectors, encoded again. An average shorter
 * than about two byte steps (opposite normals cancelling; a byte cannot hold an
 * exact 0) becomes +Z. */
static void normalize_core(float *core, size_t texels) {
    for (size_t i = 0; i < texels; i++) {
        float *v = &core[i * 4U];
        float n[3];
        for (uint32_t c = 0; c < 3; c++) {
            n[c] = (v[c] * 2.0F) - 1.0F;
        }
        const float length = sqrtf((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
        if (length < 0.02F) {
            v[0] = v[1] = 0.5F;
            v[2] = 1.0F;
            continue;
        }
        for (uint32_t c = 0; c < 3; c++) {
            v[c] = (n[c] / length * 0.5F) + 0.5F;
        }
    }
}

/* RG8 normals store XY only, so Z is rebuilt for filtering and normalization. */
static void rebuild_normal_z(uint8_t *rgba, size_t texels) {
    for (size_t i = 0; i < texels; i++) {
        const float x = ((float)rgba[i * 4U] / 127.5F) - 1.0F;
        const float y = ((float)rgba[(i * 4U) + 1U] / 127.5F) - 1.0F;
        rgba[(i * 4U) + 2U] = encode_unit((sqrtf(fmaxf(0.0F, 1.0F - (x * x) - (y * y))) * 0.5F) + 0.5F, false);
    }
}

// #region dilation
typedef struct {
    float lin[256];      /* stored byte -> linear value */
    uint32_t hole_alpha; /* texels at or below it are holes: their filtered RGB is ringing divided by a tiny A */
    bool srgb;
    bool wrap_x;
    bool wrap_y;
} dilate_ctx_t;

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
        if (!n || n[3] <= dc->hole_alpha) {
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
            const float mean_a = px[3] <= dc->hole_alpha ? ring_mean(dc, rgba, w, h, x, y, rgb) : 0.0F;
            if (mean_a == 0.0F) {
                continue;
            }
            for (uint32_t c = 0; c < 3; c++) {
                px[c] = encode_unit(rgb[c], dc->srgb);
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
    return (float)(px[3] > p->dc->hole_alpha ? px[3] : p->ring_a[f]) / 255.0F;
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
            if (px[3] > p->dc->hole_alpha || p->ring_a[((size_t)y * w) + x] != 0) {
                continue;
            }
            float parent[3];
            tent_sample(p, 1, x, y, parent);
            for (uint32_t c = 0; c < 3; c++) {
                px[c] = encode_unit(parent[c], p->dc->srgb);
            }
        }
    }
}

/* Fills the RGB of every hole of one stored level: a ring pass, then push-pull
 * over alpha-weighted float sums (pull by ceil-halving, push by tent). The level
 * has a visible texel and a hole, so it has two texels or more. */
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
    NT_BUILD_ASSERT(total > 0 && "texture dilation: one texel is never a hole beside a visible one");
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
    free(ring_a);
}

/* Whether a stored level has a visible texel and whether it has a hole. */
static void scan_level(const uint8_t *rgba, size_t texels, uint32_t hole_alpha, bool *visible, bool *hole) {
    *visible = false;
    *hole = false;
    for (size_t i = 0; i < texels && !(*visible && *hole); i++) {
        const bool texel_visible = rgba[(i * 4U) + 3U] > hole_alpha;
        *visible = *visible || texel_visible;
        *hole = *hole || !texel_visible;
    }
}

/* A level with nothing visible takes the colour of a box filter of the previous
 * level (area weights, linear light) and keeps its own alpha. */
static void fill_empty_level(const dilate_ctx_t *dc, uint8_t *rgba, uint32_t w, uint32_t h, const uint8_t *prev, uint32_t pw, uint32_t ph, const nt_tex_opts_t *opts) {
    const size_t texels = (size_t)w * h;
    float *box = (float *)malloc(texels * 4U * sizeof(float));
    NT_BUILD_ASSERT(box && "texture dilation: malloc failed");
    const resample_src_t src = {.pixels = prev, .w = pw, .h = ph, .lin = dc->lin, .channels = 4};
    resample(&src, box, w, h, NT_TEX_FILTER_BOX, opts);
    for (size_t i = 0; i < texels; i++) {
        for (uint32_t c = 0; c < 3; c++) {
            rgba[(i * 4U) + c] = encode_unit(box[(i * 4U) + c], dc->srgb);
        }
    }
    free(box);
}
// #endregion

/* Stored RGBA8 levels of a w x h texture, back to back. Level 0 is the source
 * (opaque for a format without alpha), copied or resized with resize_filter.
 * Level n filters level n-1's core with mip_filter: level 0's float core (its bytes
 * when it is a copy), then float linear values, premultiplied when alpha weights, so rounding, sRGB encoding and
 * division by alpha never accumulate. The stored copies are then dilated and scaled
 * for coverage level by level, and premultiplied last. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one pass per pipeline stage
static uint8_t *build_ldr_levels(const uint8_t *source, uint32_t src_w, uint32_t src_h, uint32_t w, uint32_t h, uint32_t level_count, const nt_tex_opts_t *opts) {
    const nt_tex_process_t *process = &opts->process;
    const bool alpha = nt_texture_pixel_format_has_alpha(opts->format);
    const bool srgb = nt_texture_format_is_srgb(opts->format);
    const bool weighted = alpha && process->alpha != NT_TEX_ALPHA_DATA;
    const bool normal = process->rgb == NT_TEX_RGB_NORMAL;
    float lin[256];
    for (uint32_t i = 0; i < 256; i++) {
        lin[i] = srgb ? srgb_to_linear((float)i / 255.0F) : (float)i / 255.0F;
    }
    uint8_t *chain = (uint8_t *)malloc(chain_bytes(w, h, level_count, 4));
    NT_BUILD_ASSERT(chain && "texture levels: malloc failed");

    // #region level 0
    const size_t src_texels = (size_t)src_w * src_h;
    float *resized = NULL; /* a resized level 0's float core, which level 1 filters */
    uint8_t *rebuilt = NULL;
    if (normal && opts->format == NT_TEXTURE_FORMAT_RG8) {
        rebuilt = (uint8_t *)malloc(src_texels * 4U);
        NT_BUILD_ASSERT(rebuilt && "texture levels: malloc failed");
        memcpy(rebuilt, source, src_texels * 4U);
        rebuild_normal_z(rebuilt, src_texels);
        source = rebuilt;
    }
    if (src_w == w && src_h == h) {
        memcpy(chain, source, src_texels * 4U);
    } else {
        resized = (float *)malloc((size_t)w * h * 4U * sizeof(float));
        NT_BUILD_ASSERT(resized && "texture levels: malloc failed");
        const resample_src_t src = {.pixels = source, .w = src_w, .h = src_h, .lin = lin, .channels = 4, .premultiply = weighted};
        resample(&src, resized, w, h, process->resize_filter, opts);
        for (size_t i = 0; !alpha && i < (size_t)w * h; i++) {
            resized[(i * 4U) + 3U] = 1.0F;
        }
        if (normal) {
            normalize_core(resized, (size_t)w * h);
        }
        quantize_core(resized, (size_t)w * h, srgb, weighted, chain);
    }
    free(rebuilt);
    /* Without alpha nothing weights by it, so an opaque level 0 is all the chain needs. */
    for (size_t i = 0; !alpha && i < (size_t)w * h; i++) {
        chain[(i * 4U) + 3U] = 255;
    }
    // #endregion

    // #region chain
    if (level_count > 1) {
        /* Odd levels go to the first buffer, even ones to the second. */
        float *cores[2] = {(float *)malloc((size_t)nt_texture_level_extent(w, 1) * nt_texture_level_extent(h, 1) * 4U * sizeof(float)),
                           (float *)malloc((size_t)nt_texture_level_extent(w, 2) * nt_texture_level_extent(h, 2) * 4U * sizeof(float))};
        NT_BUILD_ASSERT(cores[0] && cores[1] && "texture levels: malloc failed");
        uint8_t *stored = chain;
        for (uint32_t level = 1; level < level_count; level++) {
            const uint32_t pw = nt_texture_level_extent(w, level - 1);
            const uint32_t ph = nt_texture_level_extent(h, level - 1);
            const uint32_t lw = nt_texture_level_extent(w, level);
            const uint32_t lh = nt_texture_level_extent(h, level);
            float *core = cores[(level - 1) & 1U];
            resample_src_t src = {.pixels = cores[level & 1U], .w = pw, .h = ph, .channels = 4};
            if (level == 1) {
                src = resized ? (resample_src_t){.pixels = resized, .w = pw, .h = ph, .channels = 4}
                              : (resample_src_t){.pixels = chain, .w = pw, .h = ph, .lin = lin, .channels = 4, .premultiply = weighted};
            }
            resample(&src, core, lw, lh, process->mip_filter, opts);
            if (normal) {
                normalize_core(core, (size_t)lw * lh);
            }
            stored += (size_t)pw * ph * 4U;
            quantize_core(core, (size_t)lw * lh, srgb, weighted, stored);
        }
        free(cores[0]);
        free(cores[1]);
    }
    free(resized);
    // #endregion

    // #region stored copies
    const uint32_t threshold = process->alpha_cutoff > 0.0F ? nt_builder_texture_cutoff_byte(process->alpha_cutoff) : 0;
    /* A texel the alpha test passes is never a hole. */
    dilate_ctx_t dc = {.hole_alpha = threshold > 0 && threshold <= 8 ? threshold - 1 : 8,
                       .srgb = srgb,
                       .wrap_x = opts->wrap_u == NT_TEXTURE_DEFAULT_WRAP_REPEAT,
                       .wrap_y = opts->wrap_v == NT_TEXTURE_DEFAULT_WRAP_REPEAT};
    memcpy(dc.lin, lin, sizeof(lin));
    /* Premultiplied colour needs a fill only where coverage scaling lifts it into view. */
    bool dilate = weighted && (process->alpha == NT_TEX_ALPHA_STRAIGHT || threshold > 0);
    uint64_t counts0[257] = {0};
    if (threshold > 0) {
        alpha_counts(chain, (size_t)w * h, counts0);
    }
    uint8_t *level_px = chain;
    const uint8_t *prev = NULL;
    for (uint32_t level = 0; level < level_count; level++) {
        const uint32_t lw = nt_texture_level_extent(w, level);
        const uint32_t lh = nt_texture_level_extent(h, level);
        const size_t texels = (size_t)lw * lh;
        /* Dilation first: holes are found on the filtered alpha, before coverage
           scaling lifts some of them into view. A source with nothing visible is
           stored as it is. */
        if (dilate) {
            bool visible = false;
            bool hole = false;
            scan_level(level_px, texels, dc.hole_alpha, &visible, &hole);
            if (!visible && level == 0) {
                dilate = false;
            } else if (!visible) {
                fill_empty_level(&dc, level_px, lw, lh, prev, nt_texture_level_extent(w, level - 1), nt_texture_level_extent(h, level - 1), opts);
            } else if (hole) {
                dilate_level(&dc, level_px, lw, lh);
            }
        }
        if (threshold > 0 && level > 0) {
            preserve_coverage(level_px, texels, threshold, counts0[threshold], (size_t)w * h);
        }
        prev = level_px;
        level_px += texels * 4U;
    }
    /* Last, over the whole chain: an empty level fills from the straight level above. */
    if (process->alpha == NT_TEX_ALPHA_PREMULTIPLIED) {
        for (uint8_t *px = chain; px < level_px; px += 4) {
            for (uint32_t c = 0; c < 3; c++) {
                px[c] = (uint8_t)((((uint32_t)px[c] * px[3]) + 127U) / 255U);
            }
        }
    }
    // #endregion
    return chain;
}

static void pack_rgb9e5_texels(const float *pixels, size_t texels, uint32_t channels, uint8_t *dst) {
    for (size_t i = 0; i < texels; i++) {
        const uint32_t packed = pack_rgb9e5(&pixels[i * channels]);
        memcpy(dst + (i * sizeof(packed)), &packed, sizeof(packed));
    }
}

/* RGB9E5 levels packed into `dst` as they are built, each box filtered from the
 * previous float level (negative lobes would make negative colours). The source
 * serves as level 0 at its own size, so at most two float levels live at a time. */
static void encode_hdr_levels(const float *source, uint32_t src_w, uint32_t src_h, uint32_t w, uint32_t h, uint32_t level_count, const nt_tex_opts_t *opts, uint8_t *dst) {
    resample_src_t prev = {.pixels = source, .w = src_w, .h = src_h, .channels = 3};
    float *resized = NULL;
    if (src_w != w || src_h != h) {
        resized = (float *)malloc((size_t)w * h * 4U * sizeof(float));
        NT_BUILD_ASSERT(resized && "texture levels: malloc failed");
        resample(&prev, resized, w, h, NT_TEX_FILTER_BOX, opts);
        prev = (resample_src_t){.pixels = resized, .w = w, .h = h, .channels = 4};
    }
    pack_rgb9e5_texels((const float *)prev.pixels, (size_t)w * h, prev.channels, dst);
    dst += (size_t)w * h * sizeof(uint32_t);
    /* Odd levels go to the first buffer, even ones to the second. */
    float *cores[2] = {NULL, NULL};
    if (level_count > 1) {
        cores[0] = (float *)malloc((size_t)nt_texture_level_extent(w, 1) * nt_texture_level_extent(h, 1) * 4U * sizeof(float));
        cores[1] = (float *)malloc((size_t)nt_texture_level_extent(w, 2) * nt_texture_level_extent(h, 2) * 4U * sizeof(float));
        NT_BUILD_ASSERT(cores[0] && cores[1] && "texture levels: malloc failed");
    }
    for (uint32_t level = 1; level < level_count; level++) {
        const uint32_t lw = nt_texture_level_extent(w, level);
        const uint32_t lh = nt_texture_level_extent(h, level);
        float *core = cores[(level - 1) & 1U];
        resample(&prev, core, lw, lh, NT_TEX_FILTER_BOX, opts);
        pack_rgb9e5_texels(core, (size_t)lw * lh, 4, dst);
        dst += (size_t)lw * lh * sizeof(uint32_t);
        prev = (resample_src_t){.pixels = core, .w = lw, .h = lh, .channels = 4};
    }
    free(resized);
    free(cores[0]);
    free(cores[1]);
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

    const bool hdr = opts->format == NT_TEXTURE_FORMAT_RGB9E5;
    uint8_t *levels = NULL;
    nt_basisu_encode_result_t enc = {0};
    size_t data_size = 0;
    if (hdr) {
        data_size = texels * sizeof(uint32_t);
    } else {
        levels = build_ldr_levels((const uint8_t *)pixels, width, height, w, h, level_count, opts);
        if (compressed) {
            enc = nt_basisu_encode(encode_threads, levels, level_count, w, h, opts->format, &opts->compress, opts->process.rgb == NT_TEX_RGB_COLOR);
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
        encode_hdr_levels((const float *)pixels, width, height, w, h, level_count, opts, dst);
    } else if (compressed) {
        memcpy(dst, enc.data, enc.size);
    } else if (nt_texture_bpp(opts->format) == 4) {
        memcpy(dst, levels, texels * 4U);
    } else {
        strip_channels(levels, (uint32_t)texels, nt_texture_bpp(opts->format), dst);
    }

    nt_basisu_encode_free(&enc);
    free(levels);
    *out_data = buf;
    *out_size = total_asset_size;
}
