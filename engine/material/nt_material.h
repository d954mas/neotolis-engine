#ifndef NT_MATERIAL_H
#define NT_MATERIAL_H

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "render/nt_render_defs.h"
#include "resource/nt_resource.h"

/* ---- Compile-time limits ---- */

#define NT_MATERIAL_MAX_TEXTURES 4
/* Bytes of a material's `Material` uniform block (16 vec4). */
#define NT_MATERIAL_MAX_PARAM_BYTES 256
#define NT_MATERIAL_MAX_ATTR_MAP 8

/* ---- Handle type ---- */

typedef struct {
    uint32_t id;
} nt_material_t;

#define NT_MATERIAL_INVALID ((nt_material_t){0})

/* ---- Cull mode enum ---- */

typedef enum {
    NT_CULL_NONE = 0,
    NT_CULL_BACK,
    NT_CULL_FRONT,
} nt_cull_mode_t;

/* ---- Descriptor sub-types ---- */

typedef struct {
    const char *name;       /* required sampler uniform name; hashed at create, not retained */
    nt_resource_t resource; /* resolved by nt_material_apply at each call */
    nt_sampler_t sampler;   /* override; .id==0 = use texture's asset-baked default */
} nt_material_texture_desc_t;

typedef struct {
    const char *stream_name;
    uint8_t location;
    float default_value[4]; /* read only when the desc sets has_attr_defaults */
} nt_material_attr_desc_t;

/* ---- Creation descriptor ---- */

typedef struct {
    /* Borrowed: the material never links, destroys or checks the program. */
    nt_program_t program;
    /* The material's own samplers; a renderer supplies its textures (atlas page, font, skin)
     * per draw through nt_material_apply. */
    nt_material_texture_desc_t textures[NT_MATERIAL_MAX_TEXTURES];
    uint8_t texture_count;
    /* Bytes of the program's `Material` uniform block, copied at create: a C struct mirroring
     * the std140 block (vec4/ivec4/uvec4/mat4 members). params_size is 0 (no params, NULL
     * params) or a multiple of 16 up to NT_MATERIAL_MAX_PARAM_BYTES. */
    const void *params;
    uint16_t params_size;
    /* Sprite path only: a mesh feeds every stream at its pack location, so the mesh renderer
     * assert that a mesh material has none. */
    nt_material_attr_desc_t attr_map[NT_MATERIAL_MAX_ATTR_MAP];
    uint8_t attr_map_count;
    /* Sprite renderer: an emit without its own custom block bakes these instead of asserting. */
    bool has_attr_defaults;
    bool depth_test;
    bool depth_write;
    nt_blend_state_t blend;
    nt_cull_mode_t cull_mode;
    const char *label; /* debug name — must be string literal or static storage */
} nt_material_create_desc_t;

/* ---- Init descriptor ---- */

typedef struct {
    uint16_t max_materials;
} nt_material_desc_t;

/* ---- Defaults ---- */

static inline nt_material_desc_t nt_material_desc_defaults(void) {
    return (nt_material_desc_t){
        .max_materials = 64,
    };
}

/* ---- Material info (read-only query for render module) ---- */

typedef struct {
    /* Borrowed: the material never links, destroys or inspects it. May name a
     * program still linking, one that died with the GL context or one its owner
     * destroyed -- ask nt_gfx_program_ready(program) before drawing with it. */
    nt_program_t program;
    uint32_t attr_map_hashes[NT_MATERIAL_MAX_ATTR_MAP];
    uint8_t attr_map_locations[NT_MATERIAL_MAX_ATTR_MAP];
    float attr_map_defaults[NT_MATERIAL_MAX_ATTR_MAP][4];
    uint8_t attr_map_count;
    bool has_attr_defaults;
    nt_blend_state_t blend;
    bool depth_test;
    bool depth_write;
    nt_cull_mode_t cull_mode;
    const char *label; /* debug name (string literal, static storage) */
} nt_material_info_t;

/* ---- Lifecycle ---- */

/* NULL desc, zero capacity, double init and allocation failure assert. A material with params
 * owns a gfx uniform buffer: init and shut down materials inside the gfx lifetime. */
void nt_material_init(const nt_material_desc_t *desc);
/* Destroys every material's buffer; outside a drawn frame (the gfx frame rule). */
void nt_material_shutdown(void);
/* Context restore: recreates every material's params buffer from its CPU copy. Call it in the
 * restore sequence, before the first pass and before any nt_material_set_params of that frame;
 * a second call is harmless, and an uninitialized module ignores it. A material created while
 * the context was lost gets its buffer here. */
void nt_material_restore_gpu(void);

/* ---- Create / Destroy / Query ---- */

nt_material_t nt_material_create(const nt_material_create_desc_t *desc);
/* Destroys its params buffer too, so a material with params is destroyed outside a drawn frame. */
void nt_material_destroy(nt_material_t mat);
bool nt_material_valid(nt_material_t mat);
/* Replaces the borrowed program; INVALID clears it and assigning the same handle changes nothing.
 * Neither program is owned or destroyed here. mat must be valid.
 * Query readiness with
 * nt_gfx_program_ready(nt_material_get_info(mat)->program). */
void nt_material_set_program(nt_material_t mat, nt_program_t program);
const nt_material_info_t *nt_material_get_info(nt_material_t mat);

/* ---- Params and apply ---- */

/* Replaces the params bytes; size must equal the create-time params_size. Equal bytes change
 * nothing. A write follows nt_gfx_update_buffer: every draw of a frame reads the frame's last
 * write, so per-object values in one frame use separate materials, instance data or a Draw block. */
void nt_material_set_params(nt_material_t mat, const void *params, uint32_t size);

/* Binds the material for the next draws, after nt_gfx_bind_pipeline in a pass: its params buffer
 * at NT_GFX_SLOT_MATERIAL (a material without params unbinds the slot, so a program that reads
 * `Material` asserts at draw) and the complete texture set -- the material's textures resolved
 * now, followed by draw_textures (texture default or explicit sampler each; borrowed for the
 * call; NULL iff count is 0). A renderer supplies its atlas page, font or skin texture here, and
 * so may a game for its own per-draw texture. Repeated equal applies end CACHE in gfx. */
void nt_material_apply(nt_material_t mat, const nt_gfx_texture_binding_t *draw_textures, uint8_t count);

#endif /* NT_MATERIAL_H */
