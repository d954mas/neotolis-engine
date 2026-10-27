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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- assert expansions inflate the count
nt_texture_pixel_format_t nt_builder_assert_texture_opts(const nt_tex_opts_t *opts) {
    nt_tex_opts_t resolved = opts ? *opts : nt_tex_opts_defaults();
    nt_texture_pixel_format_t format = resolved.format ? resolved.format : NT_TEXTURE_FORMAT_RGBA8;

    NT_BUILD_ASSERT(nt_texture_pixel_format_valid(format) && "texture opts: invalid format");
    NT_BUILD_ASSERT((!resolved.premultiplied || format == NT_TEXTURE_FORMAT_RGBA8) && "texture opts: premultiplied=true requires RGBA8");
    NT_BUILD_ASSERT((unsigned)resolved.filter_min <= (unsigned)NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR && "texture opts: filter_min out of range");
    NT_BUILD_ASSERT((unsigned)resolved.filter_mag <= (unsigned)NT_TEXTURE_DEFAULT_FILTER_LINEAR && "texture opts: filter_mag must be NEAREST or LINEAR");
    NT_BUILD_ASSERT((unsigned)resolved.wrap_u <= (unsigned)NT_TEXTURE_DEFAULT_WRAP_MIRRORED_REPEAT && "texture opts: wrap_u out of range");
    NT_BUILD_ASSERT((unsigned)resolved.wrap_v <= (unsigned)NT_TEXTURE_DEFAULT_WRAP_MIRRORED_REPEAT && "texture opts: wrap_v out of range");

    const nt_basisu_encode_opts_t *compress_opts = &resolved.compress;
    bool compressed = compress_opts->codec != NT_BASISU_CODEC_NONE;
    bool filter_min_uses_mips = resolved.filter_min >= NT_TEXTURE_DEFAULT_FILTER_NEAREST_MIPMAP_NEAREST;
    NT_BUILD_ASSERT((compressed || format != NT_TEXTURE_FORMAT_SRGB8 || (!resolved.gen_mipmaps && !filter_min_uses_mips)) &&
                    "texture opts: RAW SRGB8 cannot generate runtime mips -- use SRGB8_ALPHA8, or gen_mipmaps=false with a non-mip filter_min");
    NT_BUILD_ASSERT((compressed || !filter_min_uses_mips || resolved.gen_mipmaps) && "texture opts: RAW mipmap filter requires gen_mipmaps=true");

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

/* HDR filters have no negative lobes: a ringing filter would make negative colors. */
static float *resize_hdr(const float *rgb, uint32_t w, uint32_t h, uint32_t out_w, uint32_t out_h) {
    float *resized = (float *)malloc((size_t)out_w * out_h * 3U * sizeof(float));
    NT_BUILD_ASSERT(resized && "texture resize: malloc failed");
    const void *written = stbir_resize(rgb, (int)w, (int)h, 0, resized, (int)out_w, (int)out_h, 0, STBIR_RGB, STBIR_TYPE_FLOAT, STBIR_EDGE_CLAMP, STBIR_FILTER_BOX);
    NT_BUILD_ASSERT(written && "texture resize: stbir_resize failed");
    return resized;
}

/* Resize source pixels to fit within max_size, preserving aspect ratio.
 * Returns the resized buffer (caller frees), or NULL when no resize is needed.
 * *out_w and *out_h receive the final size either way. */
static void *resize_if_needed(const void *pixels, nt_texture_pixel_format_t format, uint32_t w, uint32_t h, uint32_t max_size, uint32_t *out_w, uint32_t *out_h) {
    if (max_size == 0 || (w <= max_size && h <= max_size)) {
        *out_w = w;
        *out_h = h;
        return NULL;
    }
    if (w >= h) {
        *out_w = max_size;
        *out_h = h * max_size / w;
    } else {
        *out_h = max_size;
        *out_w = w * max_size / h;
    }
    if (*out_w < 1) {
        *out_w = 1;
    }
    if (*out_h < 1) {
        *out_h = 1;
    }
    if (format == NT_TEXTURE_FORMAT_RGB9E5) {
        return resize_hdr((const float *)pixels, w, h, *out_w, *out_h);
    }
    unsigned char *resized = (unsigned char *)malloc((size_t)*out_w * *out_h * 4);
    NT_BUILD_ASSERT(resized && "texture resize: malloc failed");
    const unsigned char *written = stbir_resize_uint8_linear((const unsigned char *)pixels, (int)w, (int)h, 0, resized, (int)*out_w, (int)*out_h, 0, STBIR_RGBA);
    NT_BUILD_ASSERT(written && "texture resize: stbir_resize_uint8_linear failed");
    return resized;
}

/* Strip RGBA8 source pixels to target channel count (1..3) */
static uint8_t *strip_channels(const uint8_t *rgba, uint32_t pixel_count, uint32_t target_channels) {
    NT_BUILD_ASSERT(pixel_count > 0 && target_channels > 0 && target_channels < 4 && "strip_channels: invalid args");
    uint8_t *out = (uint8_t *)malloc((size_t)pixel_count * target_channels);
    NT_BUILD_ASSERT(out && "texture strip_channels: malloc failed");
    for (uint32_t i = 0; i < pixel_count; i++) {
        for (uint32_t c = 0; c < target_channels; c++) {
            out[(i * target_channels) + c] = rgba[(i * 4) + c];
        }
    }
    return out;
}

/* RGB' = round(RGB * A / 255) in a copy, before strip or Basis encode, so block
 * compression spends no bits on invisible RGB and filtered edges get no dark fringes. */
static uint8_t *premultiply_rgba_copy(const uint8_t *rgba, uint32_t pixel_count) {
    uint8_t *out = (uint8_t *)malloc((size_t)pixel_count * 4);
    NT_BUILD_ASSERT(out && "texture premultiply: malloc failed");
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint32_t a = rgba[(i * 4) + 3];
        /* Round-to-nearest: (x * a + 127) / 255. For a=0 result is 0,
         * for a=255 result equals x (lossless for fully opaque). */
        out[(i * 4) + 0] = (uint8_t)(((uint32_t)rgba[(i * 4) + 0] * a + 127U) / 255U);
        out[(i * 4) + 1] = (uint8_t)(((uint32_t)rgba[(i * 4) + 1] * a + 127U) / 255U);
        out[(i * 4) + 2] = (uint8_t)(((uint32_t)rgba[(i * 4) + 2] * a + 127U) / 255U);
        out[(i * 4) + 3] = (uint8_t)a;
    }
    return out;
}

