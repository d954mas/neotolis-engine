/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_crc32.h"
/* clang-format on */

/* --- Entry data management --- */

static void nt_builder_free_mesh_data(NtBuildMeshData *md) {
    if (!md) {
        return;
    }
    for (uint32_t s = 0; s < md->stream_count; s++) {
        free((char *)md->layout[s].engine_name);
        free((char *)md->layout[s].gltf_name);
    }
    free(md);
}

static NtBuildMeshData *nt_builder_copy_mesh_data(const NtStreamLayout *layout, uint32_t stream_count) {
    NtBuildMeshData *md = (NtBuildMeshData *)calloc(1, sizeof(NtBuildMeshData));
    if (!md) {
        return NULL;
    }
    md->stream_count = stream_count;
    memcpy(md->layout, layout, stream_count * sizeof(NtStreamLayout));
    for (uint32_t s = 0; s < stream_count; s++) {
        if (layout[s].engine_name) {
            md->layout[s].engine_name = strdup(layout[s].engine_name);
            NT_BUILD_ASSERT(md->layout[s].engine_name && "strdup engine_name failed");
        }
        if (layout[s].gltf_name) {
            md->layout[s].gltf_name = strdup(layout[s].gltf_name);
            NT_BUILD_ASSERT(md->layout[s].gltf_name && "strdup gltf_name failed");
        }
    }
    return md;
}

static NtBuildShaderData *nt_builder_copy_shader_data(nt_build_shader_stage_t stage) {
    NtBuildShaderData *sd = (NtBuildShaderData *)calloc(1, sizeof(NtBuildShaderData));
    if (!sd) {
        return NULL;
    }
    sd->stage = stage;
    return sd;
}

static void nt_builder_free_blob_data(NtBuildBlobData *bd) {
    if (!bd) {
        return;
    }
    free(bd->data);
    free(bd);
}

static void nt_builder_free_scene_mesh_data(NtBuildSceneMeshData *sd) {
    if (!sd) {
        return;
    }
    for (uint32_t s = 0; s < sd->stream_count; s++) {
        free((char *)sd->layout[s].engine_name);
        free((char *)sd->layout[s].gltf_name);
    }
    free(sd);
}

static void nt_builder_free_tex_mem_data(NtBuildTexMemData *td) {
    if (!td) {
        return;
    }
    free(td->data);
    free(td);
}

static void nt_builder_free_tex_raw_data(NtBuildTexRawData *td) {
    if (!td) {
        return;
    }
    free(td->pixels);
    free(td);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void nt_builder_free_entry_data(NtBuildEntry *entry) {
    if (!entry->data) {
        return;
    }
    switch (entry->kind) {
    case NT_BUILD_ASSET_MESH:
        nt_builder_free_mesh_data((NtBuildMeshData *)entry->data);
        break;
    case NT_BUILD_ASSET_BLOB:
        nt_builder_free_blob_data((NtBuildBlobData *)entry->data);
        break;
    case NT_BUILD_ASSET_SCENE_MESH:
        nt_builder_free_scene_mesh_data((NtBuildSceneMeshData *)entry->data);
        break;
    case NT_BUILD_ASSET_TEXTURE_MEM:
        nt_builder_free_tex_mem_data((NtBuildTexMemData *)entry->data);
        break;
    case NT_BUILD_ASSET_TEXTURE_RAW:
        nt_builder_free_tex_raw_data((NtBuildTexRawData *)entry->data);
        break;
    default:
        free(entry->data);
        break;
    }
    entry->data = NULL;
}

/* --- Deferred entry management --- */

static int32_t nt_builder_find_entry(NtBuilderContext *ctx, uint64_t resource_id) {
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        if (ctx->pending[i].resource_id == resource_id) {
            return (int32_t)i;
        }
    }
    return -1;
}

static void nt_builder_free_entry(NtBuildEntry *entry) {
    free(entry->path);
    free(entry->rename_key);
    nt_builder_free_entry_data(entry);
    entry->path = NULL;
    entry->rename_key = NULL;
}

