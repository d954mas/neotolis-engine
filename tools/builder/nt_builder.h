#ifndef NT_BUILDER_H
#define NT_BUILDER_H

#include <stdbool.h>
#include <stdint.h>

#include "hash/nt_hash.h"
#include "nt_mesh_format.h"    /* nt_stream_type_t */
#include "nt_texture_format.h" /* nt_texture_pixel_format_t */

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

/* Mesh options for add_mesh and scene mesh extraction */
typedef struct {
    const NtStreamLayout *layout;
    uint32_t stream_count;
    nt_tangent_mode_t tangent_mode;
    const char *mesh_name;     /* select mesh by name in multi-mesh glb (NULL = not used) */
    uint32_t mesh_index;       /* select mesh by index (only when use_mesh_index is true) */
    bool use_mesh_index;       /* true = select by mesh_index */
    const char *resource_name; /* optional rid suffix override (NULL = auto) */
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

/* Opaque asset registry -- accumulates assets across multiple packs for combined header */
typedef struct NtBuilderRegistry NtBuilderRegistry;

/* --- Core API ---
 * Lifecycle: start_pack → add_* → finish_pack → free_pack.
 * Caller must always call free_pack when done, whether finish succeeded or not.
 */
NtBuilderContext *nt_builder_start_pack(const char *output_path);
nt_build_result_t nt_builder_finish_pack(NtBuilderContext *ctx);
void nt_builder_free_pack(NtBuilderContext *ctx);

/* --- Asset addition (single file) --- */
nt_build_result_t nt_builder_add_mesh(NtBuilderContext *ctx, const char *path, const nt_mesh_opts_t *opts);
nt_build_result_t nt_builder_add_texture(NtBuilderContext *ctx, const char *path);
nt_build_result_t nt_builder_add_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage);

/* --- Force mode (add or replace, no duplicate error) --- */
void nt_builder_set_force(NtBuilderContext *ctx, bool force);

/* --- Asset roots (search paths for #include and file lookup) --- */
#ifndef NT_BUILD_MAX_ASSET_ROOTS
#define NT_BUILD_MAX_ASSET_ROOTS 8
#endif
nt_build_result_t nt_builder_add_asset_root(NtBuilderContext *ctx, const char *path);

/* --- Batch addition (glob patterns) --- */
nt_build_result_t nt_builder_add_meshes(NtBuilderContext *ctx, const char *pattern, const nt_mesh_opts_t *opts);
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

/* --- Texture options (game controls format and resize per-texture) --- */

typedef struct {
    nt_texture_pixel_format_t format; /* output pixel format (default: NT_TEXTURE_FORMAT_RGBA8) */
    uint32_t max_size;                /* 0 = no resize, otherwise max(w,h) clamped to this */
} nt_tex_opts_t;

/* --- Texture compression options (Basis Universal encoding) --- */

typedef enum {
    NT_TEX_COMPRESS_ETC1S = 1, /* ETC1S mode -- small size, good for color/diffuse textures */
    NT_TEX_COMPRESS_UASTC = 2, /* UASTC mode -- higher quality, good for normal maps */
} nt_tex_compress_mode_t;

typedef struct {
    nt_tex_compress_mode_t mode; /* ETC1S or UASTC */
    uint32_t quality;            /* ETC1S: 1-255 (higher=better), UASTC: 0-5 (pack level) */
    float rdo_quality;           /* ETC1S: rate-distortion optimization (0.0 = disabled), UASTC: unused */
} nt_tex_compress_opts_t;

/* --- Compression presets (D-04) --- */

static inline nt_tex_compress_opts_t nt_tex_compress_etc1s_low(void) { return (nt_tex_compress_opts_t){.mode = NT_TEX_COMPRESS_ETC1S, .quality = 64, .rdo_quality = 1.0F}; }
static inline nt_tex_compress_opts_t nt_tex_compress_etc1s_mid(void) { return (nt_tex_compress_opts_t){.mode = NT_TEX_COMPRESS_ETC1S, .quality = 128, .rdo_quality = 1.5F}; }
static inline nt_tex_compress_opts_t nt_tex_compress_etc1s_high(void) { return (nt_tex_compress_opts_t){.mode = NT_TEX_COMPRESS_ETC1S, .quality = 200, .rdo_quality = 0.0F}; }
static inline nt_tex_compress_opts_t nt_tex_compress_uastc_low(void) { return (nt_tex_compress_opts_t){.mode = NT_TEX_COMPRESS_UASTC, .quality = 0, .rdo_quality = 0.0F}; }
static inline nt_tex_compress_opts_t nt_tex_compress_uastc_mid(void) { return (nt_tex_compress_opts_t){.mode = NT_TEX_COMPRESS_UASTC, .quality = 2, .rdo_quality = 0.0F}; }
static inline nt_tex_compress_opts_t nt_tex_compress_uastc_high(void) { return (nt_tex_compress_opts_t){.mode = NT_TEX_COMPRESS_UASTC, .quality = 4, .rdo_quality = 0.0F}; }

/* --- Texture from memory API --- */
nt_build_result_t nt_builder_add_texture_from_memory(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id);
nt_build_result_t nt_builder_add_texture_from_memory_ex(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id, const nt_tex_opts_t *opts);

/* --- Texture from raw RGBA pixels (no image decode needed) --- */
nt_build_result_t nt_builder_add_texture_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const char *resource_id, const nt_tex_opts_t *opts);

/* --- Texture from memory with compression (NULL compress_opts = raw v1 format) --- */
nt_build_result_t nt_builder_add_texture_from_memory_compressed(NtBuilderContext *ctx, const uint8_t *data, uint32_t size, const char *resource_id, const nt_tex_opts_t *opts,
                                                                const nt_tex_compress_opts_t *compress_opts);

/* --- Codegen options --- */
void nt_builder_set_header_dir(NtBuilderContext *ctx, const char *dir);

/* --- Gzip estimation (off by default, enable for transport size analysis) --- */
void nt_builder_set_gzip_estimate(NtBuilderContext *ctx, bool enabled);

/* --- Asset registry (combined header across packs) ---
 * Lifecycle: create → attach to each pack context → finish packs → generate → free.
 * finish_pack automatically registers assets into the attached registry.
 */
NtBuilderRegistry *nt_builder_create_registry(void);
void nt_builder_set_registry(NtBuilderContext *ctx, NtBuilderRegistry *reg);
nt_build_result_t nt_builder_generate_registry_header(const NtBuilderRegistry *reg, const char *output_path);
void nt_builder_free_registry(NtBuilderRegistry *reg);

/* --- Utilities --- */
nt_build_result_t nt_builder_dump_pack(const char *pack_path);
nt_hash64_t nt_builder_normalize_and_hash(const char *str);

#endif /* NT_BUILDER_H */
