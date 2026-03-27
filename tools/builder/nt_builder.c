/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_blob_format.h"
#include "nt_crc32.h"
#include "time/nt_time.h"
/* Avoid zlib-compat macros (compress, compress2) colliding with struct field names */
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
/* clang-format on */

/* Global hookable handler for NT_BUILD_ASSERT (tests use setjmp/longjmp) */
nt_build_assert_handler_t nt_build_assert_handler = NULL;

/* --- Entry data management --- */

static void nt_builder_free_entry_data(NtBuildEntry *entry) {
    if (!entry->data) {
        return;
    }
    if (entry->kind == NT_BUILD_ASSET_TEXTURE) {
        NtBuildTextureData *td = (NtBuildTextureData *)entry->data;
        free(td->source_data);
    }
    /* TEXTURE -> NtBuildTextureData*, SHADER -> NtBuildShaderData*, others -> NULL */
    free(entry->data);
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
    free(entry->decoded_data);
    entry->path = NULL;
    entry->rename_key = NULL;
    entry->decoded_data = NULL;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_add_entry(NtBuilderContext *ctx, const char *path, nt_build_asset_kind_t kind, void *data, uint8_t *decoded_data, uint32_t decoded_size, uint64_t decoded_hash) {
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
    entry->decoded_data = decoded_data;
    entry->decoded_size = decoded_size;
    entry->decoded_hash = decoded_hash;
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
    ctx->gzip_estimate = false; /* off by default -- enable with set_gzip_estimate */

    NT_LOG_INFO("Starting pack: %s", output_path);
    return ctx;
}

/* --- Data accumulation (used during finish_pack encode phase) --- */

nt_build_result_t nt_builder_append_data(NtBuilderContext *ctx, const void *data, uint32_t size) {
    if (!ctx || !data || size == 0) {
        return NT_BUILD_ERR_VALIDATION;
    }

    while (ctx->data_size + size > ctx->data_capacity) {
        uint32_t new_capacity = ctx->data_capacity > 0 ? ctx->data_capacity * 2 : NT_BUILD_INITIAL_CAPACITY;
        uint8_t *new_buf = (uint8_t *)realloc(ctx->data_buf, new_capacity);
        NT_BUILD_ASSERT(new_buf && "append_data: realloc failed");
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
        /* Duplicate found -- rewind data_buf, point to existing data */
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

/* Forward declaration for texture re-decode (defined below make_texture_data) */
static uint8_t *nt_builder_redecode_texture(const NtBuildEntry *pe);

/* --- Early dedup: compare decoded_data + encoding opts --- */

static bool opts_equal(const NtBuildEntry *a, const NtBuildEntry *b) {
    if (a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
    case NT_BUILD_ASSET_TEXTURE: {
        const NtBuildTextureData *ta = (const NtBuildTextureData *)a->data;
        const NtBuildTextureData *tb = (const NtBuildTextureData *)b->data;
        /* max_size is NOT compared here — it's applied during decode (resize),
         * so different max_size → different decoded pixels → different hash.
         * Only encode-affecting options need comparison. */
        if (ta->opts.format != tb->opts.format) {
            return false;
        }
        /* Compare compression path */
        if (ta->has_compress != tb->has_compress) {
            return false;
        }
        if (ta->has_compress) {
            if (ta->compress.mode != tb->compress.mode || ta->compress.quality != tb->compress.quality || ta->compress.rdo_quality != tb->compress.rdo_quality) {
                return false;
            }
        }
        return true;
    }
    case NT_BUILD_ASSET_SHADER: {
        const NtBuildShaderData *sa = (const NtBuildShaderData *)a->data;
        const NtBuildShaderData *sb = (const NtBuildShaderData *)b->data;
        return sa->stage == sb->stage;
    }
    case NT_BUILD_ASSET_MESH:
    case NT_BUILD_ASSET_BLOB:
        return true; /* no encoding opts -- everything is in decoded_data */
    }
    return false;
}

/* --- Finish: dedup decoded entries, encode non-duplicates, write pack --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_finish_pack(NtBuilderContext *ctx) {
    NT_BUILD_ASSERT(ctx && "finish_pack called with NULL context");
    NT_BUILD_ASSERT(ctx->pending_count > 0 && "finish_pack called with no assets added");

    double t_encode_start = nt_time_now();
    double encode_times[NT_BUILD_MAX_ASSETS];
    memset(encode_times, 0, sizeof(encode_times));

    /* Phase 0: Early dedup on hash + size + opts */
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
            if (pe->decoded_size != candidate->decoded_size) {
                continue;
            }
            if (pe->decoded_hash != candidate->decoded_hash) {
                continue;
            }
            /* Hash match -- verify with memcmp */
            if (pe->decoded_data && candidate->decoded_data) {
                if (memcmp(pe->decoded_data, candidate->decoded_data, pe->decoded_size) != 0) {
                    continue;
                }
            } else if (pe->kind == NT_BUILD_ASSET_TEXTURE) {
                /* Re-decode textures for verification when decoded_data was freed */
                uint8_t *a_data = pe->decoded_data;
                uint8_t *b_data = candidate->decoded_data;
                bool a_owned = false;
                bool b_owned = false;
                if (!a_data) {
                    a_data = nt_builder_redecode_texture(pe);
                    a_owned = true;
                }
                if (!b_data) {
                    b_data = nt_builder_redecode_texture(candidate);
                    b_owned = true;
                }
                bool match = (a_data && b_data) ? (memcmp(a_data, b_data, pe->decoded_size) == 0) : true;
                if (a_owned) {
                    free(a_data);
                }
                if (b_owned) {
                    free(b_data);
                }
                if (!match) {
                    continue;
                }
            } else {
                /* Non-texture types must always have decoded_data for memcmp verify */
                NT_BUILD_ASSERT(0 && "hash match but no decoded_data on non-texture entry");
            }
            if (opts_equal(pe, candidate)) {
                candidate->dedup_original = (int32_t)i;
                ctx->early_dedup_count++;
                NT_LOG_INFO("early dedup: [%u] %s -> [%u] %s", j, candidate->path, i, pe->path);
            } else if (ctx->dedup_warn_count < NT_BUILD_MAX_ASSETS) {
                ctx->dedup_warn_a[ctx->dedup_warn_count] = i;
                ctx->dedup_warn_b[ctx->dedup_warn_count] = j;
                ctx->dedup_warn_count++;
            }
        }
    }

    /* Phase 1: Encode non-deduped entries */
    NT_LOG_INFO("Encoding %u assets (%u early-deduped)...", ctx->pending_count, ctx->early_dedup_count);

    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        NtBuildEntry *pe = &ctx->pending[i];

        /* Skip early-deduped entries (registered after originals) */
        if (pe->dedup_original >= 0) {
            continue;
        }

        NT_LOG_INFO("  [%u/%u] %s", i + 1, ctx->pending_count, pe->path);
        double t_asset_start = nt_time_now();
        nt_build_result_t ret = NT_BUILD_OK;

        switch (pe->kind) {
        case NT_BUILD_ASSET_MESH:
        case NT_BUILD_ASSET_BLOB: {
            /* Decoded data is already the final binary -- just append + register */
            ret = nt_builder_append_data(ctx, pe->decoded_data, pe->decoded_size);
            if (ret == NT_BUILD_OK) {
                nt_asset_type_t asset_type = (pe->kind == NT_BUILD_ASSET_MESH) ? NT_ASSET_MESH : NT_ASSET_BLOB;
                uint16_t version = (pe->kind == NT_BUILD_ASSET_MESH) ? NT_MESH_VERSION : NT_BLOB_VERSION;
                ret = nt_builder_register_asset(ctx, pe->resource_id, asset_type, version, pe->decoded_size);
            }
            break;
        }
        case NT_BUILD_ASSET_TEXTURE: {
            NtBuildTextureData *td = (NtBuildTextureData *)pe->data;
            uint8_t *pixels = pe->decoded_data; /* non-NULL for raw textures */
            bool pixels_owned = false;

            if (!pixels) {
                /* Re-decode from source */
                uint32_t rw = 0;
                uint32_t rh = 0;
                if (td->source_data) {
                    /* Memory source -- re-decode from stored encoded bytes */
                    nt_build_result_t dr = nt_builder_decode_texture(td->source_data, td->source_size, &td->opts, &pixels, &rw, &rh);
                    NT_BUILD_ASSERT(dr == NT_BUILD_OK && "encode: re-decode texture from memory failed");
                } else {
                    /* File source -- re-read from path */
                    uint32_t fsize = 0;
                    uint8_t *fdata = (uint8_t *)nt_builder_read_file(pe->path, &fsize);
                    NT_BUILD_ASSERT(fdata && "encode: re-read texture file failed");
                    nt_build_result_t dr = nt_builder_decode_texture(fdata, fsize, &td->opts, &pixels, &rw, &rh);
                    free(fdata);
                    NT_BUILD_ASSERT(dr == NT_BUILD_OK && "encode: re-decode texture from file failed");
                }
                pixels_owned = true;
            }

            if (td->has_compress) {
                ret = nt_builder_encode_texture_compressed(ctx, pixels, td->width, td->height, pe->resource_id, &td->opts, &td->compress);
            } else {
                ret = nt_builder_encode_texture(ctx, pixels, td->width, td->height, pe->resource_id, &td->opts);
            }

            if (pixels_owned) {
                free(pixels);
            }
            break;
        }
        case NT_BUILD_ASSET_SHADER: {
            NtBuildShaderData *sd = (NtBuildShaderData *)pe->data;
            ret = nt_builder_encode_shader(ctx, pe->decoded_data, pe->decoded_size, sd->stage, pe->resource_id);
            break;
        }
        }

        encode_times[i] = nt_time_now() - t_asset_start;
        NT_BUILD_ASSERT(ret == NT_BUILD_OK && "asset encode failed -- see error above");

        /* Per-type counters */
        switch (pe->kind) {
        case NT_BUILD_ASSET_MESH:
            ctx->mesh_count++;
            break;
        case NT_BUILD_ASSET_TEXTURE:
            ctx->texture_count++;
            break;
        case NT_BUILD_ASSET_SHADER:
            ctx->shader_count++;
            break;
        case NT_BUILD_ASSET_BLOB:
            ctx->blob_count++;
            break;
        }
    }

    /* Register early-deduped entries (copy offset/size from original) */
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        NtBuildEntry *pe = &ctx->pending[i];
        if (pe->dedup_original < 0) {
            continue;
        }

        uint32_t orig_idx = (uint32_t)pe->dedup_original;
        /* Find the registered asset entry for the original */
        uint64_t orig_rid = ctx->pending[orig_idx].resource_id;
        bool found = false;
        for (uint32_t ei = 0; ei < ctx->entry_count; ei++) {
            if (ctx->entries[ei].resource_id == orig_rid) {
                NT_BUILD_ASSERT(ctx->entry_count < NT_BUILD_MAX_ASSETS && "entry limit reached");
                NtAssetEntry *dup = &ctx->entries[ctx->entry_count++];
                *dup = ctx->entries[ei]; /* copy offset, size, type, version */
                dup->resource_id = pe->resource_id;
                found = true;
                break;
            }
        }
        NT_BUILD_ASSERT(found && "early dedup: original entry not found in registry");

        /* Per-type counters for deduped entries */
        switch (pe->kind) {
        case NT_BUILD_ASSET_MESH:
            ctx->mesh_count++;
            break;
        case NT_BUILD_ASSET_TEXTURE:
            ctx->texture_count++;
            break;
        case NT_BUILD_ASSET_SHADER:
            ctx->shader_count++;
            break;
        case NT_BUILD_ASSET_BLOB:
            ctx->blob_count++;
            break;
        }
    }

    NT_BUILD_ASSERT(ctx->entry_count == ctx->pending_count && "entry/pending count mismatch after encode+dedup registration");

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
    NT_BUILD_ASSERT(file && "finish_pack: cannot open output file");

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
        (void)remove(ctx->output_path);
        NT_BUILD_ASSERT(0 && "finish_pack: failed to write pack file");
    }

    /* Generate codegen header (.h with ASSET_* constants) */
    nt_build_result_t codegen_result = nt_builder_generate_header(ctx);
    NT_BUILD_ASSERT(codegen_result == NT_BUILD_OK && "codegen header generation failed -- check header_dir exists and is writable");

    /* Enhanced summary */
    double total_secs = nt_time_now() - t_encode_start;
    NT_LOG_INFO("Build complete: %s", ctx->output_path);
    NT_LOG_INFO("");

    /* Per-asset table */
    NT_LOG_INFO("  %-4s %-40s %-10s %-24s %-8s", "#", "Name", "Type", "Size", "Time");
    NT_LOG_INFO("  %-4s %-40s %-10s %-24s %-8s", "--", "----", "----", "----", "----");
    static const char *kind_names[] = {"MESH", "TEX", "SHADER", "BLOB"};
    for (uint32_t i = 0; i < ctx->entry_count; i++) {
        const char *path = (i < ctx->pending_count && ctx->pending[i].path) ? ctx->pending[i].path : "(unknown)";
        const char *rkey = (i < ctx->pending_count) ? ctx->pending[i].rename_key : NULL;
        const char *display = rkey ? rkey : path;
        const char *type_name = "UNKNOWN";
        if (i < ctx->pending_count && (uint32_t)ctx->pending[i].kind < 4) {
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

        NT_LOG_INFO("  %-4u %-40s %-10s %-24s %.2fs", i, display, type_name, size_str, encode_times[i]);
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
        NT_LOG_INFO("  Early dedup: %u assets skipped", ctx->early_dedup_count);
    }
    if (ctx->dedup_warn_count > 0) {
        NT_LOG_WARN("  Same data, different opts (%u pairs):", ctx->dedup_warn_count);
        for (uint32_t w = 0; w < ctx->dedup_warn_count; w++) {
            NT_LOG_WARN("    %s  vs  %s", ctx->pending[ctx->dedup_warn_a[w]].path, ctx->pending[ctx->dedup_warn_b[w]].path);
        }
    }
    if (ctx->dedup_count > 0) {
        NT_LOG_INFO("  Dedup:   %u assets (saved %.1fK)", ctx->dedup_count, (double)ctx->dedup_saved_bytes / 1024.0);
    }
    NT_LOG_INFO("");
    if (ctx->gzip_estimate) {
        NT_LOG_INFO("  Timing: encode %.1fs | gzip %.1fs | write %.1fs | total %.1fs", encode_secs, gzip_secs, write_secs, total_secs);
    } else {
        NT_LOG_INFO("  Timing: encode %.1fs | write %.1fs | total %.1fs (gzip skipped)", encode_secs, write_secs, total_secs);
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

/* --- Texture data helper --- */

/* Re-decode a texture entry for verification. Returns malloc'd RGBA buffer. Caller frees. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint8_t *nt_builder_redecode_texture(const NtBuildEntry *pe) {
    const NtBuildTextureData *td = (const NtBuildTextureData *)pe->data;
    uint8_t *pixels = NULL;
    uint32_t w = 0;
    uint32_t h = 0;
    if (td->source_data) {
        nt_build_result_t r = nt_builder_decode_texture(td->source_data, td->source_size, &td->opts, &pixels, &w, &h);
        NT_BUILD_ASSERT(r == NT_BUILD_OK && "redecode_texture: decode from memory failed");
    } else {
        uint32_t fsize = 0;
        uint8_t *fdata = (uint8_t *)nt_builder_read_file(pe->path, &fsize);
        NT_BUILD_ASSERT(fdata && "redecode_texture: re-read file failed");
        nt_build_result_t r = nt_builder_decode_texture(fdata, fsize, &td->opts, &pixels, &w, &h);
        free(fdata);
        NT_BUILD_ASSERT(r == NT_BUILD_OK && "redecode_texture: decode from file failed");
    }
    return pixels;
}

static NtBuildTextureData *make_texture_data(uint32_t w, uint32_t h, const nt_tex_opts_t *opts) {
    NtBuildTextureData *td = (NtBuildTextureData *)calloc(1, sizeof(NtBuildTextureData));
    NT_BUILD_ASSERT(td && "texture data alloc failed");
    td->width = w;
    td->height = h;
    if (opts) {
        td->opts = *opts;
        td->opts.compress = NULL; /* don't store dangling pointer */
        if (opts->compress) {
            td->has_compress = true;
            td->compress = *opts->compress;
        }
    } else {
        td->opts.format = NT_TEXTURE_FORMAT_RGBA8;
    }
    return td;
}

