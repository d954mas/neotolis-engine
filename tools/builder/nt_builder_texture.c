/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_texture_format.h"
#include "stb_image.h"
/* clang-format on */

/* --- Texture importer --- */

nt_build_result_t nt_builder_add_texture_with_id(NtBuilderContext *ctx, const char *path, uint32_t resource_id) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Load PNG (force RGBA) */
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels) {
        (void)fprintf(stderr, "ERROR: %s: %s\n", path, stbi_failure_reason());
        ctx->has_error = true;
        return NT_BUILD_ERR_IO;
    }

    /* Validate dimensions */
    if ((uint32_t)w > NT_BUILD_MAX_TEXTURE_SIZE || (uint32_t)h > NT_BUILD_MAX_TEXTURE_SIZE) {
        (void)fprintf(stderr, "ERROR: %s: %ux%u exceeds max %u\n", path, (uint32_t)w, (uint32_t)h, (uint32_t)NT_BUILD_MAX_TEXTURE_SIZE);
        stbi_image_free(pixels);
        ctx->has_error = true;
        return NT_BUILD_ERR_LIMIT;
    }

    /* Build texture asset header */
    NtTextureAssetHeader tex_hdr;
    memset(&tex_hdr, 0, sizeof(tex_hdr));
    tex_hdr.magic = NT_TEXTURE_MAGIC;
    tex_hdr.version = NT_TEXTURE_VERSION;
    tex_hdr.format = NT_TEXTURE_FORMAT_RGBA8;
    tex_hdr.width = (uint32_t)w;
    tex_hdr.height = (uint32_t)h;
    tex_hdr.mip_count = 1;
    tex_hdr._pad = 0;

    uint32_t pixel_data_size = (uint32_t)w * (uint32_t)h * 4;
    uint32_t total_asset_size = (uint32_t)sizeof(NtTextureAssetHeader) + pixel_data_size;

    /* Append header + pixel data */
    nt_build_result_t ret = nt_builder_append_data(ctx, &tex_hdr, (uint32_t)sizeof(NtTextureAssetHeader));
    if (ret == NT_BUILD_OK) {
        ret = nt_builder_append_data(ctx, pixels, pixel_data_size);
    }

    stbi_image_free(pixels);

    if (ret != NT_BUILD_OK) {
        return ret;
    }

    /* Register asset */
    char *norm_path = nt_builder_normalize_path(path);
    ret = nt_builder_register_asset(ctx, norm_path ? norm_path : path, resource_id, NT_ASSET_TEXTURE, NT_TEXTURE_VERSION, total_asset_size);
    free(norm_path);

    return ret;
}

nt_build_result_t nt_builder_add_texture(NtBuilderContext *ctx, const char *path) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }
    char *norm_path = nt_builder_normalize_path(path);
    uint32_t resource_id = nt_builder_fnv1a(norm_path ? norm_path : path);
    free(norm_path);
    return nt_builder_add_texture_with_id(ctx, path, resource_id);
}
