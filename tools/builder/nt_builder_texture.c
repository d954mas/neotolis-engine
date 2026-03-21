/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_texture_format.h"
#include "stb_image.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#include "stb_image_resize2.h"
#pragma clang diagnostic pop
/* clang-format on */

/* No mapping needed — builder and runtime share nt_texture_pixel_format_t.
 * BPP lookup uses nt_texture_bpp() from nt_texture_format.h. */

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

    int out_w = w;
    int out_h = h;
    unsigned char *resized = NULL;

    /* Resize if needed */
    if (max_size > 0 && ((uint32_t)w > max_size || (uint32_t)h > max_size)) {
        if ((uint32_t)w >= (uint32_t)h) {
            out_w = (int)max_size;
            out_h = (int)((uint32_t)h * max_size / (uint32_t)w);
        } else {
            out_h = (int)max_size;
            out_w = (int)((uint32_t)w * max_size / (uint32_t)h);
        }
        if (out_w < 1) {
            out_w = 1;
        }
        if (out_h < 1) {
            out_h = 1;
        }
        resized = (unsigned char *)malloc((size_t)out_w * (size_t)out_h * 4);
        if (!resized) {
            stbi_image_free(pixels);
            return NT_BUILD_ERR_IO;
        }
        stbir_resize_uint8_linear(pixels, w, h, 0, resized, out_w, out_h, 0, STBIR_RGBA);
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

    /* Write header */
    NtTextureAssetHeader tex_hdr;
    memset(&tex_hdr, 0, sizeof(tex_hdr));
    tex_hdr.magic = NT_TEXTURE_MAGIC;
    tex_hdr.version = NT_TEXTURE_VERSION;
    tex_hdr.format = (uint16_t)fmt;
    tex_hdr.width = (uint32_t)out_w;
    tex_hdr.height = (uint32_t)out_h;
    tex_hdr.mip_count = 1;
    tex_hdr._pad = 0;

    uint32_t data_size = pixel_count * bpp;
    uint32_t total_asset_size = (uint32_t)sizeof(NtTextureAssetHeader) + data_size;

    nt_build_result_t ret = nt_builder_append_data(ctx, &tex_hdr, (uint32_t)sizeof(NtTextureAssetHeader));
    if (ret == NT_BUILD_OK) {
        ret = nt_builder_append_data(ctx, final_data, data_size);
    }

    free(stripped);
    free(resized);
    stbi_image_free(pixels);

    if (ret != NT_BUILD_OK) {
        return ret;
    }

    return nt_builder_register_asset(ctx, resource_id, NT_ASSET_TEXTURE, NT_TEXTURE_VERSION, total_asset_size);
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
        (void)fprintf(stderr, "ERROR: %s: %s\n", path, stbi_failure_reason());
        return NT_BUILD_ERR_IO;
    }

    if ((uint32_t)w > NT_BUILD_MAX_TEXTURE_SIZE || (uint32_t)h > NT_BUILD_MAX_TEXTURE_SIZE) {
        (void)fprintf(stderr, "ERROR: %s: %ux%u exceeds max %u\n", path, (uint32_t)w, (uint32_t)h, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
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
        (void)fprintf(stderr, "ERROR: texture from memory: %s\n", stbi_failure_reason());
        return NT_BUILD_ERR_IO;
    }

    if ((uint32_t)w > NT_BUILD_MAX_TEXTURE_SIZE || (uint32_t)h > NT_BUILD_MAX_TEXTURE_SIZE) {
        (void)fprintf(stderr, "ERROR: texture from memory: %ux%u exceeds max %u\n", (uint32_t)w, (uint32_t)h, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
        stbi_image_free(pixels);
        return NT_BUILD_ERR_LIMIT;
    }

    return import_texture_pixels(ctx, pixels, w, h, resource_id, opts);
}
