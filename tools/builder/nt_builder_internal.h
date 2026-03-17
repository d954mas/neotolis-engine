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

struct NtBuilderContext {
    char output_path[512];

    /* Asset data accumulation (heap-allocated) */
    uint8_t *data_buf;
    uint32_t data_size;
    uint32_t data_capacity;

    /* Asset entries */
    NtAssetEntry entries[NT_BUILD_MAX_ASSETS];
    uint32_t entry_count;

    /* Duplicate detection */
    uint32_t resource_ids[NT_BUILD_MAX_ASSETS];
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

/* Internal helpers */
nt_build_result_t nt_builder_append_data(NtBuilderContext *ctx, const void *data, uint32_t size);
nt_build_result_t nt_builder_register_asset(NtBuilderContext *ctx, const char *path, uint32_t resource_id, nt_asset_type_t type, uint16_t format_version, uint32_t data_size);
bool nt_builder_check_duplicate(NtBuilderContext *ctx, uint32_t resource_id, const char *path);
char *nt_builder_normalize_path(const char *path);
uint32_t nt_builder_fnv1a(const char *str);
uint16_t nt_builder_float32_to_float16(float value);

#endif /* NT_BUILDER_INTERNAL_H */
