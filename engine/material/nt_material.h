#ifndef NT_MATERIAL_H
#define NT_MATERIAL_H

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "render/nt_render_defs.h"
#include "resource/nt_resource.h"

/* ---- Compile-time limits ---- */

#define NT_MATERIAL_MAX_TEXTURES 4
#define NT_MATERIAL_MAX_PARAMS 4
#define NT_MATERIAL_MAX_ATTR_MAP 8
#define NT_MAX_PER_ENTITY_PARAMS 4

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
    const char *name; /* required sampler uniform name; hashed at create, not retained */
    nt_resource_t resource;
    nt_sampler_t sampler; /* override; .id==0 = use texture's asset-baked default */
} nt_material_texture_desc_t;

typedef struct {
    const char *name; /* required uniform name; hashed at create, not retained */
    float value[4];
} nt_material_param_desc_t;

typedef struct {
    const char *stream_name;
    uint8_t location;
} nt_material_attr_desc_t;

typedef struct {
    const char *name;
} nt_material_entity_param_desc_t;

/* ---- Creation descriptor ---- */

typedef struct {
    /* Borrowed: the material never links, destroys or checks the program. */
    nt_program_t program;
    nt_material_texture_desc_t textures[NT_MATERIAL_MAX_TEXTURES];
    uint8_t texture_count;
    nt_material_param_desc_t params[NT_MATERIAL_MAX_PARAMS];
    uint8_t param_count;
    nt_material_attr_desc_t attr_map[NT_MATERIAL_MAX_ATTR_MAP];
    uint8_t attr_map_count;
    nt_material_entity_param_desc_t entity_params[NT_MAX_PER_ENTITY_PARAMS];
    uint8_t entity_param_count;
    nt_blend_state_t blend;
    bool depth_test;
    bool depth_write;
    nt_cull_mode_t cull_mode;
    nt_color_mode_t color_mode; /* NT_COLOR_MODE_NONE (0) via zero-init */
    const char *label;          /* debug name — must be string literal or static storage */
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
     * program that died with the GL context or that its owner destroyed -- ask
     * nt_gfx_program_ready(program) before building a pipeline from it. */
    nt_program_t program;
    uint32_t resolved_tex[NT_MATERIAL_MAX_TEXTURES];
    uint32_t tex_name_hashes[NT_MATERIAL_MAX_TEXTURES];
    nt_sampler_t resolved_sampler[NT_MATERIAL_MAX_TEXTURES]; /* per-binding sampler override; .id==0 means use texture's default */
    uint8_t tex_count;
    float params[NT_MATERIAL_MAX_PARAMS][4];
    uint32_t param_name_hashes[NT_MATERIAL_MAX_PARAMS];
    uint8_t param_count;
    uint32_t attr_map_hashes[NT_MATERIAL_MAX_ATTR_MAP];
    uint8_t attr_map_locations[NT_MATERIAL_MAX_ATTR_MAP];
    uint8_t attr_map_count;
    uint32_t entity_param_hashes[NT_MAX_PER_ENTITY_PARAMS];
    uint8_t entity_param_count;
    nt_blend_state_t blend;
    bool depth_test;
    bool depth_write;
    nt_cull_mode_t cull_mode;
    nt_color_mode_t color_mode;
    uint64_t render_state_hash;
    const char *label; /* debug name (string literal, static storage) */
} nt_material_info_t;

/* ---- Lifecycle ---- */

nt_result_t nt_material_init(const nt_material_desc_t *desc);
void nt_material_shutdown(void);
void nt_material_step(void);

/* ---- Create / Destroy / Query ---- */

nt_material_t nt_material_create(const nt_material_create_desc_t *desc);
void nt_material_destroy(nt_material_t mat);
bool nt_material_valid(nt_material_t mat);
/* Replaces the borrowed program; INVALID clears it and assigning the same handle changes nothing.
 * Neither program is owned or destroyed here. mat must be valid.
 * Query readiness with
 * nt_gfx_program_ready(nt_material_get_info(mat)->program). */
void nt_material_set_program(nt_material_t mat, nt_program_t program);
const nt_material_info_t *nt_material_get_info(nt_material_t mat);

/* ---- Runtime param mutation ---- */

bool nt_material_has_param(nt_material_t mat, const char *name);
void nt_material_set_param(nt_material_t mat, const char *name, const float value[4]);
void nt_material_set_param_component(nt_material_t mat, const char *name, uint8_t index, float value);

/* Hash-based overloads -- cache nt_hash32_str() result to avoid per-frame rehashing */
bool nt_material_has_param_h(nt_material_t mat, nt_hash32_t name_hash);
void nt_material_set_param_h(nt_material_t mat, nt_hash32_t name_hash, const float value[4]);
void nt_material_set_param_component_h(nt_material_t mat, nt_hash32_t name_hash, uint8_t index, float value);

#endif /* NT_MATERIAL_H */