static nt_build_result_t nt_builder_add_entry(NtBuilderContext *ctx, const char *path, nt_build_asset_kind_t kind, void *data) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }

    char *norm_path = nt_builder_normalize_path(path);
    if (!norm_path) {
        return NT_BUILD_ERR_IO;
    }

    uint64_t resource_id = nt_hash64_str(norm_path).value;

    int32_t existing = nt_builder_find_entry(ctx, resource_id);
    if (existing >= 0) {
        if (ctx->force) {
            nt_builder_free_entry(&ctx->pending[existing]);
            ctx->pending[existing].path = norm_path;
            ctx->pending[existing].resource_id = resource_id;
            ctx->pending[existing].kind = kind;
            ctx->pending[existing].data = data;
            return NT_BUILD_OK;
        }
        (void)fprintf(stderr, "ERROR: duplicate resource_id 0x%016llX\n  existing: %s\n  new:      %s\n", (unsigned long long)resource_id, ctx->pending[existing].path, norm_path);
        free(norm_path);
        return NT_BUILD_ERR_DUPLICATE;
    }

    if (ctx->pending_count >= NT_BUILD_MAX_ASSETS) {
        (void)fprintf(stderr, "ERROR: Asset limit reached (%d max)\n", NT_BUILD_MAX_ASSETS);
        free(norm_path);
        return NT_BUILD_ERR_LIMIT;
    }

    NtBuildEntry *entry = &ctx->pending[ctx->pending_count++];
    entry->path = norm_path;
    entry->rename_key = NULL;
    entry->resource_id = resource_id;
    entry->kind = kind;
    entry->data = data;
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

    (void)printf("Starting pack: %s\n", output_path);
    return ctx;
}

/* --- Data accumulation (used during finish_pack import phase) --- */

