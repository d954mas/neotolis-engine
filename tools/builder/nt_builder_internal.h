#ifndef NT_BUILDER_INTERNAL_H
#define NT_BUILDER_INTERNAL_H

#include "nt_builder.h"
#include "nt_pack_format.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initial data buffer capacity (1 MB, doubles on overflow) */
#define NT_BUILD_INITIAL_CAPACITY (1024 * 1024)

/* Asset type tag for deferred entries */
typedef enum {
    NT_BUILD_ASSET_MESH = 0,
    NT_BUILD_ASSET_TEXTURE = 1,
    NT_BUILD_ASSET_SHADER = 2,
} nt_build_asset_kind_t;

/* Deferred asset entry -- stored during add_*, processed in finish_pack */
typedef struct {
    char *path;                           /* normalized path (owned, heap) */
    uint32_t resource_id;                 /* FNV-1a hash or explicit */
    nt_build_asset_kind_t kind;           /* mesh/texture/shader */
    nt_build_shader_stage_t shader_stage; /* only for shaders */
    NtStreamLayout layout[8];             /* NT_MESH_MAX_STREAMS -- copied from user */
    uint32_t stream_count;                /* only for meshes */
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

    /* Diagnostic paths for summary */
    char *resource_paths[NT_BUILD_MAX_ASSETS];

    /* Error state */
    bool has_error;

    /* Pack metadata */
    uint32_t pack_id;

    /* Per-type counters for summary */
    uint32_t mesh_count;
    uint32_t texture_count;
    uint32_t shader_count;
};

/* Internal helpers -- data accumulation (used in finish_pack phase) */
nt_build_result_t nt_builder_append_data(NtBuilderContext *ctx, const void *data, uint32_t size);
nt_build_result_t nt_builder_register_asset(NtBuilderContext *ctx, const char *path, uint32_t resource_id, nt_asset_type_t type, uint16_t format_version, uint32_t data_size);

/* Internal import functions -- called from finish_pack */
nt_build_result_t nt_builder_import_mesh(NtBuilderContext *ctx, const char *path, const NtStreamLayout *layout, uint32_t stream_count, uint32_t resource_id);
nt_build_result_t nt_builder_import_texture(NtBuilderContext *ctx, const char *path, uint32_t resource_id);
nt_build_result_t nt_builder_import_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage, uint32_t resource_id);

/* Hash and path utilities */
char *nt_builder_normalize_path(const char *path);
uint32_t nt_builder_fnv1a(const char *str);
uint16_t nt_builder_float32_to_float16(float value);

#endif /* NT_BUILDER_INTERNAL_H */
