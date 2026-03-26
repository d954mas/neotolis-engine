/* clang-format off */
#include "nt_builder_internal.h"
#include "cgltf.h"
#include "hash/nt_hash.h"
#include "nt_crc32.h"
#include "time/nt_time.h"
/* Avoid zlib-compat macros (compress, compress2) colliding with struct field names */
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
/* clang-format on */

/* Global hookable handler for NT_BUILD_ASSERT (tests use setjmp/longjmp) */
nt_build_assert_handler_t nt_build_assert_handler = NULL;

/* --- Entry data management --- */

static void nt_builder_free_mesh_data(NtBuildMeshData *md) {
    if (!md) {
        return;
    }
    for (uint32_t s = 0; s < md->stream_count; s++) {
        free((char *)md->layout[s].engine_name);
        free((char *)md->layout[s].gltf_name);
    }
    free(md->mesh_name);
    free(md->file_path);
    free(md);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static NtBuildMeshData *nt_builder_copy_mesh_data(const nt_mesh_opts_t *opts) {
    NtBuildMeshData *md = (NtBuildMeshData *)calloc(1, sizeof(NtBuildMeshData));
    if (!md) {
        return NULL;
    }
    md->stream_count = opts->stream_count;
    md->tangent_mode = opts->tangent_mode;
    md->mesh_index = UINT32_MAX; /* sentinel: not used */
    memcpy(md->layout, opts->layout, opts->stream_count * sizeof(NtStreamLayout));
    for (uint32_t s = 0; s < opts->stream_count; s++) {
        if (opts->layout[s].engine_name) {
            md->layout[s].engine_name = strdup(opts->layout[s].engine_name);
            NT_BUILD_ASSERT(md->layout[s].engine_name && "strdup engine_name failed");
        }
        if (opts->layout[s].gltf_name) {
            md->layout[s].gltf_name = strdup(opts->layout[s].gltf_name);
            NT_BUILD_ASSERT(md->layout[s].gltf_name && "strdup gltf_name failed");
        }
    }
    if (opts->mesh_name) {
        md->mesh_name = strdup(opts->mesh_name);
        NT_BUILD_ASSERT(md->mesh_name && "strdup mesh_name failed");
    }
    if (opts->use_mesh_index) {
        md->mesh_index = opts->mesh_index;
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

static void nt_builder_free_tex_mem_compressed_data(NtBuildTexMemCompressedData *td) {
    if (!td) {
        return;
    }
    free(td->data);
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
    case NT_BUILD_ASSET_TEXTURE_MEM_COMPRESSED:
        nt_builder_free_tex_mem_compressed_data((NtBuildTexMemCompressedData *)entry->data);
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
    if (entry->resolved_owned) {
        free(entry->resolved_data);
    }
    entry->resolved_data = NULL;
    entry->path = NULL;
    entry->rename_key = NULL;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_add_entry(NtBuilderContext *ctx, const char *path, nt_build_asset_kind_t kind, void *data) {
    NT_BUILD_ASSERT(ctx && "ctx is NULL");
    NT_BUILD_ASSERT(path && "path is NULL");

    char *norm_path = nt_builder_normalize_path(path);
    NT_BUILD_ASSERT(norm_path && "normalize_path failed");

    uint64_t resource_id = nt_hash64_str(norm_path).value;

    int32_t existing = nt_builder_find_entry(ctx, resource_id);
    if (existing >= 0) {
        NT_LOG_ERROR("duplicate resource_id 0x%016llX  existing: %s  new:      %s", (unsigned long long)resource_id, ctx->pending[existing].path, norm_path);
        free(norm_path);
        NT_BUILD_ASSERT(0 && "duplicate resource_id in same pack");
    }

    NT_BUILD_ASSERT(ctx->pending_count < NT_BUILD_MAX_ASSETS && "asset limit reached");

    NtBuildEntry *entry = &ctx->pending[ctx->pending_count++];
    entry->path = norm_path;
    entry->rename_key = NULL;
    entry->resource_id = resource_id;
    entry->kind = kind;
    entry->data = data;
    entry->resolved_data = NULL;
    entry->resolved_size = 0;
    entry->resolved_owned = false;
    entry->dedup_original = -1;
}

/* --- Core pack writer --- */

NtBuilderContext *nt_builder_start_pack(const char *output_path) {
    if (!output_path) {
        NT_LOG_ERROR("output_path is NULL");
        return NULL;
    }

    NtBuilderContext *ctx = (NtBuilderContext *)calloc(1, sizeof(NtBuilderContext));
    if (!ctx) {
        NT_LOG_ERROR("Failed to allocate builder context");
        return NULL;
    }

    strncpy(ctx->output_path, output_path, sizeof(ctx->output_path) - 1);
    ctx->output_path[sizeof(ctx->output_path) - 1] = '\0';
    ctx->gzip_estimate = false; /* off by default — enable with set_gzip_estimate */

    NT_LOG_INFO("Starting pack: %s", output_path);
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
            NT_LOG_ERROR("Failed to grow data buffer to %u bytes", new_capacity);
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
        NT_LOG_ERROR("entry limit reached (%d max)", NT_BUILD_MAX_ASSETS);
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

/* --- Metadata accumulation --- */

void nt_builder_add_meta(NtBuilderContext *ctx, uint64_t resource_id, uint64_t kind, const void *data, uint32_t size) {
    NT_BUILD_ASSERT(size <= 256 && "metadata entry exceeds 256 byte limit (D-12)");
    NT_BUILD_ASSERT(ctx->meta_count < NT_BUILD_MAX_META_ENTRIES && "metadata entry limit exceeded");
    NtBuildMetaEntry *m = &ctx->meta_pending[ctx->meta_count++];
    m->resource_id = resource_id;
    m->kind = kind;
    m->size = size;
    memset(m->data, 0, sizeof(m->data));
    memcpy(m->data, data, size);
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
    for (uint32_t i = 0; i < ctx->asset_root_count; i++) {
        free(ctx->asset_roots[i]);
    }
    free(ctx->header_dir);
    free(ctx);
}

/* --- Codegen options --- */

void nt_builder_set_header_dir(NtBuilderContext *ctx, const char *dir) {
    if (!ctx) {
        return;
    }
    free(ctx->header_dir);
    ctx->header_dir = dir ? strdup(dir) : NULL;
}

void nt_builder_set_gzip_estimate(NtBuilderContext *ctx, bool enabled) {
    if (ctx) {
        ctx->gzip_estimate = enabled;
    }
}

void nt_builder_free_pack(NtBuilderContext *ctx) { nt_builder_free_context(ctx); }

/* --- Early dedup: resolve raw bytes and compare opts (Phase 38) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void resolve_entry(NtBuildEntry *pe) {
    switch (pe->kind) {
    case NT_BUILD_ASSET_MESH: {
        NtBuildMeshData *md = (NtBuildMeshData *)pe->data;
        const char *file_path = md->file_path ? md->file_path : pe->path;
        pe->resolved_data = (uint8_t *)nt_builder_read_file(file_path, &pe->resolved_size);
        pe->resolved_owned = true;
        NT_BUILD_ASSERT(pe->resolved_data && "resolve: failed to read mesh file");
        break;
    }
    case NT_BUILD_ASSET_TEXTURE: {
        pe->resolved_data = (uint8_t *)nt_builder_read_file(pe->path, &pe->resolved_size);
        pe->resolved_owned = true;
        NT_BUILD_ASSERT(pe->resolved_data && "resolve: failed to read texture file");
        break;
    }
    case NT_BUILD_ASSET_SHADER: {
        pe->resolved_data = (uint8_t *)nt_builder_read_file(pe->path, &pe->resolved_size);
        pe->resolved_owned = true;
        NT_BUILD_ASSERT(pe->resolved_data && "resolve: failed to read shader file");
        break;
    }
    case NT_BUILD_ASSET_BLOB: {
        NtBuildBlobData *bd = (NtBuildBlobData *)pe->data;
        pe->resolved_data = (uint8_t *)bd->data;
        pe->resolved_size = bd->size;
        pe->resolved_owned = false;
        break;
    }
    case NT_BUILD_ASSET_SCENE_MESH: {
        NtBuildSceneMeshData *smd = (NtBuildSceneMeshData *)pe->data;
        cgltf_data *gltf = (cgltf_data *)smd->scene->_internal;
        if (gltf->buffers_count > 0 && gltf->buffers[0].data != NULL) {
            pe->resolved_data = (uint8_t *)gltf->buffers[0].data;
            pe->resolved_size = (uint32_t)gltf->buffers[0].size;
        } else {
            pe->resolved_data = NULL;
            pe->resolved_size = 0;
        }
        pe->resolved_owned = false;
        break;
    }
    case NT_BUILD_ASSET_TEXTURE_MEM: {
        NtBuildTexMemData *tmd = (NtBuildTexMemData *)pe->data;
        pe->resolved_data = tmd->data;
        pe->resolved_size = tmd->size;
        pe->resolved_owned = false;
        break;
    }
    case NT_BUILD_ASSET_TEXTURE_RAW: {
        NtBuildTexRawData *trd = (NtBuildTexRawData *)pe->data;
        pe->resolved_data = trd->pixels;
        pe->resolved_size = trd->width * trd->height * 4;
        pe->resolved_owned = false;
        break;
    }
    case NT_BUILD_ASSET_TEXTURE_MEM_COMPRESSED: {
        NtBuildTexMemCompressedData *tcd = (NtBuildTexMemCompressedData *)pe->data;
        pe->resolved_data = tcd->data;
        pe->resolved_size = tcd->size;
        pe->resolved_owned = false;
        break;
    }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool opts_equal(const NtBuildEntry *a, const NtBuildEntry *b) {
    if (a->kind != b->kind) {
        return false;
    }

    switch (a->kind) {
    case NT_BUILD_ASSET_TEXTURE:
    case NT_BUILD_ASSET_BLOB:
        return true; /* no opts */

    case NT_BUILD_ASSET_MESH: {
        const NtBuildMeshData *ma = (const NtBuildMeshData *)a->data;
        const NtBuildMeshData *mb = (const NtBuildMeshData *)b->data;
        if (ma->stream_count != mb->stream_count) {
            return false;
        }
        if (ma->tangent_mode != mb->tangent_mode) {
            return false;
        }
        if (ma->mesh_index != mb->mesh_index) {
            return false;
        }
        /* NULL-safe mesh_name comparison */
        if (ma->mesh_name != NULL && mb->mesh_name != NULL) {
            if (strcmp(ma->mesh_name, mb->mesh_name) != 0) {
                return false;
            }
        } else if (ma->mesh_name != mb->mesh_name) {
            return false; /* one NULL, one not */
        }
        for (uint32_t s = 0; s < ma->stream_count; s++) {
            if (ma->layout[s].type != mb->layout[s].type) {
                return false;
            }
            if (ma->layout[s].count != mb->layout[s].count) {
                return false;
            }
            if (ma->layout[s].normalized != mb->layout[s].normalized) {
                return false;
            }
            /* NULL-safe gltf_name comparison */
            const char *ga = ma->layout[s].gltf_name;
            const char *gb = mb->layout[s].gltf_name;
            if (ga != NULL && gb != NULL) {
                if (strcmp(ga, gb) != 0) {
                    return false;
                }
            } else if (ga != gb) {
                return false; /* one NULL, one not */
            }
        }
        return true;
    }

    case NT_BUILD_ASSET_SHADER: {
        const NtBuildShaderData *sa = (const NtBuildShaderData *)a->data;
        const NtBuildShaderData *sb = (const NtBuildShaderData *)b->data;
        return sa->stage == sb->stage;
    }

    case NT_BUILD_ASSET_SCENE_MESH: {
        const NtBuildSceneMeshData *sa = (const NtBuildSceneMeshData *)a->data;
        const NtBuildSceneMeshData *sb = (const NtBuildSceneMeshData *)b->data;
        if (sa->mesh_index != sb->mesh_index) {
            return false;
        }
        if (sa->primitive_index != sb->primitive_index) {
            return false;
        }
        if (sa->stream_count != sb->stream_count) {
            return false;
        }
        if (sa->tangent_mode != sb->tangent_mode) {
            return false;
        }
        for (uint32_t s = 0; s < sa->stream_count; s++) {
            if (sa->layout[s].type != sb->layout[s].type) {
                return false;
            }
            if (sa->layout[s].count != sb->layout[s].count) {
                return false;
            }
            if (sa->layout[s].normalized != sb->layout[s].normalized) {
                return false;
            }
            const char *ga = sa->layout[s].gltf_name;
            const char *gb = sb->layout[s].gltf_name;
            if (ga != NULL && gb != NULL) {
                if (strcmp(ga, gb) != 0) {
                    return false;
                }
            } else if (ga != gb) {
                return false; /* one NULL, one not */
            }
        }
        return true;
    }

    case NT_BUILD_ASSET_TEXTURE_MEM: {
        const NtBuildTexMemData *ta = (const NtBuildTexMemData *)a->data;
        const NtBuildTexMemData *tb = (const NtBuildTexMemData *)b->data;
        return memcmp(&ta->opts, &tb->opts, sizeof(nt_tex_opts_t)) == 0;
    }

    case NT_BUILD_ASSET_TEXTURE_RAW: {
        const NtBuildTexRawData *ta = (const NtBuildTexRawData *)a->data;
        const NtBuildTexRawData *tb = (const NtBuildTexRawData *)b->data;
        if (ta->width != tb->width || ta->height != tb->height) {
            return false;
        }
        return memcmp(&ta->opts, &tb->opts, sizeof(nt_tex_opts_t)) == 0;
    }

    case NT_BUILD_ASSET_TEXTURE_MEM_COMPRESSED: {
        const NtBuildTexMemCompressedData *ta = (const NtBuildTexMemCompressedData *)a->data;
        const NtBuildTexMemCompressedData *tb = (const NtBuildTexMemCompressedData *)b->data;
        if (memcmp(&ta->opts, &tb->opts, sizeof(nt_tex_opts_t)) != 0) {
            return false;
        }
        return ta->compress.mode == tb->compress.mode && ta->compress.quality == tb->compress.quality && ta->compress.rdo_quality == tb->compress.rdo_quality;
    }

    default:
        return false;
    }
}

/* --- Finish: import all deferred entries, write pack --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_finish_pack(NtBuilderContext *ctx) {
    NT_BUILD_ASSERT(ctx && "finish_pack called with NULL context");
    NT_BUILD_ASSERT(ctx->pending_count > 0 && "finish_pack called with no assets added");

    /* Phase 0: Resolve raw source bytes for early dedup comparison (Phase 38) */
    NT_LOG_INFO("Importing %u assets...", ctx->pending_count);
    double t_import_start = nt_time_now();
    double import_times[NT_BUILD_MAX_ASSETS];
    memset(import_times, 0, sizeof(import_times));

    double t_resolve_start = nt_time_now();
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        resolve_entry(&ctx->pending[i]);
    }

    /* Phase 0.5: Early dedup -- O(n^2) comparison of kind + raw bytes + opts (D-05) */
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        NtBuildEntry *pe = &ctx->pending[i];
        if (pe->dedup_original >= 0) {
            continue;
        }
        for (uint32_t j = i + 1; j < ctx->pending_count; j++) {
            NtBuildEntry *candidate = &ctx->pending[j];
            if (candidate->dedup_original >= 0) {
                continue;
            }
            if (candidate->kind != pe->kind) {
                continue;
            }
            /* Compare raw bytes */
            if (pe->resolved_size != candidate->resolved_size) {
                continue;
            }
            if (pe->resolved_size > 0 && memcmp(pe->resolved_data, candidate->resolved_data, pe->resolved_size) != 0) {
                continue;
            }
            /* Bytes match -- check opts */
            if (opts_equal(pe, candidate)) {
                candidate->dedup_original = (int32_t)i;
                ctx->early_dedup_count++;
                NT_LOG_INFO("early dedup: [%u] %s -> [%u] %s", j, candidate->path, i, pe->path);
            } else if (ctx->dedup_warn_count < NT_BUILD_MAX_ASSETS) {
                /* Same source bytes but different opts -- record warning (D-10) */
                ctx->dedup_warn_a[ctx->dedup_warn_count] = i;
                ctx->dedup_warn_b[ctx->dedup_warn_count] = j;
                ctx->dedup_warn_count++;
            }
        }
    }
    double resolve_secs = nt_time_now() - t_resolve_start;

    /* Phase 1: Encode loop -- skip early-deduped entries, register deduped inline */
    double t_encode_start = nt_time_now();
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        NtBuildEntry *pe = &ctx->pending[i];

        if (pe->dedup_original >= 0) {
            /* Early-deduped: create entry pointing to original's data.
             * dedup_original always points to an earlier index (j < i impossible,
             * since we set candidate->dedup_original = i where i < j), so the
             * original's entry at entries[pe->dedup_original] is already populated. */
            uint32_t orig_idx = (uint32_t)pe->dedup_original;
            NtAssetEntry *orig_entry = &ctx->entries[orig_idx];
            NtAssetEntry *dup_entry = &ctx->entries[ctx->entry_count];
            dup_entry->resource_id = pe->resource_id;
            dup_entry->asset_type = orig_entry->asset_type;
            dup_entry->format_version = orig_entry->format_version;
            dup_entry->size = orig_entry->size;
            dup_entry->offset = orig_entry->offset;
            dup_entry->meta_offset = UINT32_MAX; /* no meta for deduped entries */
            dup_entry->_pad = 0;
            ctx->entry_count++;
            import_times[i] = 0.0;

            /* Increment type counter for summary accuracy */
            switch (orig_entry->asset_type) {
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
            default:
                break;
            }
            continue;
        }

        /* Normal import (existing switch on pe->kind) */
        NT_LOG_INFO("  [%u/%u] %s", i + 1, ctx->pending_count, pe->path);
        double t_asset_start = nt_time_now();
        nt_build_result_t ret = NT_BUILD_OK;

        switch (pe->kind) {
        case NT_BUILD_ASSET_MESH: {
            NtBuildMeshData *md = (NtBuildMeshData *)pe->data;
            const char *file_path = md->file_path ? md->file_path : pe->path;
            ret = nt_builder_import_mesh(ctx, file_path, md->layout, md->stream_count, md->tangent_mode, md->mesh_name, md->mesh_index, pe->resource_id);
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
        case NT_BUILD_ASSET_TEXTURE_MEM_COMPRESSED: {
            NtBuildTexMemCompressedData *tcd = (NtBuildTexMemCompressedData *)pe->data;
            ret = nt_builder_import_texture_from_memory_compressed(ctx, tcd->data, tcd->size, pe->resource_id, &tcd->opts, &tcd->compress);
            break;
        }
        }

        import_times[i] = nt_time_now() - t_asset_start;
        NT_BUILD_ASSERT(ret == NT_BUILD_OK && "asset import failed -- see error above");
    }
    NT_BUILD_ASSERT(ctx->entry_count == ctx->pending_count && "entry/pending count mismatch after encode");
    double encode_secs = nt_time_now() - t_encode_start;

    /* Phase 1b: Write meta section to data_buf (appended after asset data).
     * Meta entries grouped by resource_id (D-05), covered by CRC32. */
    uint32_t meta_section_start_databuf = 0; /* data_buf-relative, before header shift */
    if (ctx->meta_count > 0) {
        /* Sort meta_pending by resource_id (insertion sort -- count is small) */
        for (uint32_t i = 1; i < ctx->meta_count; i++) {
            NtBuildMetaEntry key = ctx->meta_pending[i];
            uint32_t j = i;
            while (j > 0 && ctx->meta_pending[j - 1].resource_id > key.resource_id) {
                ctx->meta_pending[j] = ctx->meta_pending[j - 1];
                j--;
            }
            ctx->meta_pending[j] = key;
        }

        /* Align meta section start to 4 bytes */
        meta_section_start_databuf = (ctx->data_size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
        uint32_t meta_section_start = meta_section_start_databuf;
        if (meta_section_start > ctx->data_size) {
            uint8_t zeros[4] = {0};
            NT_BUILD_ASSERT(nt_builder_append_data(ctx, zeros, meta_section_start - ctx->data_size) == NT_BUILD_OK);
        }

        /* Write each meta entry: NtMetaEntryHeader + payload */
        for (uint32_t m = 0; m < ctx->meta_count; m++) {
            NtBuildMetaEntry *me = &ctx->meta_pending[m];
            uint32_t meta_data_offset = ctx->data_size;

            /* Set meta_offset for first entry of each resource_id */
            for (uint32_t a = 0; a < ctx->entry_count; a++) {
                if (ctx->entries[a].resource_id == me->resource_id && ctx->entries[a].meta_offset == 0) {
                    ctx->entries[a].meta_offset = meta_data_offset;
                    break;
                }
            }

            NtMetaEntryHeader mh;
            mh.resource_id = me->resource_id;
            mh.kind = me->kind;
            mh.size = me->size;
            NT_BUILD_ASSERT(nt_builder_append_data(ctx, &mh, (uint32_t)sizeof(NtMetaEntryHeader)) == NT_BUILD_OK);
            NT_BUILD_ASSERT(nt_builder_append_data(ctx, me->data, me->size) == NT_BUILD_OK);
            uint32_t padded_size = (me->size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
            if (padded_size > me->size) {
                uint8_t pad[4] = {0};
                NT_BUILD_ASSERT(nt_builder_append_data(ctx, pad, padded_size - me->size) == NT_BUILD_OK);
            }
        }
    }

    /* Phase 2: Compute layout and write pack file */
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + (ctx->entry_count * sizeof(NtAssetEntry)));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);

    /* Compute gzip sizes BEFORE shifting offsets (entries still data_buf-relative) */
    uint32_t gz_sizes[NT_BUILD_MAX_ASSETS];
    memset(gz_sizes, 0, sizeof(gz_sizes));
    uint32_t total_gz = 0;
    double gzip_secs = 0.0;
    if (ctx->gzip_estimate) {
        double t_gzip_start = nt_time_now();
        /* Find max asset size to allocate one compression buffer */
        uint32_t max_asset_size = 0;
        for (uint32_t i = 0; i < ctx->entry_count; i++) {
            if (ctx->entries[i].size > max_asset_size) {
                max_asset_size = ctx->entries[i].size;
            }
        }
        if (max_asset_size > 0) {
            mz_ulong comp_bound = mz_compressBound((mz_ulong)max_asset_size);
            uint8_t *comp_buf = (uint8_t *)malloc(comp_bound);
            if (comp_buf) {
                for (uint32_t i = 0; i < ctx->entry_count; i++) {
                    mz_ulong comp_len = comp_bound;
                    int rc = mz_compress2(comp_buf, &comp_len, ctx->data_buf + ctx->entries[i].offset, (mz_ulong)ctx->entries[i].size, 6);
                    gz_sizes[i] = (rc == MZ_OK) ? (uint32_t)comp_len : ctx->entries[i].size;
                    total_gz += gz_sizes[i];
                }
                free(comp_buf);
            }
        }
        gzip_secs = nt_time_now() - t_gzip_start;
    }

    /* entry->offset is relative to data_buf start (set in register_asset).
     * Shift to absolute file offset by adding header_size. */
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        ctx->entries[i].offset += header_size;
        if (ctx->entries[i].meta_offset != 0) {
            ctx->entries[i].meta_offset += header_size;
        }
    }

    uint32_t total_size = header_size + ctx->data_size;
    uint32_t checksum = nt_crc32(ctx->data_buf, ctx->data_size);

    NtPackHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = NT_PACK_MAGIC;
    header.meta_count = ctx->meta_count;
    header.meta_offset = (ctx->meta_count > 0) ? meta_section_start_databuf + header_size : 0;
    header.version = NT_PACK_VERSION;
    header.asset_count = (uint16_t)ctx->entry_count;
    header.header_size = header_size;
    header.total_size = total_size;
    header.checksum = checksum;

    double t_write_start = nt_time_now();

    FILE *file = fopen(ctx->output_path, "wb");
    if (!file) {
        NT_LOG_ERROR("Cannot open output file: %s", ctx->output_path);
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

    double write_secs = nt_time_now() - t_write_start;

    if (!write_ok) {
        NT_LOG_ERROR("Failed to write pack file");
        (void)remove(ctx->output_path);
        return NT_BUILD_ERR_IO;
    }

    /* Generate codegen header (.h with ASSET_* constants) */
    nt_build_result_t codegen_result = nt_builder_generate_header(ctx);
    NT_BUILD_ASSERT(codegen_result == NT_BUILD_OK && "codegen header generation failed -- check header_dir exists and is writable");

    /* Enhanced summary */
    double total_secs = nt_time_now() - t_import_start;
    NT_LOG_INFO("Build complete: %s", ctx->output_path);
    NT_LOG_INFO("");

    /* Per-asset table */
    NT_LOG_INFO("  %-4s %-40s %-10s %-24s %-8s %-16s", "#", "Name", "Type", "Size", "Time", "Note");
    NT_LOG_INFO("  %-4s %-40s %-10s %-24s %-8s %-16s", "--", "----", "----", "----", "----", "----");
    static const char *kind_names[] = {"MESH", "TEX", "SHADER", "BLOB", "MESH", "TEX|MEM", "TEX|RAW", "TEX|CMP"};
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        const char *path = (i < ctx->pending_count && ctx->pending[i].path) ? ctx->pending[i].path : "(unknown)";
        const char *rkey = (i < ctx->pending_count) ? ctx->pending[i].rename_key : NULL;
        const char *display = rkey ? rkey : path;
        const char *type_name = "UNKNOWN";
        if (i < ctx->pending_count && (uint32_t)ctx->pending[i].kind < 8) {
            type_name = kind_names[ctx->pending[i].kind];
        }

        /* Format size with gzip */
        char size_str[64];
        uint32_t raw_sz = ctx->entries[i].size;
        uint32_t gz_sz = gz_sizes[i];
        char raw_str[16];
        char gz_str[16];
        nt_format_size(raw_sz, raw_str, sizeof(raw_str));
        if (raw_sz > 0 && gz_sz > 0) {
            nt_format_size(gz_sz, gz_str, sizeof(gz_str));
            uint32_t pct = (gz_sz * 100) / raw_sz;
            (void)snprintf(size_str, sizeof(size_str), "%s (%s gz %u%%)", raw_str, gz_str, pct);
        } else {
            (void)snprintf(size_str, sizeof(size_str), "%s", raw_str);
        }

        /* Note column: early dedup annotation (D-13) */
        char note_str[32] = "";
        char time_str[16];
        if (i < ctx->pending_count && ctx->pending[i].dedup_original >= 0) {
            (void)snprintf(note_str, sizeof(note_str), "dup #%d (early)", ctx->pending[i].dedup_original);
            (void)snprintf(time_str, sizeof(time_str), "--");
        } else {
            (void)snprintf(time_str, sizeof(time_str), "%.2fs", import_times[i]);
        }

        NT_LOG_INFO("  %-4u %-40s %-10s %-24s %-8s %s", i, display, type_name, size_str, time_str, note_str);
    }

    /* Per-type summary */
    NT_LOG_INFO("");
    uint32_t raw_total = ctx->data_size;
    if (ctx->mesh_count > 0) {
        NT_LOG_INFO("  MESH:    %u asset%s", ctx->mesh_count, ctx->mesh_count > 1 ? "s" : "");
    }
    if (ctx->texture_count > 0) {
        NT_LOG_INFO("  TEX:     %u asset%s", ctx->texture_count, ctx->texture_count > 1 ? "s" : "");
    }
    if (ctx->shader_count > 0) {
        NT_LOG_INFO("  SHADER:  %u asset%s", ctx->shader_count, ctx->shader_count > 1 ? "s" : "");
    }
    if (ctx->blob_count > 0) {
        NT_LOG_INFO("  BLOB:    %u asset%s", ctx->blob_count, ctx->blob_count > 1 ? "s" : "");
    }
    if (total_gz > 0 && raw_total > 0) {
        char total_raw_str[16];
        char total_gz_str[16];
        nt_format_size(raw_total, total_raw_str, sizeof(total_raw_str));
        nt_format_size(total_gz, total_gz_str, sizeof(total_gz_str));
        uint32_t total_pct = (total_gz * 100) / raw_total;
        NT_LOG_INFO("  Total:   %s raw -> %s gz (%u%%)", total_raw_str, total_gz_str, total_pct);
    }
    if (ctx->meta_count > 0) {
        NT_LOG_INFO("  META:    %u entries", ctx->meta_count);
    }
    if (ctx->early_dedup_count > 0) {
        NT_LOG_INFO("  Early dedup: %u assets (skipped encode)", ctx->early_dedup_count);
    }
    if (ctx->dedup_count > 0) {
        NT_LOG_INFO("  Late dedup:  %u assets (saved %.1fK)", ctx->dedup_count, (double)ctx->dedup_saved_bytes / 1024.0);
    }

    /* Same-source-different-opts warnings (D-10, D-11) */
    if (ctx->dedup_warn_count > 0) {
        NT_LOG_INFO("");
        NT_LOG_INFO("  WARNINGS: %u same-source-different-opts pairs:", ctx->dedup_warn_count);
        for (uint32_t w = 0; w < ctx->dedup_warn_count; w++) {
            uint32_t ai = ctx->dedup_warn_a[w];
            uint32_t bi = ctx->dedup_warn_b[w];
            NT_LOG_INFO("    [%u] %s  vs  [%u] %s", ai, ctx->pending[ai].path, bi, ctx->pending[bi].path);
        }
    }

    NT_LOG_INFO("");
    if (ctx->gzip_estimate) {
        NT_LOG_INFO("  Timing: resolve %.1fs | encode %.1fs | gzip %.1fs | write %.1fs | total %.1fs", resolve_secs, encode_secs, gzip_secs, write_secs, total_secs);
    } else {
        NT_LOG_INFO("  Timing: resolve %.1fs | encode %.1fs | write %.1fs | total %.1fs (gzip skipped)", resolve_secs, encode_secs, write_secs, total_secs);
    }
    NT_LOG_INFO("  CRC32:  0x%08X", checksum);

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_add_mesh(NtBuilderContext *ctx, const char *path, const nt_mesh_opts_t *opts) {
    NT_BUILD_ASSERT(opts && opts->layout && opts->stream_count > 0 && opts->stream_count <= NT_MESH_MAX_STREAMS && "invalid mesh opts");
    NtBuildMeshData *md = nt_builder_copy_mesh_data(opts);
    NT_BUILD_ASSERT(md && "copy_mesh_data alloc failed");

    /* Build logical path for resource_id:
     *   mesh_name set  -> "path/mesh_name" (or "path/resource_name" if override)
     *   mesh_index set -> "path/index"     (or "path/resource_name" if override)
     *   neither        -> "path" (single-mesh mode)
     */
    char logical_path[1024];
    if (opts->mesh_name != NULL) {
        const char *suffix = opts->resource_name ? opts->resource_name : opts->mesh_name;
        (void)snprintf(logical_path, sizeof(logical_path), "%s/%s", path, suffix);
        md->file_path = strdup(path);
        NT_BUILD_ASSERT(md->file_path && "strdup file_path failed");
    } else if (opts->use_mesh_index) {
        if (opts->resource_name) {
            (void)snprintf(logical_path, sizeof(logical_path), "%s/%s", path, opts->resource_name);
        } else {
            (void)snprintf(logical_path, sizeof(logical_path), "%s/%u", path, opts->mesh_index);
        }
        md->file_path = strdup(path);
        NT_BUILD_ASSERT(md->file_path && "strdup file_path failed");
    } else {
        (void)snprintf(logical_path, sizeof(logical_path), "%s", path);
        /* file_path stays NULL -- use entry->path directly */
    }

    nt_builder_add_entry(ctx, logical_path, NT_BUILD_ASSET_MESH, md);
}