nt_build_result_t nt_builder_append_data(NtBuilderContext *ctx, const void *data, uint32_t size) {
    if (!ctx || !data || size == 0) {
        return NT_BUILD_ERR_VALIDATION;
    }

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

nt_build_result_t nt_builder_register_asset(NtBuilderContext *ctx, uint64_t resource_id, nt_asset_type_t type, uint16_t format_version, uint32_t data_size) {
    if (!ctx) {
        return NT_BUILD_ERR_VALIDATION;
    }

    if (ctx->entry_count >= NT_BUILD_MAX_ASSETS) {
        (void)fprintf(stderr, "ERROR: entry limit reached (%d max)\n", NT_BUILD_MAX_ASSETS);
        return NT_BUILD_ERR_LIMIT;
    }

    /* Deduplicate: check if identical data already exists in an earlier entry.
     * The just-written data sits at data_buf[new_data_start .. data_size). */
    uint32_t new_data_start = ctx->data_size - data_size;
    const uint8_t *new_data = ctx->data_buf + new_data_start;
    uint32_t dedup_offset = UINT32_MAX;

    for (uint32_t ei = 0; ei < ctx->entry_count; ei++) {
        if (ctx->entries[ei].size == data_size && ctx->entries[ei].offset + data_size <= new_data_start) {
            if (memcmp(ctx->data_buf + ctx->entries[ei].offset, new_data, data_size) == 0) {
                dedup_offset = ctx->entries[ei].offset;
                break;
            }
        }
    }

    uint32_t idx = ctx->entry_count;
    NtAssetEntry *entry = &ctx->entries[idx];
    entry->resource_id = resource_id;
    entry->format_version = format_version;
    entry->asset_type = (uint8_t)type;
    entry->_pad = 0;
    entry->size = data_size;

    if (dedup_offset != UINT32_MAX) {
        /* Duplicate found — rewind data_buf, point to existing data */
        ctx->data_size = new_data_start;
        entry->offset = dedup_offset; /* temporary: relative to data_buf start */
        ctx->dedup_count++;
        ctx->dedup_saved_bytes += data_size;
    } else {
        entry->offset = new_data_start; /* temporary: relative to data_buf start */
        /* Add alignment padding */
        uint32_t aligned_size = (data_size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
        uint32_t padding = aligned_size - data_size;
        if (padding > 0) {
            uint8_t zeros[4] = {0};
            nt_build_result_t res = nt_builder_append_data(ctx, zeros, padding);
            if (res != NT_BUILD_OK) {
                return res;
            }
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
    case NT_ASSET_BLOB:
        ctx->blob_count++;
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
        nt_builder_free_entry(&ctx->pending[i]);
    }
    free(ctx);
}

void nt_builder_free_pack(NtBuilderContext *ctx) { nt_builder_free_context(ctx); }

/* --- Finish: import all deferred entries, write pack --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_finish_pack(NtBuilderContext *ctx) {
    if (!ctx) {
        return NT_BUILD_ERR_VALIDATION;
    }

    if (ctx->pending_count == 0) {
        (void)fprintf(stderr, "ERROR: No assets added.\n");
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Phase 1: Import all deferred assets */
    (void)printf("Importing %u assets...\n", ctx->pending_count);
    uint32_t fail_count = 0;

    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        NtBuildEntry *pe = &ctx->pending[i];
        nt_build_result_t ret = NT_BUILD_OK;

        switch (pe->kind) {
        case NT_BUILD_ASSET_MESH: {
            NtBuildMeshData *md = (NtBuildMeshData *)pe->data;
            ret = nt_builder_import_mesh(ctx, pe->path, md->layout, md->stream_count, pe->resource_id);
            break;
        }
        case NT_BUILD_ASSET_TEXTURE:
            ret = nt_builder_import_texture(ctx, pe->path, pe->resource_id);
            break;
        case NT_BUILD_ASSET_SHADER: {
            NtBuildShaderData *sd = (NtBuildShaderData *)pe->data;
            ret = nt_builder_import_shader(ctx, pe->path, sd->stage, pe->resource_id);
            break;
        }
        case NT_BUILD_ASSET_BLOB: {
            NtBuildBlobData *bd = (NtBuildBlobData *)pe->data;
            ret = nt_builder_import_blob(ctx, bd->data, bd->size, pe->resource_id);
            break;
        }
        case NT_BUILD_ASSET_SCENE_MESH: {
            NtBuildSceneMeshData *smd = (NtBuildSceneMeshData *)pe->data;
            ret = nt_builder_import_scene_mesh(ctx, smd->scene, smd->mesh_index, smd->primitive_index, smd->layout, smd->stream_count, smd->tangent_mode, pe->resource_id);
            break;
        }
        case NT_BUILD_ASSET_TEXTURE_MEM: {
            NtBuildTexMemData *tmd = (NtBuildTexMemData *)pe->data;
            ret = nt_builder_import_texture_from_memory(ctx, tmd->data, tmd->size, pe->resource_id, &tmd->opts);
            break;
        }
        case NT_BUILD_ASSET_TEXTURE_RAW: {
            NtBuildTexRawData *trd = (NtBuildTexRawData *)pe->data;
            ret = nt_builder_import_texture_raw(ctx, trd->pixels, trd->width, trd->height, pe->resource_id, &trd->opts);
            break;
        }
        }

        if (ret != NT_BUILD_OK) {
            ctx->has_error = true;
            fail_count++;
            (void)fprintf(stderr, "  FAILED: %s\n", pe->path);
        }
    }

    if (ctx->has_error) {
        (void)fprintf(stderr, "ERROR: Build failed: %u/%u assets failed. No .ntpack written.\n", fail_count, ctx->pending_count);
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Phase 2: Compute layout and write pack file */
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + (ctx->entry_count * sizeof(NtAssetEntry)));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);

    /* entry->offset is relative to data_buf start (set in register_asset).
     * Shift to absolute file offset by adding header_size. */
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        ctx->entries[i].offset += header_size;
    }

    uint32_t total_size = header_size + ctx->data_size;
    uint32_t checksum = nt_crc32(ctx->data_buf, ctx->data_size);

    NtPackHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = NT_PACK_MAGIC;
    header.version = NT_PACK_VERSION;
    header.asset_count = (uint16_t)ctx->entry_count;
    header.header_size = header_size;
    header.total_size = total_size;
    header.checksum = checksum;

    FILE *file = fopen(ctx->output_path, "wb");
    if (!file) {
        (void)fprintf(stderr, "ERROR: Cannot open output file: %s\n", ctx->output_path);
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
        return NT_BUILD_ERR_IO;
    }

    /* Summary */
    (void)printf("Build complete: %s\n", ctx->output_path);
    (void)printf("  Assets: %u total (%u meshes, %u textures, %u shaders, %u blobs)\n", ctx->entry_count, ctx->mesh_count, ctx->texture_count, ctx->shader_count, ctx->blob_count);
    if (ctx->dedup_count > 0) {
        (void)printf("  Deduplicated: %u assets (saved %.3f MB)\n", ctx->dedup_count, (double)ctx->dedup_saved_bytes / (1024.0 * 1024.0));
        for (uint32_t i = 0; i < ctx->entry_count; i++) {
            for (uint32_t j = 0; j < i; j++) {
                if (ctx->entries[i].offset == ctx->entries[j].offset && ctx->entries[i].size == ctx->entries[j].size) {
                    /* Find names by matching resource_id in pending[] */
                    const char *dup_name = "?";
                    const char *orig_name = "?";
                    for (uint32_t pi = 0; pi < ctx->pending_count; pi++) {
                        if (ctx->pending[pi].resource_id == ctx->entries[i].resource_id) {
                            dup_name = ctx->pending[pi].path ? ctx->pending[pi].path : "?";
                        }
                        if (ctx->pending[pi].resource_id == ctx->entries[j].resource_id) {
                            orig_name = ctx->pending[pi].path ? ctx->pending[pi].path : "?";
                        }
                    }
                    (void)printf("    %s -> %s\n", dup_name, orig_name);
                    break;
                }
            }
        }
    }
    if (total_size >= 1024 * 1024) {
        (void)printf("  Pack size: %.3f MB (%u bytes)\n", (double)total_size / (1024.0 * 1024.0), total_size);
    } else if (total_size >= 1024) {
        (void)printf("  Pack size: %.3f KB (%u bytes)\n", (double)total_size / 1024.0, total_size);
    } else {
        (void)printf("  Pack size: %u bytes\n", total_size);
    }
    (void)printf("  CRC32: 0x%08X\n", checksum);

    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        const char *path = ctx->pending[i].path ? ctx->pending[i].path : "(unknown)";
        const char *rkey = ctx->pending[i].rename_key;
        if (rkey) {
            (void)printf("  %s -> 0x%016llX (renamed: %s)\n", path, (unsigned long long)ctx->pending[i].resource_id, rkey);
        } else {
            (void)printf("  %s -> 0x%016llX\n", path, (unsigned long long)ctx->pending[i].resource_id);
        }
    }

    return NT_BUILD_OK;
}