/* --- Decode: image data -> source pixels (eager, called from add_*) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- assert expansions inflate the count
void *nt_builder_decode_texture(const uint8_t *src_data, uint32_t src_size, nt_texture_pixel_format_t format, uint32_t max_size, uint32_t *out_w, uint32_t *out_h) {
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

    void *resized = resize_if_needed(pixels, format, (uint32_t)w, (uint32_t)h, max_size, out_w, out_h);
    if (resized) {
        stbi_image_free(pixels);
        pixels = resized;
    }
    /* stbi allocates with malloc, so the caller frees either buffer with free. */
    return pixels;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- assert expansions inflate the count
void *nt_builder_decode_texture_raw(const void *pixels, uint32_t width, uint32_t height, nt_texture_pixel_format_t format, uint32_t max_size, uint32_t *out_w, uint32_t *out_h) {
    NT_BUILD_ASSERT(pixels && width != 0 && height != 0 && out_w && out_h && "texture raw: invalid args");
    if (width > NT_BUILD_MAX_TEXTURE_SIZE || height > NT_BUILD_MAX_TEXTURE_SIZE) {
        NT_BUILD_FAIL("texture raw: image exceeds NT_BUILD_MAX_TEXTURE_SIZE", "texture raw: %ux%u exceeds max %u", width, height, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
    }
    if (format == NT_TEXTURE_FORMAT_RGB9E5) {
        assert_hdr_range((const float *)pixels, width * height);
    }

    void *result = resize_if_needed(pixels, format, width, height, max_size, out_w, out_h);
    if (!result) {
        /* The caller owns the result, so an unresized source is copied. */
        const uint32_t size = nt_builder_texture_source_size(format, width, height);
        result = malloc(size);
        NT_BUILD_ASSERT(result && "texture raw: malloc failed");
        memcpy(result, pixels, size);
    }
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

/* Level 0 alone, or the full halved chain back to back. RGB9E5 is not color-renderable,
 * so the runtime cannot generate its mips; each level is box-filtered from the previous
 * float level, never from packed RGB9E5. */
static uint32_t *encode_rgb9e5_chain(const float *rgb, uint32_t width, uint32_t height, bool full_chain, uint16_t *out_levels, uint32_t *out_size) {
    const uint32_t level_count = full_chain ? nt_texture_full_chain_levels(width, height) : 1;
    uint64_t total = nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGB9E5, nt_texture_level_extent(width, 0), nt_texture_level_extent(height, 0));
    for (uint32_t level = 1; level < level_count; level++) {
        total += nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGB9E5, nt_texture_level_extent(width, level), nt_texture_level_extent(height, level));
    }
    NT_BUILD_ASSERT(total <= UINT32_MAX - sizeof(NtTextureAssetHeader) && "texture encode: RGB9E5 chain exceeds 4 GiB");
    uint32_t *chain = (uint32_t *)malloc((size_t)total);
    NT_BUILD_ASSERT(chain && "texture encode: malloc failed");

    const float *level_rgb = rgb;
    float *owned = NULL;
    uint32_t *dst = chain;
    for (uint32_t level = 0; level < level_count; level++) {
        const uint32_t w = nt_texture_level_extent(width, level);
        const uint32_t h = nt_texture_level_extent(height, level);
        if (level > 0) {
            float *next = resize_hdr(level_rgb, nt_texture_level_extent(width, level - 1), nt_texture_level_extent(height, level - 1), w, h);
            free(owned);
            owned = next;
            level_rgb = next;
        }
        for (uint32_t i = 0; i < w * h; i++) {
            dst[i] = pack_rgb9e5(&level_rgb[(size_t)i * 3]);
        }
        dst += (size_t)w * h;
    }
    free(owned);
    *out_levels = (uint16_t)level_count;
    *out_size = (uint32_t)total;
    return chain;
}

