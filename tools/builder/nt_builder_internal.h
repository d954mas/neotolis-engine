#ifndef NT_BUILDER_INTERNAL_H
#define NT_BUILDER_INTERNAL_H

#include "log/nt_log.h"
#include "nt_builder.h"
#include "nt_pack_format.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NT_BUILD_ASSERT is defined in nt_builder.h (public, usable by game build scripts) */

/* Initial data buffer capacity (1 MB, doubles on overflow) */
#define NT_BUILD_INITIAL_CAPACITY (1024 * 1024)

/* Asset type tag -- 5 unified kinds (source-agnostic) */
typedef enum {
    NT_BUILD_ASSET_MESH = 0,
    NT_BUILD_ASSET_TEXTURE = 1,
    NT_BUILD_ASSET_SHADER = 2,
    NT_BUILD_ASSET_BLOB = 3,
    NT_BUILD_ASSET_FONT = 4,
    NT_BUILD_ASSET_ATLAS = 5,
} nt_build_asset_kind_t;

/* Type-specific data for shader entries */
typedef struct {
    nt_build_shader_stage_t stage;
} NtBuildShaderData;

/* Texture decode options -- stored alongside decoded RGBA pixels */
typedef struct {
    uint32_t width;
    uint32_t height;
    nt_tex_opts_t opts;              /* format + resize (compress ptr zeroed, use has_compress) */
    nt_tex_compress_opts_t compress; /* Basis compression settings */
    bool has_compress;               /* true = use Basis path, false = raw path */
    uint8_t *source_data;            /* deep-copied encoded image bytes for re-decode (owned, NULL if from file) */
    uint32_t source_size;            /* 0 if from file */
    char *source_path;               /* resolved file path for re-read (owned, NULL if from memory) */
} NtBuildTextureData;

/* Type-specific data for font entries */
typedef struct {
    char *charset; /* deep copy of opts.charset (owned) */
} NtBuildFontData;

/* Input sprite for atlas packing (populated during atlas_add_*) */
typedef struct {
    uint8_t *rgba;         /* decoded RGBA pixels (owned, heap) */
    uint32_t width;        /* decoded image width */
    uint32_t height;       /* decoded image height */
    char *name;            /* region name for hash (owned, heap) */
    float origin_x;        /* default 0.5 */
    float origin_y;        /* default 0.5 */
    uint64_t decoded_hash; /* xxh64 of decoded RGBA pixels */
} NtAtlasSpriteInput;

/* Atlas build state (active between begin_atlas / end_atlas) */
typedef struct {
    char *name;                      /* atlas name for resource_id (owned) */
    nt_atlas_opts_t opts;            /* copy of user opts (compress ptr zeroed, use has_compress) */
    nt_tex_compress_opts_t compress; /* Basis compression settings */
    bool has_compress;               /* true = use Basis path */
    NtAtlasSpriteInput *sprites;     /* dynamic array (heap) */
    uint32_t sprite_count;
    uint32_t sprite_capacity;
} NtBuildAtlasState;

/* Metadata accumulation limit (Phase 37) */
#ifndef NT_BUILD_MAX_META_ENTRIES
#define NT_BUILD_MAX_META_ENTRIES (NT_BUILD_MAX_ASSETS * 2)
#endif

/* Builder-side meta entry for accumulation (max 256 bytes payload per D-12) */
typedef struct {
    uint64_t resource_id;
    uint64_t kind;
    uint32_t size;
    uint8_t data[256];
} NtBuildMetaEntry;

/* Deferred asset entry -- stored during add_*, processed in finish_pack */
typedef struct {
    char *path;                 /* source path for logging (owned, heap) */
    char *rename_key;           /* renamed key path (owned, heap, NULL if not renamed) */
    uint64_t resource_id;       /* nt_hash64 value */
    nt_build_asset_kind_t kind; /* MESH / TEXTURE / SHADER / BLOB */
    void *data;                 /* NtBuildTextureData* / NtBuildShaderData* / NULL (owned, heap) */

    /* Decoded intermediate representation (eager, populated at add_* time) */
    uint8_t *decoded_data; /* NULL for file/memory textures (freed after hash) */
    uint32_t decoded_size; /* always set (RGBA size for textures even if freed) */
    uint64_t decoded_hash; /* xxh64 of decoded_data, computed at add_* time */

    /* Early dedup: index of original entry, -1 if not a duplicate */
    int32_t dedup_original;
} NtBuildEntry;