/* --- Public add_* (eager decode) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_add_mesh(NtBuilderContext *ctx, const char *path, const nt_mesh_opts_t *opts) {
    NT_BUILD_ASSERT(opts && opts->layout && opts->stream_count > 0 && opts->stream_count <= NT_MESH_MAX_STREAMS && "invalid mesh opts");

    /* Build logical path for resource_id:
     *   mesh_name set  -> "path/mesh_name" (or "path/resource_name" if override)
     *   mesh_index set -> "path/index"     (or "path/resource_name" if override)
     *   neither        -> "path" (single-mesh mode)
     */
    char logical_path[1024];
    const char *file_path = path; /* actual file path for decode */

    if (opts->mesh_name != NULL) {
        const char *suffix = opts->resource_name ? opts->resource_name : opts->mesh_name;
        (void)snprintf(logical_path, sizeof(logical_path), "%s/%s", path, suffix);
    } else if (opts->use_mesh_index) {
        if (opts->resource_name) {
            (void)snprintf(logical_path, sizeof(logical_path), "%s/%s", path, opts->resource_name);
        } else {
            (void)snprintf(logical_path, sizeof(logical_path), "%s/%u", path, opts->mesh_index);
        }
    } else {
        (void)snprintf(logical_path, sizeof(logical_path), "%s", path);
    }

    /* Resolve actual file path via asset roots */
    char *resolved_path = nt_builder_find_file(file_path, NULL, ctx);
    const char *decode_path = resolved_path ? resolved_path : file_path;

    const char *mesh_name = opts->mesh_name;
    uint32_t mesh_index = (opts->use_mesh_index) ? opts->mesh_index : UINT32_MAX;

    uint8_t *mesh_data = NULL;
    uint32_t mesh_size = 0;
    nt_build_result_t r = nt_builder_decode_mesh(decode_path, opts->layout, opts->stream_count, opts->tangent_mode, mesh_name, mesh_index, &mesh_data, &mesh_size);
    free(resolved_path);
    NT_BUILD_ASSERT(r == NT_BUILD_OK && "add_mesh: decode failed");

    uint64_t hash = nt_hash64(mesh_data, mesh_size).value;
    nt_builder_add_entry(ctx, logical_path, NT_BUILD_ASSET_MESH, NULL, mesh_data, mesh_size, hash);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_add_texture(NtBuilderContext *ctx, const char *path, const nt_tex_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && path && "invalid add_texture args");

    /* Resolve actual file path via asset roots */
    char *resolved_path = nt_builder_find_file(path, NULL, ctx);
    const char *read_path = resolved_path ? resolved_path : path;

    /* Read file */
    uint32_t file_size = 0;
    uint8_t *file_data = (uint8_t *)nt_builder_read_file(read_path, &file_size);
    free(resolved_path);
    NT_BUILD_ASSERT(file_data && "add_texture: failed to read file");

    /* Decode to RGBA */
    uint8_t *pixels = NULL;
    uint32_t w = 0;
    uint32_t h = 0;
    nt_build_result_t r = nt_builder_decode_texture(file_data, file_size, opts, &pixels, &w, &h);
    free(file_data);
    NT_BUILD_ASSERT(r == NT_BUILD_OK && "add_texture: decode failed");

    /* Hash decoded pixels, then free -- will re-read from file at encode time */
    uint64_t hash = nt_hash64(pixels, w * h * 4).value;
    free(pixels);

    NtBuildTextureData *td = make_texture_data(w, h, opts);
    /* td->source_data = NULL, td->source_size = 0 (re-read from path) */
    nt_builder_add_entry(ctx, path, NT_BUILD_ASSET_TEXTURE, td, NULL, w * h * 4, hash);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_add_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage) {
    NT_BUILD_ASSERT(ctx && path && "invalid add_shader args");

    /* Resolve actual file path via asset roots */
    char *resolved_path = nt_builder_find_file(path, NULL, ctx);
    const char *read_path = resolved_path ? resolved_path : path;

    uint32_t file_size = 0;
    char *source = nt_builder_read_file(read_path, &file_size);
    NT_BUILD_ASSERT(source && "add_shader: failed to read file");

    uint32_t resolved_len = 0;
    char *resolved = nt_builder_resolve_includes(source, file_size, read_path, ctx, &resolved_len);
    free(source);
    free(resolved_path);
    NT_BUILD_ASSERT(resolved && "add_shader: include resolution failed");

    NtBuildShaderData *sd = (NtBuildShaderData *)calloc(1, sizeof(NtBuildShaderData));
    NT_BUILD_ASSERT(sd && "add_shader: alloc failed");
    sd->stage = stage;

    uint64_t hash = nt_hash64(resolved, resolved_len).value;
    nt_builder_add_entry(ctx, path, NT_BUILD_ASSET_SHADER, sd, (uint8_t *)resolved, resolved_len, hash);
}