/* --- Helper: compute resource_id from path --- */

static uint64_t nt_builder_path_id(const char *path) {
    char *norm = nt_builder_normalize_path(path);
    uint64_t id = nt_hash64_str(norm ? norm : path).value;
    free(norm);
    return id;
}

/* --- Public add_* --- */

nt_build_result_t nt_builder_add_mesh(NtBuilderContext *ctx, const char *path, const NtStreamLayout *layout, uint32_t stream_count) {
    if (!layout || stream_count == 0 || stream_count > NT_MESH_MAX_STREAMS) {
        return NT_BUILD_ERR_VALIDATION;
    }
    NtBuildMeshData *md = nt_builder_copy_mesh_data(layout, stream_count);
    if (!md) {
        return NT_BUILD_ERR_IO;
    }
    nt_build_result_t r = nt_builder_add_entry(ctx, path, NT_BUILD_ASSET_MESH, md);
    if (r != NT_BUILD_OK) {
        nt_builder_free_mesh_data(md);
    }
    return r;
}

nt_build_result_t nt_builder_add_texture(NtBuilderContext *ctx, const char *path) { return nt_builder_add_entry(ctx, path, NT_BUILD_ASSET_TEXTURE, NULL); }

nt_build_result_t nt_builder_add_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage) {
    NtBuildShaderData *sd = nt_builder_copy_shader_data(stage);
    if (!sd) {
        return NT_BUILD_ERR_IO;
    }
    nt_build_result_t r = nt_builder_add_entry(ctx, path, NT_BUILD_ASSET_SHADER, sd);
    if (r != NT_BUILD_OK) {
        free(sd);
    }
    return r;
}

/* --- Public add_blob --- */

