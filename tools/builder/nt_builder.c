/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_crc32.h"
/* clang-format on */

/* --- Deferred entry management --- */

static int32_t nt_builder_find_entry(NtBuilderContext *ctx, uint32_t resource_id) {
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        if (ctx->pending[i].resource_id == resource_id) {
            return (int32_t)i;
        }
    }
    return -1;
}

static nt_build_result_t nt_builder_add_entry(NtBuilderContext *ctx, const char *path, uint32_t resource_id, nt_build_asset_kind_t kind, nt_build_shader_stage_t shader_stage,
                                              const NtStreamLayout *layout, uint32_t stream_count) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }

    char *norm_path = nt_builder_normalize_path(path);
    if (!norm_path) {
        return NT_BUILD_ERR_IO;
    }

    /* Check for existing entry with same resource_id -- replace (force semantics) */
    int32_t existing = nt_builder_find_entry(ctx, resource_id);
    NtBuildEntry *entry;

    if (existing >= 0) {
        entry = &ctx->pending[existing];
        free(entry->path);
        (void)printf("  Replacing: %s\n", norm_path);
    } else {
        if (ctx->pending_count >= NT_BUILD_MAX_ASSETS) {
            (void)fprintf(stderr, "ERROR: Asset limit reached (%d max)\n", NT_BUILD_MAX_ASSETS);
            free(norm_path);
            return NT_BUILD_ERR_LIMIT;
        }
        entry = &ctx->pending[ctx->pending_count++];
    }

    entry->path = norm_path;
    entry->resource_id = resource_id;
    entry->kind = kind;
    entry->shader_stage = shader_stage;
    entry->stream_count = stream_count;
    if (layout && stream_count > 0) {
        memcpy(entry->layout, layout, stream_count * sizeof(NtStreamLayout));
    }

    return NT_BUILD_OK;
}

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
    ctx->pack_id = nt_builder_fnv1a(output_path);

    (void)printf("Starting pack: %s\n", output_path);
    return ctx;
}

/* --- Data accumulation (used during finish_pack import phase) --- */

nt_build_result_t nt_builder_append_data(NtBuilderContext *ctx, const void *data, uint32_t size) {
    if (!ctx || !data || size == 0) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Grow buffer if needed */
    while (ctx->data_size + size > ctx->data_capacity) {
        uint32_t new_capacity = ctx->data_capacity > 0 ? ctx->data_capacity * 2 : NT_BUILD_INITIAL_CAPACITY;
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

    uint32_t idx = ctx->entry_count;

    if (path) {
        ctx->resource_paths[idx] = strdup(path);
        assert(ctx->resource_paths[idx] && "strdup failed: out of memory");
    } else {
        ctx->resource_paths[idx] = NULL;
    }

    NtAssetEntry *entry = &ctx->entries[idx];
    entry->resource_id = resource_id;
    entry->format_version = format_version;
    entry->asset_type = (uint8_t)type;
    entry->_pad = 0;
    entry->offset = 0; /* computed later */
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

/* --- Cleanup --- */

static void nt_builder_free_context(NtBuilderContext *ctx) {
    if (!ctx) {
        return;
    }
    free(ctx->data_buf);
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        free(ctx->pending[i].path);
    }
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        free(ctx->resource_paths[i]);
    }
    free(ctx);
}

void nt_builder_free_pack(NtBuilderContext *ctx) { nt_builder_free_context(ctx); }

/* --- Finish: import all deferred entries, write pack --- */

