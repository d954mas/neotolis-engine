#ifndef NT_BUILDER_H
#define NT_BUILDER_H

#include <stdbool.h>
#include <stdint.h>

#include "nt_mesh_format.h" /* nt_stream_type_t */

/* Build limits (game can override before including this header) */
#ifndef NT_BUILD_MAX_ASSETS
#define NT_BUILD_MAX_ASSETS 1024
#endif
#ifndef NT_BUILD_MAX_VERTICES
#define NT_BUILD_MAX_VERTICES 65536
#endif
#ifndef NT_BUILD_MAX_INDICES
#define NT_BUILD_MAX_INDICES (65536 * 3)
#endif
#ifndef NT_BUILD_MAX_TEXTURE_SIZE
#define NT_BUILD_MAX_TEXTURE_SIZE 4096
#endif

/* Return codes */
typedef enum {
    NT_BUILD_OK = 0,
    NT_BUILD_ERR_IO = 1,
    NT_BUILD_ERR_VALIDATION = 2,
    NT_BUILD_ERR_FORMAT = 3,
    NT_BUILD_ERR_LIMIT = 4,
    NT_BUILD_ERR_DUPLICATE = 5,
} nt_build_result_t;

/* Explicit stream layout -- game declares which glTF attributes to extract */
typedef struct {
    const char *engine_name; /* e.g. "position", "uv0" */
    const char *gltf_name;   /* e.g. "POSITION", "TEXCOORD_0" */
    nt_stream_type_t type;   /* target type in output (may differ from glTF) */
    uint8_t count;           /* components per vertex (1-4) */
    bool normalized;         /* true = normalize to [0,1] or [-1,1] in runtime */
} NtStreamLayout;

/* Shader stage hint for add_shader */
typedef enum {
    NT_BUILD_SHADER_VERTEX = 0,
    NT_BUILD_SHADER_FRAGMENT = 1,
} nt_build_shader_stage_t;

/* Opaque builder context */
typedef struct NtBuilderContext NtBuilderContext;

/* --- Core API --- */
NtBuilderContext *nt_builder_start_pack(const char *output_path);
nt_build_result_t nt_builder_finish_pack(NtBuilderContext *ctx);
void nt_builder_free_pack(NtBuilderContext *ctx);

/* --- Asset addition (single file) --- */
nt_build_result_t nt_builder_add_mesh(NtBuilderContext *ctx, const char *path, const NtStreamLayout *layout, uint32_t stream_count);
nt_build_result_t nt_builder_add_texture(NtBuilderContext *ctx, const char *path);
nt_build_result_t nt_builder_add_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage);

/* --- Force mode (add or replace, no duplicate error) --- */
void nt_builder_set_force(NtBuilderContext *ctx, bool force);

/* --- Batch addition (glob patterns) --- */
nt_build_result_t nt_builder_add_meshes(NtBuilderContext *ctx, const char *pattern, const NtStreamLayout *layout, uint32_t stream_count);
nt_build_result_t nt_builder_add_textures(NtBuilderContext *ctx, const char *pattern);
nt_build_result_t nt_builder_add_shaders(NtBuilderContext *ctx, const char *pattern, nt_build_shader_stage_t stage);

/* --- Rename (change resource_id key, keep source file) --- */
nt_build_result_t nt_builder_rename(NtBuilderContext *ctx, const char *old_path, const char *new_path);

/* --- Utilities --- */
nt_build_result_t nt_builder_dump_pack(const char *pack_path);
uint32_t nt_builder_hash(const char *str);

#endif /* NT_BUILDER_H */