nt_build_result_t nt_builder_add_blob(NtBuilderContext *ctx, const void *data, uint32_t size, const char *resource_id) {
    if (!ctx || !data || size == 0 || !resource_id) {
        return NT_BUILD_ERR_VALIDATION;
    }

    uint64_t rid = nt_hash64_str(resource_id).value;

    int32_t existing = nt_builder_find_entry(ctx, rid);
    if (existing >= 0 && !ctx->force) {
        (void)fprintf(stderr, "ERROR: duplicate resource_id for blob '%s'\n", resource_id);
        return NT_BUILD_ERR_DUPLICATE;
    }

    NtBuildBlobData *bd = (NtBuildBlobData *)calloc(1, sizeof(NtBuildBlobData));
    if (!bd) {
        return NT_BUILD_ERR_IO;
    }
    bd->data = malloc(size);
    if (!bd->data) {
        free(bd);
        return NT_BUILD_ERR_IO;
    }
    memcpy(bd->data, data, size);
    bd->size = size;

    if (ctx->pending_count >= NT_BUILD_MAX_ASSETS) {
        (void)fprintf(stderr, "ERROR: Asset limit reached (%d max)\n", NT_BUILD_MAX_ASSETS);
        free(bd->data);
        free(bd);
        return NT_BUILD_ERR_LIMIT;
    }

    if (existing >= 0) {
        nt_builder_free_entry(&ctx->pending[existing]);
        ctx->pending[existing].path = strdup(resource_id);
        ctx->pending[existing].rename_key = NULL;
        ctx->pending[existing].resource_id = rid;
        ctx->pending[existing].kind = NT_BUILD_ASSET_BLOB;
        ctx->pending[existing].data = bd;
        return NT_BUILD_OK;
    }

    NtBuildEntry *entry = &ctx->pending[ctx->pending_count++];
    entry->path = strdup(resource_id);
    entry->rename_key = NULL;
    entry->resource_id = rid;
    entry->kind = NT_BUILD_ASSET_BLOB;
    entry->data = bd;
    return NT_BUILD_OK;
}

/* --- Public add_texture_from_memory --- */

nt_build_result_t nt_builder_add_texture_from_memory_ex(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id, const nt_tex_opts_t *opts) {
    if (!ctx || !data || size == 0 || !resource_id) {
        return NT_BUILD_ERR_VALIDATION;
    }

    uint64_t rid = nt_hash64_str(resource_id).value;

    int32_t existing = nt_builder_find_entry(ctx, rid);
    if (existing >= 0 && !ctx->force) {
        (void)fprintf(stderr, "ERROR: duplicate resource_id for texture '%s'\n", resource_id);
        return NT_BUILD_ERR_DUPLICATE;
    }

    NtBuildTexMemData *td = (NtBuildTexMemData *)calloc(1, sizeof(NtBuildTexMemData));
    if (!td) {
        return NT_BUILD_ERR_IO;
    }
    td->data = (uint8_t *)malloc(size);
    if (!td->data) {
        free(td);
        return NT_BUILD_ERR_IO;
    }
    memcpy(td->data, data, size);
    td->size = size;
    if (opts) {
        td->opts = *opts;
    } else {
        td->opts.format = NT_TEXTURE_FORMAT_RGBA8;
        td->opts.max_size = 0;
    }

    if (ctx->pending_count >= NT_BUILD_MAX_ASSETS) {
        (void)fprintf(stderr, "ERROR: Asset limit reached (%d max)\n", NT_BUILD_MAX_ASSETS);
        free(td->data);
        free(td);
        return NT_BUILD_ERR_LIMIT;
    }

    if (existing >= 0) {
        nt_builder_free_entry(&ctx->pending[existing]);
        ctx->pending[existing].path = strdup(resource_id);
        ctx->pending[existing].rename_key = NULL;
        ctx->pending[existing].resource_id = rid;
        ctx->pending[existing].kind = NT_BUILD_ASSET_TEXTURE_MEM;
        ctx->pending[existing].data = td;
        return NT_BUILD_OK;
    }

    NtBuildEntry *entry = &ctx->pending[ctx->pending_count++];
    entry->path = strdup(resource_id);
    entry->rename_key = NULL;
    entry->resource_id = rid;
    entry->kind = NT_BUILD_ASSET_TEXTURE_MEM;
    entry->data = td;
    return NT_BUILD_OK;
}

