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

/* Lazy encoder initialization */
static bool s_encoder_initialized = false;

/* No mapping needed — builder and runtime share nt_texture_pixel_format_t.
 * BPP lookup uses nt_texture_bpp() from nt_texture_format.h. */

/* Resize RGBA pixels to fit within max_size, preserving aspect ratio.
 * Returns resized buffer (caller frees) or NULL if no resize needed.
 * On resize, *out_w and *out_h are updated. */
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
    if (!resized) {
        return NULL;
    }
    stbir_resize_uint8_linear(pixels, w, h, 0, resized, *out_w, *out_h, 0, STBIR_RGBA);
    return resized;
}

/* Strip RGBA8 source pixels to target channel count */
static uint8_t *strip_channels(const uint8_t *rgba, uint32_t pixel_count, uint32_t target_channels) {
    if (target_channels >= 4) {
        return NULL; /* no strip needed */
    }
    uint8_t *out = (uint8_t *)malloc((size_t)pixel_count * target_channels);
    if (!out) {
        return NULL;
    }
    for (uint32_t i = 0; i < pixel_count; i++) {
        for (uint32_t c = 0; c < target_channels; c++) {
            out[(i * target_channels) + c] = rgba[(i * 4) + c];
        }
    }
    return out;
}

/* Shared import logic for both file and memory paths */
static nt_build_result_t import_texture_pixels(NtBuilderContext *ctx, unsigned char *pixels, int w, int h, uint64_t resource_id, const nt_tex_opts_t *opts) {
    nt_texture_pixel_format_t fmt = (opts && opts->format) ? opts->format : NT_TEXTURE_FORMAT_RGBA8;
    uint32_t max_size = opts ? opts->max_size : 0;

    int out_w = 0;
    int out_h = 0;
    unsigned char *resized = resize_if_needed(pixels, w, h, max_size, &out_w, &out_h);
    if (max_size > 0 && !resized && ((uint32_t)w > max_size || (uint32_t)h > max_size)) {
        stbi_image_free(pixels);
        return NT_BUILD_ERR_IO;
    }

    const unsigned char *src = resized ? resized : pixels;
    uint32_t pixel_count = (uint32_t)out_w * (uint32_t)out_h;
    uint32_t bpp = nt_texture_bpp(fmt);

    /* Strip channels if needed */
    uint8_t *stripped = NULL;
    const uint8_t *final_data;
    if (bpp < 4) {
        stripped = strip_channels(src, pixel_count, bpp);
        if (!stripped) {
            free(resized);
            stbi_image_free(pixels);
            return NT_BUILD_ERR_IO;
        }
        final_data = stripped;
    } else {
        final_data = src;
    }

    /* Write v2 header (RAW compression — uncompressed pixel data) */
    uint32_t data_size = pixel_count * bpp;
    NtTextureAssetHeaderV2 tex_hdr;
    memset(&tex_hdr, 0, sizeof(tex_hdr));
    tex_hdr.magic = NT_TEXTURE_MAGIC;
    tex_hdr.version = NT_TEXTURE_VERSION_V2;
    tex_hdr.format = (uint16_t)fmt;
    tex_hdr.width = (uint32_t)out_w;
    tex_hdr.height = (uint32_t)out_h;
    tex_hdr.mip_count = 1;
    tex_hdr.compression = (uint8_t)NT_TEXTURE_COMPRESSION_RAW;
    tex_hdr._pad = 0;
    tex_hdr.data_size = data_size;

    uint32_t total_asset_size = (uint32_t)sizeof(NtTextureAssetHeaderV2) + data_size;

    nt_build_result_t ret = nt_builder_append_data(ctx, &tex_hdr, (uint32_t)sizeof(NtTextureAssetHeaderV2));
    if (ret == NT_BUILD_OK) {
        ret = nt_builder_append_data(ctx, final_data, data_size);
    }

    free(stripped);
    free(resized);
    stbi_image_free(pixels);

    if (ret != NT_BUILD_OK) {
        return ret;
    }

    return nt_builder_register_asset(ctx, resource_id, NT_ASSET_TEXTURE, NT_TEXTURE_VERSION_V2, total_asset_size);
}

/* --- Texture import from file (called from finish_pack) --- */

nt_build_result_t nt_builder_import_texture(NtBuilderContext *ctx, const char *path, uint64_t resource_id) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels) {
        NT_LOG_ERROR("%s: %s", path, stbi_failure_reason());
        return NT_BUILD_ERR_IO;
    }

    if ((uint32_t)w > NT_BUILD_MAX_TEXTURE_SIZE || (uint32_t)h > NT_BUILD_MAX_TEXTURE_SIZE) {
        NT_LOG_ERROR("%s: %ux%u exceeds max %u", path, (uint32_t)w, (uint32_t)h, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
        stbi_image_free(pixels);
        return NT_BUILD_ERR_LIMIT;
    }

    /* File-based textures use default opts (RGBA8, no resize) */
    return import_texture_pixels(ctx, pixels, w, h, resource_id, NULL);
}

/* --- Texture import from memory (called from finish_pack) --- */