/* --- Public add_blob --- */

void nt_builder_add_blob(NtBuilderContext *ctx, const void *data, uint32_t size, const char *resource_id) {
    NT_BUILD_ASSERT(ctx && data && size > 0 && resource_id && "invalid blob args");

    /* Build blob asset format: NtBlobAssetHeader + raw data */
    NtBlobAssetHeader blob_hdr;
    memset(&blob_hdr, 0, sizeof(blob_hdr));
    blob_hdr.magic = NT_BLOB_MAGIC;
    blob_hdr.version = NT_BLOB_VERSION;
    blob_hdr._pad = 0;

    uint32_t total_size = (uint32_t)sizeof(NtBlobAssetHeader) + size;
    uint8_t *copy = (uint8_t *)malloc(total_size);
    NT_BUILD_ASSERT(copy && "add_blob: alloc failed");
    memcpy(copy, &blob_hdr, sizeof(NtBlobAssetHeader));
    memcpy(copy + sizeof(NtBlobAssetHeader), data, size);

    uint64_t hash = nt_hash64(copy, total_size).value;
    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_BLOB, NULL, copy, total_size, hash);
}

/* --- Public add_texture_from_memory --- */

void nt_builder_add_texture_from_memory(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id, const nt_tex_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && data && size > 0 && resource_id && "invalid texture_from_memory args");

    uint8_t *pixels = NULL;
    uint32_t w = 0;
    uint32_t h = 0;
    nt_build_result_t r = nt_builder_decode_texture(data, size, opts, &pixels, &w, &h);
    NT_BUILD_ASSERT(r == NT_BUILD_OK && "add_texture_from_memory: decode failed");

    /* Hash decoded pixels, then free -- deep copy source for re-decode at encode time */
    uint64_t hash = nt_hash64(pixels, w * h * 4).value;
    free(pixels);

    NtBuildTextureData *td = make_texture_data(w, h, opts);
    td->source_data = (uint8_t *)malloc(size);
    NT_BUILD_ASSERT(td->source_data && "add_texture_from_memory: source copy alloc failed");
    memcpy(td->source_data, data, size);
    td->source_size = size;

    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_TEXTURE, td, NULL, w * h * 4, hash);
}

/* --- Public add_texture_raw --- */

void nt_builder_add_texture_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const char *resource_id, const nt_tex_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && rgba_pixels && width > 0 && height > 0 && resource_id && "invalid texture_raw args");

    uint8_t *pixels = NULL;
    uint32_t w = 0;
    uint32_t h = 0;
    nt_build_result_t r = nt_builder_decode_texture_raw(rgba_pixels, width, height, opts, &pixels, &w, &h);
    NT_BUILD_ASSERT(r == NT_BUILD_OK && "add_texture_raw: decode failed");

    /* Hash and keep decoded_data -- can't re-derive from source */
    uint64_t hash = nt_hash64(pixels, w * h * 4).value;
    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_TEXTURE, make_texture_data(w, h, opts), pixels, w * h * 4, hash);
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
    NT_BUILD_ASSERT(normalized && "asset root path normalize failed");
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