void nt_builder_add_texture(NtBuilderContext *ctx, const char *path) { nt_builder_add_entry(ctx, path, NT_BUILD_ASSET_TEXTURE, NULL); }

void nt_builder_add_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage) {
    NtBuildShaderData *sd = nt_builder_copy_shader_data(stage);
    NT_BUILD_ASSERT(sd && "copy_shader_data alloc failed");
    nt_builder_add_entry(ctx, path, NT_BUILD_ASSET_SHADER, sd);
}

/* --- Public add_blob --- */

void nt_builder_add_blob(NtBuilderContext *ctx, const void *data, uint32_t size, const char *resource_id) {
    NT_BUILD_ASSERT(ctx && data && size > 0 && resource_id && "invalid blob args");

    NtBuildBlobData *bd = (NtBuildBlobData *)calloc(1, sizeof(NtBuildBlobData));
    NT_BUILD_ASSERT(bd && "blob alloc failed");
    bd->data = malloc(size);
    NT_BUILD_ASSERT(bd->data && "blob data alloc failed");
    memcpy(bd->data, data, size);
    bd->size = size;

    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_BLOB, bd);
}

/* --- Public add_texture_from_memory --- */

void nt_builder_add_texture_from_memory_ex(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id, const nt_tex_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && data && size > 0 && resource_id && "invalid texture_from_memory args");

    NtBuildTexMemData *td = (NtBuildTexMemData *)calloc(1, sizeof(NtBuildTexMemData));
    NT_BUILD_ASSERT(td && "tex_mem alloc failed");
    td->data = (uint8_t *)malloc(size);
    NT_BUILD_ASSERT(td->data && "tex_mem data alloc failed");
    memcpy(td->data, data, size);
    td->size = size;
    if (opts) {
        td->opts = *opts;
    } else {
        td->opts.format = NT_TEXTURE_FORMAT_RGBA8;
        td->opts.max_size = 0;
    }

    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_TEXTURE_MEM, td);
}

