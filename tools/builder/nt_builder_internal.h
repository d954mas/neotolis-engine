#ifndef NT_BUILDER_INTERNAL_H
#define NT_BUILDER_INTERNAL_H

#include "log/nt_log.h"
#include "nt_builder.h"
#include "nt_pack_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NT_BUILD_ASSERT is defined in nt_builder.h (public, usable by game build scripts) */

/* Initial data buffer capacity (1 MB, doubles on overflow) */
#define NT_BUILD_INITIAL_CAPACITY (1024 * 1024)

/* Asset type tag for deferred entries */
typedef enum {
    NT_BUILD_ASSET_MESH = 0,
    NT_BUILD_ASSET_TEXTURE = 1,
    NT_BUILD_ASSET_SHADER = 2,
    NT_BUILD_ASSET_BLOB = 3,
    NT_BUILD_ASSET_SCENE_MESH = 4,
    NT_BUILD_ASSET_TEXTURE_MEM = 5,
    NT_BUILD_ASSET_TEXTURE_RAW = 6,
    NT_BUILD_ASSET_TEXTURE_MEM_COMPRESSED = 7,
} nt_build_asset_kind_t;

/* Type-specific data for mesh entries */
typedef struct {
    NtStreamLayout layout[NT_MESH_MAX_STREAMS]; /* deep-copied from user */
    uint32_t stream_count;
    nt_tangent_mode_t tangent_mode;
    char *mesh_name;     /* owned copy, NULL if not used */
    uint32_t mesh_index; /* UINT32_MAX if not used */
    char *file_path;     /* owned copy of actual file path (for multi-mesh logical path) */
} NtBuildMeshData;

/* Type-specific data for shader entries */
typedef struct {
    nt_build_shader_stage_t stage;
} NtBuildShaderData;

/* Type-specific data for blob entries */
typedef struct {
    void *data; /* deep-copied blob data (owned, heap) */
    uint32_t size;
} NtBuildBlobData;

/* Type-specific data for scene mesh entries */
typedef struct {
    const nt_glb_scene_t *scene; /* borrowed reference, valid until free_glb_scene */
    uint32_t mesh_index;
    uint32_t primitive_index;
    NtStreamLayout layout[NT_MESH_MAX_STREAMS];
    uint32_t stream_count;
    nt_tangent_mode_t tangent_mode;
} NtBuildSceneMeshData;

/* Type-specific data for texture-from-memory entries */
typedef struct {
    uint8_t *data; /* deep-copied image data (owned, heap) */
    uint32_t size;
    nt_tex_opts_t opts; /* format + resize options */
} NtBuildTexMemData;

/* Type-specific data for raw RGBA pixel texture entries */
typedef struct {
    uint8_t *pixels; /* deep-copied RGBA pixel data (owned, heap) */
    uint32_t width;
    uint32_t height;
    nt_tex_opts_t opts; /* format + resize options */
} NtBuildTexRawData;

/* Type-specific data for compressed texture-from-memory entries */
typedef struct {
    uint8_t *data; /* deep-copied image data (owned, heap) */
    uint32_t size;
    nt_tex_opts_t opts;              /* format + resize options */
    nt_tex_compress_opts_t compress; /* compression settings */
} NtBuildTexMemCompressedData;

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
    char *path;                 /* normalized source file path (owned, heap) */
    char *rename_key;           /* renamed key path (owned, heap, NULL if not renamed) */
    uint64_t resource_id;       /* nt_hash64 value */
    nt_build_asset_kind_t kind; /* mesh/texture/shader */
    void *data;                 /* NtBuildMeshData* / NtBuildShaderData* / NULL (owned, heap) */
} NtBuildEntry;

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

    /* Deduplication stats */
    uint32_t dedup_count;
    uint32_t dedup_saved_bytes;

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
};

/* Internal helpers -- data accumulation (used in finish_pack phase) */
nt_build_result_t nt_builder_append_data(NtBuilderContext *ctx, const void *data, uint32_t size);
nt_build_result_t nt_builder_register_asset(NtBuilderContext *ctx, uint64_t resource_id, nt_asset_type_t type, uint16_t format_version, uint32_t data_size);

/* Internal import functions -- called from finish_pack */
nt_build_result_t nt_builder_import_mesh(NtBuilderContext *ctx, const char *path, const NtStreamLayout *layout, uint32_t stream_count, nt_tangent_mode_t tangent_mode, const char *mesh_name,
                                         uint32_t mesh_index, uint64_t resource_id);
nt_build_result_t nt_builder_import_texture(NtBuilderContext *ctx, const char *path, uint64_t resource_id);
nt_build_result_t nt_builder_import_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage, uint64_t resource_id);
nt_build_result_t nt_builder_import_blob(NtBuilderContext *ctx, const void *data, uint32_t size, uint64_t resource_id);
nt_build_result_t nt_builder_import_texture_from_memory(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, uint64_t resource_id, const nt_tex_opts_t *opts);
nt_build_result_t nt_builder_import_texture_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, uint64_t resource_id, const nt_tex_opts_t *opts);
nt_build_result_t nt_builder_import_texture_from_memory_compressed(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, uint64_t resource_id, const nt_tex_opts_t *opts,
                                                                   const nt_tex_compress_opts_t *compress_opts);
nt_build_result_t nt_builder_import_scene_mesh(NtBuilderContext *ctx, const nt_glb_scene_t *scene, uint32_t mesh_index, uint32_t primitive_index, const NtStreamLayout *layout, uint32_t stream_count,
                                               nt_tangent_mode_t tangent_mode, uint64_t resource_id);

/* Metadata accumulation (called from import functions) */
void nt_builder_add_meta(NtBuilderContext *ctx, uint64_t resource_id, uint64_t kind, const void *data, uint32_t size);

/* Tangent computation (MikkTSpace wrapper) */
nt_build_result_t nt_builder_compute_tangents(const float *positions, const float *normals, const float *uvs, const uint32_t *indices, uint32_t vertex_count, uint32_t index_count,
                                              float *out_tangents);

/* Hash and path utilities (declared early — used by inline functions below) */
char *nt_builder_normalize_path(const char *path);
uint16_t nt_builder_float32_to_float16(float value);

/* Shared type conversion: float → target stream type (float16, int8, uint8, int16, uint16) */
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

/* Size formatting: bytes → human-readable string ("1.2K", "3.5M", "42B") */
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
 * "build/foo/demo.ntpack" → "demo" */
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
 * "build/foo/demo.ntpack" → "build/foo/demo.h" */
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
void nt_builder_add_entry(NtBuilderContext *ctx, const char *path, nt_build_asset_kind_t kind, void *data);

/* Codegen: generate .h header with ASSET_* constants */
nt_build_result_t nt_builder_generate_header(const NtBuilderContext *ctx);

/* File I/O utilities */
char *nt_builder_read_file(const char *path, uint32_t *out_size);

/* Include resolver (D-11, D-12, D-13) */
char *nt_builder_resolve_includes(const char *source, uint32_t source_len, const char *source_path, const NtBuilderContext *ctx, uint32_t *out_len);

/* File lookup via asset roots */
char *nt_builder_find_file(const char *filename, const char *relative_to_dir, const NtBuilderContext *ctx);

#endif /* NT_BUILDER_INTERNAL_H */