/* Per-asset encode result -- produced by workers, consumed by sequential assembly */
typedef struct {
    uint8_t *data;           /* encoded bytes (malloc'd by encode function, freed after assembly) */
    uint32_t size;           /* encoded byte count */
    nt_asset_type_t type;    /* asset type for register_asset (NT_ASSET_MESH, NT_ASSET_TEXTURE, etc.) */
    uint16_t format_version; /* format version for register_asset */
    bool from_cache;         /* true = loaded from cache, false = freshly encoded */
    double encode_secs;      /* wall time for this asset's encode (set by worker or single-threaded loop) */
} NtEncodeResult;

/* Shared state between main thread and encode workers (per D-08) */
typedef struct {
    /* Work items: indices into pending[] that need encoding (cache misses, non-deduped, non-shader) */
    uint32_t *work_indices;
    uint32_t work_count;
    _Atomic uint32_t next_work;  /* atomic: next index to claim */
    _Atomic uint32_t error_flag; /* 0 = ok, 1 = error occurred (per D-13) */

    /* Per-asset results (indexed by pending[] index, sized to pending_count) */
    NtEncodeResult *results;

    /* Read-only context for workers */
    NtBuildEntry *pending;
    uint32_t pending_count;
    uint32_t encode_threads;     /* internal threads per encode call: max(1, thread_count/work_count) */
    _Atomic uint32_t done_count; /* atomic: completed items (for progress logging) */
} NtParallelContext;

/* Per-asset cache status (tracked during finish_pack) */
typedef enum {
    NT_CACHE_NONE = 0,      /* caching disabled or early-deduped entry */
    NT_CACHE_HIT = 1,       /* exact cache file found */
    NT_CACHE_MISS_NEW = 2,  /* no cache entry found for this decoded_hash */
    NT_CACHE_MISS_OPTS = 3, /* decoded_hash found but opts_version_hash differs */
} nt_cache_status_t;

struct NtBuilderContext {
    char output_path[512];

    /* Deferred asset descriptors */
    NtBuildEntry pending[NT_BUILD_MAX_ASSETS];
    uint32_t pending_count;

    /* Data accumulation buffer (used during finish_pack) */
    uint8_t *data_buf;
    uint32_t data_size;
    uint32_t data_capacity;

    /* Final asset entries (built during finish_pack) */
    NtAssetEntry entries[NT_BUILD_MAX_ASSETS];
    uint32_t entry_count;

    /* Mode flags */

    /* Per-type counters for summary */
    uint32_t mesh_count;
    uint32_t texture_count;
    uint32_t shader_count;
    uint32_t blob_count;
    uint32_t font_count;
    uint32_t atlas_count;

    /* Atlas: active atlas state (non-NULL between begin_atlas/end_atlas) */
    NtBuildAtlasState *active_atlas;

    /* Deduplication stats */
    uint32_t dedup_count;
    uint32_t dedup_saved_bytes;

    /* Early dedup stats */
    uint32_t early_dedup_count;

    /* Same-decoded-different-opts warning pairs */
    uint32_t dedup_warn_a[NT_BUILD_MAX_ASSETS];
    uint32_t dedup_warn_b[NT_BUILD_MAX_ASSETS];
    uint32_t dedup_warn_count;

    /* Metadata accumulation (Phase 37) */
    NtBuildMetaEntry meta_pending[NT_BUILD_MAX_META_ENTRIES];
    uint32_t meta_count;

    /* Asset root search paths (D-09) */
    char *asset_roots[NT_BUILD_MAX_ASSET_ROOTS];
    uint32_t asset_root_count;

