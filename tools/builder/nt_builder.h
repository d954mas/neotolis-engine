#ifndef NT_BUILDER_H
#define NT_BUILDER_H

#include <stdbool.h>
#include <stdint.h>

#include "hash/nt_hash.h"
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

/* Tangent computation mode for scene mesh extraction */
typedef enum {
    NT_TANGENT_AUTO = 0,    /* extract from glTF if present, compute MikkTSpace if not */
    NT_TANGENT_COMPUTE = 1, /* always compute via MikkTSpace (ignore glTF tangents) */
    NT_TANGENT_REQUIRE = 2, /* error if glTF doesn't have tangents */
    NT_TANGENT_NONE = 3,    /* skip tangent attribute entirely */
} nt_tangent_mode_t;

/* Mesh options for scene mesh extraction */
typedef struct {
    const NtStreamLayout *layout;
    uint32_t stream_count;
    nt_tangent_mode_t tangent_mode;
} nt_mesh_opts_t;

/* --- glTF scene types (parse/inspect/extract) --- */

typedef struct {
    uint32_t diffuse_index;  /* index into scene.textures[], UINT32_MAX if none */
    uint32_t normal_index;   /* index into scene.textures[], UINT32_MAX if none */
    uint32_t specular_index; /* index into scene.textures[], UINT32_MAX if none */
    float base_color[4];     /* base color factor from glTF material */
    float alpha_cutoff;      /* alpha cutoff for alpha test materials */
    bool double_sided;
    const char *name; /* material name from glTF (NULL if unnamed) */
} nt_glb_material_t;

typedef struct {
    const uint8_t *data;   /* raw image data pointer (from glb buffer) */
    uint32_t size;         /* image data size in bytes */
    const char *name;      /* image name or URI from glTF */
    const char *mime_type; /* e.g. "image/png", "image/jpeg" */
} nt_glb_texture_t;

typedef struct {
    uint32_t primitive_count;
    uint32_t material_index; /* glTF material index, UINT32_MAX if none */
    const char *name;
} nt_glb_mesh_t;

typedef struct {
    float transform[16]; /* world transform mat4 */
    uint32_t mesh_index; /* index into scene.meshes[], UINT32_MAX if no mesh */
    const char *name;
} nt_glb_node_t;

typedef struct {
    nt_glb_mesh_t *meshes;
    uint32_t mesh_count;
    nt_glb_material_t *materials;
    uint32_t material_count;
    nt_glb_texture_t *textures;
    uint32_t texture_count;
    nt_glb_node_t *nodes;
    uint32_t node_count;
    void *_internal; /* opaque cgltf_data pointer */
} nt_glb_scene_t;

/* Opaque builder context */
typedef struct NtBuilderContext NtBuilderContext;

/* --- Core API ---
 * Lifecycle: start_pack → add_* → finish_pack → free_pack.
 * Caller must always call free_pack when done, whether finish succeeded or not.
 */
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

/* --- Scene API (parse/inspect/extract multi-mesh glTF) --- */
nt_build_result_t nt_builder_parse_glb_scene(nt_glb_scene_t *scene, const char *path);
nt_build_result_t nt_builder_add_scene_mesh(NtBuilderContext *ctx, const nt_glb_scene_t *scene, uint32_t mesh_index, uint32_t primitive_index, const char *resource_id, const nt_mesh_opts_t *opts);
void nt_builder_free_glb_scene(nt_glb_scene_t *scene);

/* --- Blob API (generic binary data asset) --- */
nt_build_result_t nt_builder_add_blob(NtBuilderContext *ctx, const void *data, uint32_t size, const char *resource_id);

/* --- Texture from memory API --- */
nt_build_result_t nt_builder_add_texture_from_memory(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id);

/* --- Utilities --- */
nt_build_result_t nt_builder_dump_pack(const char *pack_path);
nt_hash64_t nt_builder_normalize_and_hash(const char *str);

#endif /* NT_BUILDER_H */
