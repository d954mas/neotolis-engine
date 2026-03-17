/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_crc32.h"
/* clang-format on */

/* --- Core pack writer --- */

NtBuilderContext *nt_builder_start_pack(const char *output_path) {
    if (!output_path) {
        (void)fprintf(stderr, "ERROR: output_path is NULL\n");
        return NULL;
    }

    NtBuilderContext *ctx = (NtBuilderContext *)calloc(1, sizeof(NtBuilderContext));
    if (!ctx) {
        (void)fprintf(stderr, "ERROR: Failed to allocate builder context\n");
        return NULL;
    }

    strncpy(ctx->output_path, output_path, sizeof(ctx->output_path) - 1);
    ctx->output_path[sizeof(ctx->output_path) - 1] = '\0';

    ctx->data_capacity = NT_BUILD_INITIAL_CAPACITY;
    ctx->data_buf = (uint8_t *)malloc(ctx->data_capacity);
    if (!ctx->data_buf) {
        (void)fprintf(stderr, "ERROR: Failed to allocate data buffer (%u bytes)\n", ctx->data_capacity);
        free(ctx);
        return NULL;
    }

    ctx->pack_id = nt_builder_fnv1a(output_path);
    ctx->data_size = 0;
    ctx->entry_count = 0;
    ctx->has_error = false;
    ctx->mesh_count = 0;
    ctx->texture_count = 0;
    ctx->shader_count = 0;

    (void)printf("Starting pack: %s\n", output_path);
    return ctx;
}

nt_build_result_t nt_builder_append_data(NtBuilderContext *ctx, const void *data, uint32_t size) {
    if (!ctx || !data || size == 0) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Grow buffer if needed */
    while (ctx->data_size + size > ctx->data_capacity) {
        uint32_t new_capacity = ctx->data_capacity * 2;
        uint8_t *new_buf = (uint8_t *)realloc(ctx->data_buf, new_capacity);
        if (!new_buf) {
            (void)fprintf(stderr, "ERROR: Failed to grow data buffer to %u bytes\n", new_capacity);
            return NT_BUILD_ERR_IO;
        }
        ctx->data_buf = new_buf;
        ctx->data_capacity = new_capacity;
    }

    memcpy(ctx->data_buf + ctx->data_size, data, size);
    ctx->data_size += size;
    return NT_BUILD_OK;
}

nt_build_result_t nt_builder_register_asset(NtBuilderContext *ctx, const char *path, uint32_t resource_id, nt_asset_type_t type, uint16_t format_version, uint32_t data_size) {
    if (!ctx) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Check asset limit */
    if (ctx->entry_count >= NT_BUILD_MAX_ASSETS) {
        (void)fprintf(stderr, "ERROR: Asset limit reached (%d max)\n", NT_BUILD_MAX_ASSETS);
        ctx->has_error = true;
        return NT_BUILD_ERR_LIMIT;
    }

    /* Check for duplicates */
    if (nt_builder_check_duplicate(ctx, resource_id, path)) {
        ctx->has_error = true;
        return NT_BUILD_ERR_DUPLICATE;
    }

    /* Store tracking data */
    uint32_t idx = ctx->entry_count;
    ctx->resource_ids[idx] = resource_id;
    ctx->resource_paths[idx] = path ? strdup(path) : NULL;

    /* Fill entry */
    NtAssetEntry *entry = &ctx->entries[idx];
    entry->resource_id = resource_id;
    entry->format_version = format_version;
    entry->asset_type = (uint8_t)type;
    entry->_pad = 0;
    entry->offset = 0; /* computed in finish_pack */
    entry->size = data_size;

    /* Pad data buffer to asset alignment */
    uint32_t aligned_size = (data_size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
    uint32_t padding = aligned_size - data_size;
    if (padding > 0) {
        uint8_t zeros[4] = {0};
        nt_build_result_t res = nt_builder_append_data(ctx, zeros, padding);
        if (res != NT_BUILD_OK) {
            return res;
        }
    }

    ctx->entry_count++;

    /* Per-type counter */
    switch (type) {
    case NT_ASSET_MESH:
        ctx->mesh_count++;
        break;
    case NT_ASSET_TEXTURE:
        ctx->texture_count++;
        break;
    case NT_ASSET_SHADER_CODE:
        ctx->shader_count++;
        break;
    }

    return NT_BUILD_OK;
}

static void nt_builder_free_context(NtBuilderContext *ctx) {
    if (!ctx) {
        return;
    }
    free(ctx->data_buf);
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        free(ctx->resource_paths[i]);
    }
    free(ctx);
}