    /* Codegen: custom header output directory (NULL = next to pack) */
    char *header_dir;

    /* Gzip estimation in summary (default: off) */
    bool gzip_estimate;

    /* Cache: content-addressed encode cache (D-08) */
    char *cache_dir;           /* NULL = caching disabled, set by nt_builder_set_cache_dir */
    uint32_t cache_hit_count;  /* per-build hit stats */
    uint32_t cache_miss_count; /* per-build miss stats */
    double cache_restore_secs; /* total time reading cache files */

    /* Parallel encoding: thread count (0 = single-threaded, per D-12) */
    uint32_t thread_count;
};

/* Internal helpers -- data accumulation (used in finish_pack phase) */
nt_build_result_t nt_builder_append_data(NtBuilderContext *ctx, const void *data, uint32_t size);
nt_build_result_t nt_builder_register_asset(NtBuilderContext *ctx, uint64_t resource_id, nt_asset_type_t type, uint16_t format_version, uint32_t data_size);

/* Internal decode functions -- called from add_* (eager decode) */
nt_build_result_t nt_builder_decode_texture(const uint8_t *src_data, uint32_t src_size, const nt_tex_opts_t *opts, uint8_t **out_pixels, uint32_t *out_w, uint32_t *out_h);
nt_build_result_t nt_builder_decode_texture_raw(const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const nt_tex_opts_t *opts, uint8_t **out_pixels, uint32_t *out_w, uint32_t *out_h);
nt_build_result_t nt_builder_decode_mesh(const char *path, const NtStreamLayout *layout, uint32_t stream_count, nt_tangent_mode_t tangent_mode, const char *mesh_name, uint32_t mesh_index,
                                         uint8_t **out_data, uint32_t *out_size);
nt_build_result_t nt_builder_decode_scene_mesh(const nt_glb_scene_t *scene, uint32_t mesh_index, uint32_t primitive_index, const NtStreamLayout *layout, uint32_t stream_count,
                                               nt_tangent_mode_t tangent_mode, uint8_t **out_data, uint32_t *out_size);

/* Font decode: TTF -> final NT_ASSET_FONT binary (like mesh path) */
nt_build_result_t nt_builder_decode_font(const char *path, const char *charset, uint8_t **out_data, uint32_t *out_size);

/* Internal encode functions -- called from finish_pack (encode phase) */
nt_build_result_t nt_builder_encode_texture(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, uint64_t resource_id, const nt_tex_opts_t *opts);
nt_build_result_t nt_builder_encode_texture_compressed(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, uint64_t resource_id, const nt_tex_opts_t *opts,
                                                       const nt_tex_compress_opts_t *compress_opts);
nt_build_result_t nt_builder_encode_shader(NtBuilderContext *ctx, const uint8_t *resolved_text, uint32_t text_len, nt_build_shader_stage_t stage, uint64_t resource_id);

/* Thread-safe encode functions -- return independent buffers (no shared state) */
nt_build_result_t nt_builder_encode_texture_to_buf(const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const nt_tex_opts_t *opts, uint8_t **out_data, uint32_t *out_size,
                                                   nt_asset_type_t *out_type, uint16_t *out_version);
nt_build_result_t nt_builder_encode_texture_compressed_to_buf(const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const nt_tex_opts_t *opts, const nt_tex_compress_opts_t *compress_opts,
                                                              uint32_t encode_threads, uint8_t **out_data, uint32_t *out_size, nt_asset_type_t *out_type, uint16_t *out_version);
nt_build_result_t nt_builder_encode_shader_to_buf(const uint8_t *resolved_text, uint32_t text_len, nt_build_shader_stage_t stage, uint8_t **out_data, uint32_t *out_size, nt_asset_type_t *out_type,
                                                  uint16_t *out_version);

/* Metadata accumulation (called from import functions) */
void nt_builder_add_meta(NtBuilderContext *ctx, uint64_t resource_id, uint64_t kind, const void *data, uint32_t size);