nt_build_result_t nt_builder_finish_pack(NtBuilderContext *ctx) {
    if (!ctx) {
        return NT_BUILD_ERR_VALIDATION;
    }

    if (ctx->pending_count == 0) {
        (void)fprintf(stderr, "ERROR: No assets added.\n");
        nt_builder_free_context(ctx);
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Phase 1: Import all deferred assets */
    (void)printf("Importing %u assets...\n", ctx->pending_count);

    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        NtBuildEntry *pe = &ctx->pending[i];
        nt_build_result_t ret = NT_BUILD_OK;

        switch (pe->kind) {
        case NT_BUILD_ASSET_MESH:
            ret = nt_builder_import_mesh(ctx, pe->path, pe->layout, pe->stream_count, pe->resource_id);
            break;
        case NT_BUILD_ASSET_TEXTURE:
            ret = nt_builder_import_texture(ctx, pe->path, pe->resource_id);
            break;
        case NT_BUILD_ASSET_SHADER:
            ret = nt_builder_import_shader(ctx, pe->path, pe->shader_stage, pe->resource_id);
            break;
        }

        if (ret != NT_BUILD_OK) {
            ctx->has_error = true;
            (void)fprintf(stderr, "  FAILED: %s\n", pe->path);
        }
    }

    if (ctx->has_error) {
        (void)fprintf(stderr, "ERROR: Build failed. No .neopak written.\n");
        nt_builder_free_context(ctx);
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Phase 2: Compute layout and write pack file */
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + ctx->entry_count * sizeof(NtAssetEntry));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);

    uint32_t data_offset = 0;
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        ctx->entries[i].offset = header_size + data_offset;
        data_offset += (ctx->entries[i].size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
    }

    uint32_t total_size = header_size + ctx->data_size;
    uint32_t checksum = nt_crc32(ctx->data_buf, ctx->data_size);

    NtPackHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = NT_PACK_MAGIC;
    header.pack_id = ctx->pack_id;
    header.version = NT_PACK_VERSION;
    header.asset_count = (uint16_t)ctx->entry_count;
    header.header_size = header_size;
    header.total_size = total_size;
    header.checksum = checksum;

    FILE *file = fopen(ctx->output_path, "wb");
    if (!file) {
        (void)fprintf(stderr, "ERROR: Cannot open output file: %s\n", ctx->output_path);
        nt_builder_free_context(ctx);
        return NT_BUILD_ERR_IO;
    }

    bool write_ok = true;
    write_ok = write_ok && (fwrite(&header, sizeof(header), 1, file) == 1);
    if (write_ok && ctx->entry_count > 0) {
        write_ok = write_ok && (fwrite(ctx->entries, ctx->entry_count * sizeof(NtAssetEntry), 1, file) == 1);
    }
    uint32_t header_padding = header_size - raw_header;
    if (write_ok && header_padding > 0) {
        uint8_t zeros[8] = {0};
        write_ok = write_ok && (fwrite(zeros, header_padding, 1, file) == 1);
    }
    if (write_ok && ctx->data_size > 0) {
        write_ok = write_ok && (fwrite(ctx->data_buf, ctx->data_size, 1, file) == 1);
    }

    (void)fclose(file);

    if (!write_ok) {
        (void)fprintf(stderr, "ERROR: Failed to write pack file\n");
        (void)remove(ctx->output_path);
        nt_builder_free_context(ctx);
        return NT_BUILD_ERR_IO;
    }

    /* Summary */
    (void)printf("Build complete: %s\n", ctx->output_path);
    (void)printf("  Assets: %u total (%u meshes, %u textures, %u shaders)\n", ctx->entry_count, ctx->mesh_count, ctx->texture_count, ctx->shader_count);
    (void)printf("  Pack size: %u bytes\n", total_size);
    (void)printf("  CRC32: 0x%08X\n", checksum);

    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        (void)printf("  %s -> 0x%08X\n", ctx->resource_paths[i] ? ctx->resource_paths[i] : "(unknown)", ctx->entries[i].resource_id);
    }

    nt_builder_free_context(ctx);
    return NT_BUILD_OK;
}

/* --- Lightweight add_* functions (deferred) --- */

nt_build_result_t nt_builder_add_mesh(NtBuilderContext *ctx, const char *path, const NtStreamLayout *layout, uint32_t stream_count) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }
    char *norm = nt_builder_normalize_path(path);
    uint32_t id = nt_builder_fnv1a(norm ? norm : path);
    free(norm);
    return nt_builder_add_entry(ctx, path, id, NT_BUILD_ASSET_MESH, (nt_build_shader_stage_t)0, layout, stream_count);
}

nt_build_result_t nt_builder_add_mesh_with_id(NtBuilderContext *ctx, const char *path, const NtStreamLayout *layout, uint32_t stream_count, uint32_t resource_id) {
    return nt_builder_add_entry(ctx, path, resource_id, NT_BUILD_ASSET_MESH, (nt_build_shader_stage_t)0, layout, stream_count);
}

nt_build_result_t nt_builder_add_texture(NtBuilderContext *ctx, const char *path) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }
    char *norm = nt_builder_normalize_path(path);
    uint32_t id = nt_builder_fnv1a(norm ? norm : path);
    free(norm);
    return nt_builder_add_entry(ctx, path, id, NT_BUILD_ASSET_TEXTURE, (nt_build_shader_stage_t)0, NULL, 0);
}

nt_build_result_t nt_builder_add_texture_with_id(NtBuilderContext *ctx, const char *path, uint32_t resource_id) {
    return nt_builder_add_entry(ctx, path, resource_id, NT_BUILD_ASSET_TEXTURE, (nt_build_shader_stage_t)0, NULL, 0);
}

nt_build_result_t nt_builder_add_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }
    char *norm = nt_builder_normalize_path(path);
    uint32_t id = nt_builder_fnv1a(norm ? norm : path);
    free(norm);
    return nt_builder_add_entry(ctx, path, id, NT_BUILD_ASSET_SHADER, stage, NULL, 0);
}

nt_build_result_t nt_builder_add_shader_with_id(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage, uint32_t resource_id) {
    return nt_builder_add_entry(ctx, path, resource_id, NT_BUILD_ASSET_SHADER, stage, NULL, 0);
}