nt_build_result_t nt_builder_import_texture_from_memory(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, uint64_t resource_id, const nt_tex_opts_t *opts) {
    if (!ctx || !data || size == 0) {
        return NT_BUILD_ERR_VALIDATION;
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    if (!pixels) {
        NT_LOG_ERROR("texture from memory: %s", stbi_failure_reason());
        return NT_BUILD_ERR_IO;
    }

    if ((uint32_t)w > NT_BUILD_MAX_TEXTURE_SIZE || (uint32_t)h > NT_BUILD_MAX_TEXTURE_SIZE) {
        NT_LOG_ERROR("texture from memory: %ux%u exceeds max %u", (uint32_t)w, (uint32_t)h, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
        stbi_image_free(pixels);
        return NT_BUILD_ERR_LIMIT;
    }

    return import_texture_pixels(ctx, pixels, w, h, resource_id, opts);
}

/* --- Texture import from raw RGBA pixels (called from finish_pack) --- */

nt_build_result_t nt_builder_import_texture_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, uint64_t resource_id, const nt_tex_opts_t *opts) {
    if (!ctx || !rgba_pixels || width == 0 || height == 0) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Create a malloc'd copy -- import_texture_pixels frees via stbi_image_free (= free) */
    uint32_t data_size = width * height * 4;
    unsigned char *pixels = (unsigned char *)malloc(data_size);
    if (!pixels) {
        return NT_BUILD_ERR_IO;
    }
    memcpy(pixels, rgba_pixels, data_size);

    return import_texture_pixels(ctx, pixels, (int)width, (int)height, resource_id, opts);
}

/* --- Compressed texture import (Basis Universal encoding) --- */

static nt_build_result_t import_texture_compressed(NtBuilderContext *ctx, unsigned char *pixels, int w, int h, uint64_t resource_id, const nt_tex_opts_t *opts,
                                                   const nt_tex_compress_opts_t *compress_opts) {
    nt_texture_pixel_format_t fmt = (opts && opts->format) ? opts->format : NT_TEXTURE_FORMAT_RGBA8;
    uint32_t max_size = opts ? opts->max_size : 0;

    int out_w = 0;
    int out_h = 0;
    unsigned char *resized = resize_if_needed(pixels, w, h, max_size, &out_w, &out_h);
    if (max_size > 0 && !resized && ((uint32_t)w > max_size || (uint32_t)h > max_size)) {
        stbi_image_free(pixels);
        return NT_BUILD_ERR_IO;
    }

    const unsigned char *src = resized ? resized : pixels;

    /* Determine alpha from format (D-06) */
    bool has_alpha = (fmt == NT_TEXTURE_FORMAT_RGBA8);

    /* Lazy-init encoder */
    if (!s_encoder_initialized) {
        nt_basisu_encoder_init();
        s_encoder_initialized = true;
    }

    /* Encode via Basis Universal */
    bool uastc = (compress_opts->mode == NT_TEX_COMPRESS_UASTC);
    nt_basisu_encode_result_t enc = nt_basisu_encode(src, (uint32_t)out_w, (uint32_t)out_h, has_alpha, uastc, compress_opts->quality, compress_opts->rdo_quality, true);

    free(resized);
    stbi_image_free(pixels);

    if (!enc.data) {
        NT_LOG_ERROR("Basis encode failed (%ux%u, %s)", (uint32_t)out_w, (uint32_t)out_h, uastc ? "UASTC" : "ETC1S");
        return NT_BUILD_ERR_IO;
    }

    /* Write v2 header */
    NtTextureAssetHeaderV2 tex_hdr;
    memset(&tex_hdr, 0, sizeof(tex_hdr));
    tex_hdr.magic = NT_TEXTURE_MAGIC;
    tex_hdr.version = NT_TEXTURE_VERSION_V2;
    tex_hdr.format = (uint16_t)fmt;
    tex_hdr.width = (uint32_t)out_w;
    tex_hdr.height = (uint32_t)out_h;
    tex_hdr.mip_count = (uint16_t)enc.mip_count;
    tex_hdr.compression = (uint8_t)NT_TEXTURE_COMPRESSION_BASIS;
    tex_hdr._pad = 0;
    tex_hdr.data_size = enc.size;

    uint32_t total_asset_size = (uint32_t)sizeof(NtTextureAssetHeaderV2) + enc.size;

    nt_build_result_t ret = nt_builder_append_data(ctx, &tex_hdr, (uint32_t)sizeof(NtTextureAssetHeaderV2));
    if (ret == NT_BUILD_OK) {
        ret = nt_builder_append_data(ctx, enc.data, enc.size);
    }

    nt_basisu_encode_free(&enc);

    if (ret != NT_BUILD_OK) {
        return ret;
    }

    return nt_builder_register_asset(ctx, resource_id, NT_ASSET_TEXTURE, NT_TEXTURE_VERSION_V2, total_asset_size);
}

/* --- Texture import from memory with compression (called from finish_pack) --- */

nt_build_result_t nt_builder_import_texture_from_memory_compressed(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, uint64_t resource_id, const nt_tex_opts_t *opts,
                                                                   const nt_tex_compress_opts_t *compress_opts) {
    if (!ctx || !data || size == 0) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* NULL compress_opts = fall back to v1 raw path */
    if (!compress_opts) {
        return nt_builder_import_texture_from_memory(ctx, data, size, resource_id, opts);
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    if (!pixels) {
        NT_LOG_ERROR("compressed texture from memory: %s", stbi_failure_reason());
        return NT_BUILD_ERR_IO;
    }

    if ((uint32_t)w > NT_BUILD_MAX_TEXTURE_SIZE || (uint32_t)h > NT_BUILD_MAX_TEXTURE_SIZE) {
        NT_LOG_ERROR("compressed texture from memory: %ux%u exceeds max %u", (uint32_t)w, (uint32_t)h, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
        stbi_image_free(pixels);
        return NT_BUILD_ERR_LIMIT;
    }

    return import_texture_compressed(ctx, pixels, w, h, resource_id, opts, compress_opts);
}