/* AABB extraction from cgltf primitive POSITION accessor */
struct cgltf_primitive;
void nt_extract_aabb(const struct cgltf_primitive *prim, float out_min[3], float out_max[3]);

/* Tangent computation (MikkTSpace wrapper) */
nt_build_result_t nt_builder_compute_tangents(const float *positions, const float *normals, const float *uvs, const uint32_t *indices, uint32_t vertex_count, uint32_t index_count,
                                              float *out_tangents);

/* Shared: build binary mesh output buffer from mesh components (used by mesh + scene decode) */
nt_build_result_t nt_builder_build_mesh_buffer(const NtStreamLayout *layout, uint32_t stream_count, float *stream_floats[], uint32_t vertex_count, const struct cgltf_primitive *prim,
                                               uint8_t *index_buf, uint32_t index_count, uint8_t index_type, uint32_t index_data_size, uint8_t **out_data, uint32_t *out_size);

/* Hash and path utilities (declared early -- used by inline functions below) */
char *nt_builder_normalize_path(const char *path);
uint16_t nt_builder_float32_to_float16(float value);

/* Shared type conversion: float -> target stream type (float16, int8, uint8, int16, uint16) */
static inline float nt_builder_clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static inline void nt_builder_convert_component(float value, nt_stream_type_t type, bool normalized, uint8_t *out_ptr) {
    switch (type) {
    case NT_STREAM_FLOAT32: {
        memcpy(out_ptr, &value, sizeof(float));
        break;
    }
    case NT_STREAM_FLOAT16: {
        NT_BUILD_ASSERT((value <= 65504.0F && value >= -65504.0F) && "float16 overflow -- value exceeds +-65504, use FLOAT32 for this stream");
        uint16_t h = nt_builder_float32_to_float16(value);
        memcpy(out_ptr, &h, sizeof(uint16_t));
        break;
    }
    case NT_STREAM_UINT8: {
        if (normalized) {
            float c = nt_builder_clampf(value, 0.0F, 1.0F);
            *out_ptr = (uint8_t)((c * 255.0F) + 0.5F);
        } else {
            *out_ptr = (uint8_t)nt_builder_clampf(value + 0.5F, 0.0F, 255.0F);
        }
        break;
    }
    case NT_STREAM_INT8: {
        if (normalized) {
            float c = nt_builder_clampf(value, -1.0F, 1.0F);
            float bias = (c >= 0.0F) ? 0.5F : -0.5F;
            int8_t s = (int8_t)((c * 127.0F) + bias);
            memcpy(out_ptr, &s, sizeof(int8_t));
        } else {
            int8_t s = (int8_t)nt_builder_clampf(value + ((value >= 0.0F) ? 0.5F : -0.5F), -128.0F, 127.0F);
            memcpy(out_ptr, &s, sizeof(int8_t));
        }
        break;
    }
    case NT_STREAM_UINT16: {
        if (normalized) {
            float c = nt_builder_clampf(value, 0.0F, 1.0F);
            uint16_t u = (uint16_t)((c * 65535.0F) + 0.5F);
            memcpy(out_ptr, &u, sizeof(uint16_t));
        } else {
            uint16_t u = (uint16_t)nt_builder_clampf(value + 0.5F, 0.0F, 65535.0F);
            memcpy(out_ptr, &u, sizeof(uint16_t));
        }
        break;
    }
    case NT_STREAM_INT16: {
        if (normalized) {
            float c = nt_builder_clampf(value, -1.0F, 1.0F);
            float bias = (c >= 0.0F) ? 0.5F : -0.5F;
            int16_t s = (int16_t)((c * 32767.0F) + bias);
            memcpy(out_ptr, &s, sizeof(int16_t));
        } else {
            int16_t s = (int16_t)nt_builder_clampf(value + ((value >= 0.0F) ? 0.5F : -0.5F), -32768.0F, 32767.0F);
            memcpy(out_ptr, &s, sizeof(int16_t));
        }
        break;
    }
    }
}