nt_build_result_t nt_builder_add_texture_from_memory(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id) {
    return nt_builder_add_texture_from_memory_ex(ctx, data, size, resource_id, NULL);
}

/* --- Public add_texture_raw --- */

nt_build_result_t nt_builder_add_texture_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const char *resource_id, const nt_tex_opts_t *opts) {
    if (!ctx || !rgba_pixels || width == 0 || height == 0 || !resource_id) {
        return NT_BUILD_ERR_VALIDATION;
    }

    uint64_t rid = nt_hash64_str(resource_id).value;

    int32_t existing = nt_builder_find_entry(ctx, rid);
    if (existing >= 0 && !ctx->force) {
        (void)fprintf(stderr, "ERROR: duplicate resource_id for raw texture '%s'\n", resource_id);
        return NT_BUILD_ERR_DUPLICATE;
    }

    uint32_t data_size = width * height * 4;
    NtBuildTexRawData *td = (NtBuildTexRawData *)calloc(1, sizeof(NtBuildTexRawData));
    if (!td) {
        return NT_BUILD_ERR_IO;
    }
    td->pixels = (uint8_t *)malloc(data_size);
    if (!td->pixels) {
        free(td);
        return NT_BUILD_ERR_IO;
    }
    memcpy(td->pixels, rgba_pixels, data_size);
    td->width = width;
    td->height = height;
    if (opts) {
        td->opts = *opts;
    } else {
        td->opts.format = NT_TEXTURE_FORMAT_RGBA8;
        td->opts.max_size = 0;
    }

    if (ctx->pending_count >= NT_BUILD_MAX_ASSETS) {
        (void)fprintf(stderr, "ERROR: Asset limit reached (%d max)\n", NT_BUILD_MAX_ASSETS);
        free(td->pixels);
        free(td);
        return NT_BUILD_ERR_LIMIT;
    }

    if (existing >= 0) {
        nt_builder_free_entry(&ctx->pending[existing]);
        ctx->pending[existing].path = strdup(resource_id);
        ctx->pending[existing].rename_key = NULL;
        ctx->pending[existing].resource_id = rid;
        ctx->pending[existing].kind = NT_BUILD_ASSET_TEXTURE_RAW;
        ctx->pending[existing].data = td;
        return NT_BUILD_OK;
    }

    NtBuildEntry *entry = &ctx->pending[ctx->pending_count++];
    entry->path = strdup(resource_id);
    entry->rename_key = NULL;
    entry->resource_id = rid;
    entry->kind = NT_BUILD_ASSET_TEXTURE_RAW;
    entry->data = td;
    return NT_BUILD_OK;
}

void nt_builder_set_force(NtBuilderContext *ctx, bool force) {
    if (ctx) {
        ctx->force = force;
    }
}

/* --- Rename: change resource_id key, keep source file --- */

nt_build_result_t nt_builder_rename(NtBuilderContext *ctx, const char *old_path, const char *new_path) {
    if (!ctx || !old_path || !new_path) {
        return NT_BUILD_ERR_VALIDATION;
    }

    uint64_t old_id = nt_builder_path_id(old_path);
    int32_t idx = nt_builder_find_entry(ctx, old_id);
    if (idx < 0) {
        (void)fprintf(stderr, "ERROR: rename: '%s' not found in pending assets\n", old_path);
        return NT_BUILD_ERR_VALIDATION;
    }

    uint64_t new_id = nt_builder_path_id(new_path);

    int32_t collision = nt_builder_find_entry(ctx, new_id);
    if (collision >= 0 && collision != idx) {
        (void)fprintf(stderr, "ERROR: rename: new path '%s' collides with existing '%s'\n", new_path, ctx->pending[collision].path);
        return NT_BUILD_ERR_DUPLICATE;
    }

    ctx->pending[idx].resource_id = new_id;

    free(ctx->pending[idx].rename_key);
    ctx->pending[idx].rename_key = nt_builder_normalize_path(new_path);

    return NT_BUILD_OK;
}
