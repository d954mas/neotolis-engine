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

/* Resize RGBA pixels to fit within max_size, preserving aspect ratio.
 * Returns the resized buffer (caller frees), or NULL when no resize is needed.
 * *out_w and *out_h receive the final size either way. */
static unsigned char *resize_if_needed(const unsigned char *pixels, int w, int h, uint32_t max_size, int *out_w, int *out_h) {
    if (max_size == 0 || ((uint32_t)w <= max_size && (uint32_t)h <= max_size)) {
        *out_w = w;
        *out_h = h;
        return NULL;
    }
    if ((uint32_t)w >= (uint32_t)h) {
        *out_w = (int)max_size;
        *out_h = (int)((uint32_t)h * max_size / (uint32_t)w);
    } else {
        *out_h = (int)max_size;
        *out_w = (int)((uint32_t)w * max_size / (uint32_t)h);
    }
    if (*out_w < 1) {
        *out_w = 1;
    }
    if (*out_h < 1) {
        *out_h = 1;
    }
    unsigned char *resized = (unsigned char *)malloc((size_t)*out_w * (size_t)*out_h * 4);
    NT_BUILD_ASSERT(resized && "texture resize: malloc failed");
    const unsigned char *written = stbir_resize_uint8_linear(pixels, w, h, 0, resized, *out_w, *out_h, 0, STBIR_RGBA);
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

/* --- Decode: image data -> RGBA pixels (eager, called from add_*) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- assert expansions inflate the count
uint8_t *nt_builder_decode_texture(const uint8_t *src_data, uint32_t src_size, const nt_tex_opts_t *opts, uint32_t *out_w, uint32_t *out_h) {
    NT_BUILD_ASSERT(src_data && src_size != 0 && out_w && out_h && "texture decode: invalid args");
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(src_data, (int)src_size, &w, &h, &channels, 4);
    NT_BUILD_ASSERT(pixels && "texture decode: stbi_load_from_memory failed");
    if ((uint32_t)w > NT_BUILD_MAX_TEXTURE_SIZE || (uint32_t)h > NT_BUILD_MAX_TEXTURE_SIZE) {
        NT_BUILD_FAIL("texture decode: image exceeds NT_BUILD_MAX_TEXTURE_SIZE", "texture decode: %ux%u exceeds max %u", (uint32_t)w, (uint32_t)h, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
    }

    int rw = 0;
    int rh = 0;
    unsigned char *resized = resize_if_needed(pixels, w, h, opts ? opts->max_size : 0, &rw, &rh);
    if (resized) {
        stbi_image_free(pixels);
        pixels = resized;
    }
    *out_w = (uint32_t)rw;
    *out_h = (uint32_t)rh;
    /* stbi allocates with malloc, so the caller frees either buffer with free. */
    return (uint8_t *)pixels;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- assert expansions inflate the count
uint8_t *nt_builder_decode_texture_raw(const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const nt_tex_opts_t *opts, uint32_t *out_w, uint32_t *out_h) {
    NT_BUILD_ASSERT(rgba_pixels && width != 0 && height != 0 && out_w && out_h && "texture raw: invalid args");
    if (width > NT_BUILD_MAX_TEXTURE_SIZE || height > NT_BUILD_MAX_TEXTURE_SIZE) {
        NT_BUILD_FAIL("texture raw: image exceeds NT_BUILD_MAX_TEXTURE_SIZE", "texture raw: %ux%u exceeds max %u", width, height, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
    }

    int rw = 0;
    int rh = 0;
    uint8_t *pixels = resize_if_needed(rgba_pixels, (int)width, (int)height, opts ? opts->max_size : 0, &rw, &rh);
    if (!pixels) {
        /* The caller owns the result, so an unresized source is copied. */
        pixels = (uint8_t *)malloc((size_t)width * height * 4);
        NT_BUILD_ASSERT(pixels && "texture raw: malloc failed");
        memcpy(pixels, rgba_pixels, (size_t)width * height * 4);
    }
    *out_w = (uint32_t)rw;
    *out_h = (uint32_t)rh;
    return pixels;
}

/* --- Encode: RGBA pixels -> independent buffer (thread-safe, no shared state) --- */

void nt_builder_encode_texture_to_buf(const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const nt_tex_opts_t *opts, uint32_t encode_threads, uint8_t **out_data, uint32_t *out_size) {
    uint32_t pixel_count = width * height;
    uint32_t bpp = nt_texture_bpp(opts->format);
    bool compressed = opts->compress.codec != NT_BASISU_CODEC_NONE;

    uint8_t *premul_buf = NULL;
    const uint8_t *source = rgba_pixels;
    if (opts->premultiplied) {
        premul_buf = premultiply_rgba_copy(rgba_pixels, pixel_count);
        source = premul_buf;
    }

    const uint8_t *payload = source;
    uint32_t data_size = pixel_count * bpp;
    uint16_t mip_count = 1;
    uint8_t *stripped = NULL;
    nt_basisu_encode_result_t enc = {0};
    if (compressed) {
        enc = nt_basisu_encode(encode_threads, source, width, height, opts->format, &opts->compress);
        NT_BUILD_ASSERT(enc.mip_count == nt_texture_full_chain_levels(width, height) && "texture encode: Basis requires a full mip chain");
        payload = enc.data;
        data_size = enc.size;
        mip_count = (uint16_t)enc.mip_count;
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
    if (!compressed && opts->gen_mipmaps) {
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
    free(stripped);
    free(premul_buf);
    *out_data = buf;
    *out_size = total_asset_size;
}