/* --- Encode: source pixels -> independent buffer (thread-safe, no shared state) --- */

void nt_builder_encode_texture_to_buf(const void *pixels, uint32_t width, uint32_t height, const nt_tex_opts_t *opts, uint32_t encode_threads, uint8_t **out_data, uint32_t *out_size) {
    uint32_t pixel_count = width * height;
    uint32_t bpp = nt_texture_bpp(opts->format);
    bool compressed = opts->compress.codec != NT_BASISU_CODEC_NONE;
    bool hdr = opts->format == NT_TEXTURE_FORMAT_RGB9E5;

    uint8_t *premul_buf = NULL;
    const uint8_t *source = (const uint8_t *)pixels;
    if (opts->premultiplied) {
        premul_buf = premultiply_rgba_copy(source, pixel_count);
        source = premul_buf;
    }

    const uint8_t *payload = source;
    uint32_t data_size = pixel_count * bpp;
    uint16_t mip_count = 1;
    uint8_t *stripped = NULL;
    uint32_t *hdr_chain = NULL;
    nt_basisu_encode_result_t enc = {0};
    if (compressed) {
        enc = nt_basisu_encode(encode_threads, source, width, height, opts->format, &opts->compress);
        NT_BUILD_ASSERT(enc.mip_count == nt_texture_full_chain_levels(width, height) && "texture encode: Basis requires a full mip chain");
        payload = enc.data;
        data_size = enc.size;
        mip_count = (uint16_t)enc.mip_count;
    } else if (hdr) {
        hdr_chain = encode_rgb9e5_chain((const float *)pixels, width, height, opts->gen_mipmaps, &mip_count, &data_size);
        payload = (const uint8_t *)hdr_chain;
    } else if (bpp < 4) {
        stripped = strip_channels(source, pixel_count, bpp);
        payload = stripped;
    }

    NtTextureAssetHeader tex_hdr = {0};
    tex_hdr.magic = NT_TEXTURE_MAGIC;
    tex_hdr.version = NT_TEXTURE_VERSION;
    tex_hdr.format = (uint16_t)opts->format;
    tex_hdr.width = width;
    tex_hdr.height = height;
    tex_hdr.mip_count = mip_count;
    tex_hdr.compression = (uint8_t)(compressed ? NT_TEXTURE_COMPRESSION_BASIS : NT_TEXTURE_COMPRESSION_RAW);
    tex_hdr.flags = opts->premultiplied ? (uint8_t)NT_TEXTURE_FLAG_PREMULTIPLIED : 0;
    if (!compressed && !hdr && opts->gen_mipmaps) {
        tex_hdr.flags |= (uint8_t)NT_TEXTURE_FLAG_GEN_MIPMAPS;
    }
    tex_hdr.default_min_filter = (uint8_t)opts->filter_min;
    tex_hdr.default_mag_filter = (uint8_t)opts->filter_mag;
    tex_hdr.default_wrap_u = (uint8_t)opts->wrap_u;
    tex_hdr.default_wrap_v = (uint8_t)opts->wrap_v;
    tex_hdr.data_size = data_size;

    uint32_t total_asset_size = (uint32_t)sizeof(tex_hdr) + data_size;
    uint8_t *buf = (uint8_t *)malloc(total_asset_size);
    NT_BUILD_ASSERT(buf && "texture encode: malloc failed");
    memcpy(buf, &tex_hdr, sizeof(tex_hdr));
    memcpy(buf + sizeof(tex_hdr), payload, data_size);

    nt_basisu_encode_free(&enc);
    free(hdr_chain);
    free(stripped);
    free(premul_buf);
    *out_data = buf;
    *out_size = total_asset_size;
}