nt_build_result_t nt_builder_finish_pack(NtBuilderContext *ctx) {
    if (!ctx) {
        return NT_BUILD_ERR_VALIDATION;
    }

    if (ctx->has_error) {
        (void)fprintf(stderr, "ERROR: Build failed. No .neopak written.\n");
        nt_builder_free_context(ctx);
        return NT_BUILD_ERR_VALIDATION;
    }

    if (ctx->entry_count == 0) {
        (void)fprintf(stderr, "ERROR: No assets added.\n");
        nt_builder_free_context(ctx);
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Compute header size (aligned to data boundary) */
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + ctx->entry_count * sizeof(NtAssetEntry));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);

    /* Fix up entry offsets */
    uint32_t data_offset = 0;
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        ctx->entries[i].offset = header_size + data_offset;
        data_offset += (ctx->entries[i].size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
    }

    uint32_t total_size = header_size + ctx->data_size;

    /* Compute CRC32 over data region */
    uint32_t checksum = nt_crc32(ctx->data_buf, ctx->data_size);

    /* Fill pack header */
    NtPackHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = NT_PACK_MAGIC;
    header.pack_id = ctx->pack_id;
    header.version = NT_PACK_VERSION;
    header.asset_count = (uint16_t)ctx->entry_count;
    header.header_size = header_size;
    header.total_size = total_size;
    header.checksum = checksum;

    /* Write file */
    FILE *file = fopen(ctx->output_path, "wb");
    if (!file) {
        (void)fprintf(stderr, "ERROR: Cannot open output file: %s\n", ctx->output_path);
        nt_builder_free_context(ctx);
        return NT_BUILD_ERR_IO;
    }

    /* Write header */
    if (fwrite(&header, sizeof(header), 1, file) != 1) {
        (void)fprintf(stderr, "ERROR: Failed to write pack header\n");
        (void)fclose(file);
        nt_builder_free_context(ctx);
        return NT_BUILD_ERR_IO;
    }

    /* Write entry array */
    if (ctx->entry_count > 0) {
        size_t entries_size = ctx->entry_count * sizeof(NtAssetEntry);
        if (fwrite(ctx->entries, entries_size, 1, file) != 1) {
            (void)fprintf(stderr, "ERROR: Failed to write asset entries\n");
            (void)fclose(file);
            nt_builder_free_context(ctx);
            return NT_BUILD_ERR_IO;
        }
    }

    /* Write header padding (gap between entries and data) */
    uint32_t header_padding = header_size - raw_header;
    if (header_padding > 0) {
        uint8_t zeros[8] = {0};
        if (fwrite(zeros, header_padding, 1, file) != 1) {
            (void)fprintf(stderr, "ERROR: Failed to write header padding\n");
            (void)fclose(file);
            nt_builder_free_context(ctx);
            return NT_BUILD_ERR_IO;
        }
    }

    /* Write data region */
    if (ctx->data_size > 0) {
        if (fwrite(ctx->data_buf, ctx->data_size, 1, file) != 1) {
            (void)fprintf(stderr, "ERROR: Failed to write data region\n");
            (void)fclose(file);
            nt_builder_free_context(ctx);
            return NT_BUILD_ERR_IO;
        }
    }

    (void)fclose(file);

    /* Print build summary */
    (void)printf("Build complete: %s\n", ctx->output_path);
    (void)printf("  Assets: %u total (%u meshes, %u textures, %u shaders)\n", ctx->entry_count, ctx->mesh_count, ctx->texture_count, ctx->shader_count);
    (void)printf("  Pack size: %u bytes\n", total_size);
    (void)printf("  CRC32: 0x%08X\n", checksum);

    /* Print resource ID table */
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        (void)printf("  %s -> 0x%08X\n", ctx->resource_paths[i] ? ctx->resource_paths[i] : "(unknown)", ctx->entries[i].resource_id);
    }

    nt_builder_free_context(ctx);
    return NT_BUILD_OK;
}

/* --- Stub implementations for batch functions (implemented in Plan 03) --- */

nt_build_result_t nt_builder_add_meshes(NtBuilderContext *ctx, const char *pattern, const NtStreamLayout *layout, uint32_t stream_count) {
    (void)ctx;
    (void)pattern;
    (void)layout;
    (void)stream_count;
    (void)fprintf(stderr, "ERROR: nt_builder_add_meshes not yet implemented\n");
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_textures(NtBuilderContext *ctx, const char *pattern) {
    (void)ctx;
    (void)pattern;
    (void)fprintf(stderr, "ERROR: nt_builder_add_textures not yet implemented\n");
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_shaders(NtBuilderContext *ctx, const char *pattern, nt_build_shader_stage_t stage) {
    (void)ctx;
    (void)pattern;
    (void)stage;
    (void)fprintf(stderr, "ERROR: nt_builder_add_shaders not yet implemented\n");
    return NT_BUILD_ERR_VALIDATION;
}