/* Size formatting: bytes -> human-readable string ("1.2K", "3.5M", "42B") */
static inline void nt_format_size(uint32_t bytes, char *buf, size_t buf_size) {
    if (bytes >= 1024 * 1024) {
        (void)snprintf(buf, buf_size, "%.1fM", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        (void)snprintf(buf, buf_size, "%.1fK", (double)bytes / 1024.0);
    } else {
        (void)snprintf(buf, buf_size, "%uB", bytes);
    }
}

/* Pack path utilities (shared between codegen and dump) */

/* Extract filename stem from pack path (no directory, no extension).
 * "build/foo/demo.ntpack" -> "demo" */
static inline void nt_builder_pack_stem(const char *pack_path, char *stem, size_t stem_size) {
    const char *slash = strrchr(pack_path, '/');
    const char *bslash = strrchr(pack_path, '\\');
    const char *filename = pack_path;
    if (slash && slash > filename) {
        filename = slash + 1;
    }
    if (bslash && bslash + 1 > filename) {
        filename = bslash + 1;
    }
    strncpy(stem, filename, stem_size - 1);
    stem[stem_size - 1] = '\0';
    char *dot = strrchr(stem, '.');
    if (dot) {
        *dot = '\0';
    }
}

/* Derive .h header path from .ntpack path (replace extension).
 * "build/foo/demo.ntpack" -> "build/foo/demo.h" */
static inline void nt_builder_pack_to_header_path(const char *pack_path, char *header_path, size_t size) {
    strncpy(header_path, pack_path, size - 1);
    header_path[size - 1] = '\0';
    char *dot = strrchr(header_path, '.');
    if (dot && (size_t)(dot - header_path) < size - 3) {
        dot[0] = '.';
        dot[1] = 'h';
        dot[2] = '\0';
    }
}

/* Deferred entry addition (shared between add_* and scene_mesh) */
void nt_builder_add_entry(NtBuilderContext *ctx, const char *path, nt_build_asset_kind_t kind, void *data, uint8_t *decoded_data, uint32_t decoded_size, uint64_t decoded_hash);

/* Codegen: generate .h header with ASSET_* constants */
nt_build_result_t nt_builder_generate_header(const NtBuilderContext *ctx);

/* File I/O utilities */
char *nt_builder_read_file(const char *path, uint32_t *out_size);

/* Include resolver (D-11, D-12, D-13) */
char *nt_builder_resolve_includes(const char *source, uint32_t source_len, const char *source_path, const NtBuilderContext *ctx, uint32_t *out_len);

/* File lookup via asset roots */
char *nt_builder_find_file(const char *filename, const char *relative_to_dir, const NtBuilderContext *ctx);

/* Cache: internal functions (nt_builder_cache.c) */
uint64_t nt_builder_compute_opts_hash(const NtBuildEntry *pe);
void nt_builder_build_cache_path(const char *cache_dir, uint64_t decoded_hash, uint64_t opts_hash, char *out, size_t out_size);
nt_cache_status_t nt_builder_cache_lookup(const char *cache_dir, uint64_t decoded_hash, uint64_t opts_hash, uint8_t **out_data, uint32_t *out_size);
bool nt_builder_cache_store(const char *cache_dir, uint64_t decoded_hash, uint64_t opts_hash, const uint8_t *data, uint32_t size);
void nt_builder_ensure_cache_dir(const char *dir);

/* Test-access wrappers for atlas geometry algorithms (static functions in nt_builder_atlas.c) */
#ifdef NT_BUILDER_ATLAS_TEST_ACCESS
bool nt_atlas_test_alpha_trim(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t threshold, uint32_t *ox, uint32_t *oy, uint32_t *ow, uint32_t *oh);
uint32_t nt_atlas_test_convex_hull(const void *pts, uint32_t n, void *out);
uint32_t nt_atlas_test_rdp_simplify(const void *hull, uint32_t n, uint32_t max_v, void *out);
uint32_t nt_atlas_test_fan_triangulate(uint32_t vc, uint16_t *idx);
#endif

#endif /* NT_BUILDER_INTERNAL_H */