void nt_builder_add_texture_from_memory(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id) {
    nt_builder_add_texture_from_memory_ex(ctx, data, size, resource_id, NULL);
}

/* --- Public add_texture_raw --- */

void nt_builder_add_texture_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const char *resource_id, const nt_tex_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && rgba_pixels && width > 0 && height > 0 && resource_id && "invalid texture_raw args");

    uint32_t data_size = width * height * 4;
    NtBuildTexRawData *td = (NtBuildTexRawData *)calloc(1, sizeof(NtBuildTexRawData));
    NT_BUILD_ASSERT(td && "tex_raw alloc failed");
    td->pixels = (uint8_t *)malloc(data_size);
    NT_BUILD_ASSERT(td->pixels && "tex_raw pixels alloc failed");
    memcpy(td->pixels, rgba_pixels, data_size);
    td->width = width;
    td->height = height;
    if (opts) {
        td->opts = *opts;
    } else {
        td->opts.format = NT_TEXTURE_FORMAT_RGBA8;
        td->opts.max_size = 0;
    }

    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_TEXTURE_RAW, td);
}

/* --- Public add_texture_from_memory_compressed --- */

void nt_builder_add_texture_from_memory_compressed(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id, const nt_tex_opts_t *opts,
                                                   const nt_tex_compress_opts_t *compress_opts) {
    NT_BUILD_ASSERT(ctx && data && size > 0 && resource_id && "invalid texture_from_memory_compressed args");

    /* NULL compress_opts = fall through to uncompressed path */
    if (!compress_opts) {
        nt_builder_add_texture_from_memory_ex(ctx, data, size, resource_id, opts);
        return;
    }

    NtBuildTexMemCompressedData *td = (NtBuildTexMemCompressedData *)calloc(1, sizeof(NtBuildTexMemCompressedData));
    NT_BUILD_ASSERT(td && "tex_compressed alloc failed");
    td->data = (uint8_t *)malloc(size);
    NT_BUILD_ASSERT(td->data && "tex_compressed data alloc failed");
    memcpy(td->data, data, size);
    td->size = size;
    td->compress = *compress_opts;
    if (opts) {
        td->opts = *opts;
    } else {
        td->opts.format = NT_TEXTURE_FORMAT_RGBA8;
        td->opts.max_size = 0;
    }

    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_TEXTURE_MEM_COMPRESSED, td);
}

nt_build_result_t nt_builder_add_asset_root(NtBuilderContext *ctx, const char *path) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }
    if (ctx->asset_root_count >= NT_BUILD_MAX_ASSET_ROOTS) {
        NT_LOG_ERROR("asset root limit reached (%d max)", NT_BUILD_MAX_ASSET_ROOTS);
        return NT_BUILD_ERR_LIMIT;
    }
    char *normalized = nt_builder_normalize_path(path);
    if (!normalized) {
        return NT_BUILD_ERR_IO;
    }
    ctx->asset_roots[ctx->asset_root_count++] = normalized;
    NT_LOG_INFO("asset root [%u]: %s", ctx->asset_root_count - 1, normalized);
    return NT_BUILD_OK;
}

/* --- Rename: change resource_id key, keep source file --- */

void nt_builder_rename(NtBuilderContext *ctx, const char *old_path, const char *new_path) {
    NT_BUILD_ASSERT(ctx && old_path && new_path && "invalid rename args");

    uint64_t old_id = nt_builder_path_id(old_path);
    int32_t idx = nt_builder_find_entry(ctx, old_id);
    NT_BUILD_ASSERT(idx >= 0 && "rename: old_path not found in pending assets");

    uint64_t new_id = nt_builder_path_id(new_path);

    int32_t collision = nt_builder_find_entry(ctx, new_id);
    NT_BUILD_ASSERT((collision < 0 || collision == idx) && "rename: new_path collides with existing");

    ctx->pending[idx].resource_id = new_id;

    free(ctx->pending[idx].rename_key);
    ctx->pending[idx].rename_key = nt_builder_normalize_path(new_path);
}
