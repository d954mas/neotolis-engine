#ifndef NT_MATERIAL_H
#define NT_MATERIAL_H

#include "core/nt_types.h"
#include "resource/nt_resource.h"

/* ---- Compile-time limits ---- */

#ifndef NT_MAX_MATERIALS
#define NT_MAX_MATERIALS 64
#endif

#define NT_MATERIAL_MAX_TEXTURES 4
#define NT_MATERIAL_MAX_PARAMS 4
#define NT_MATERIAL_MAX_ATTR_MAP 8
#define NT_MAX_PER_ENTITY_PARAMS 4

/* ---- Handle type ---- */

typedef struct {
    uint32_t id;
} nt_material_t;

#define NT_MATERIAL_INVALID ((nt_material_t){0})

/* ---- Blend mode enum ---- */

typedef enum {
    NT_BLEND_MODE_OPAQUE = 0,
    NT_BLEND_MODE_ALPHA,
} nt_blend_mode_t;

/* ---- Cull mode enum ---- */

typedef enum {
    NT_CULL_NONE = 0,
    NT_CULL_BACK,
    NT_CULL_FRONT,
} nt_cull_mode_t;

/* ---- Descriptor sub-types ---- */

typedef struct {
    const char *name;
    nt_resource_t resource;
} nt_material_texture_desc_t;

typedef struct {
    const char *name;
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
    nt_resource_t vs;
    nt_resource_t fs;
    nt_material_texture_desc_t textures[NT_MATERIAL_MAX_TEXTURES];
    uint8_t texture_count;
    nt_material_param_desc_t params[NT_MATERIAL_MAX_PARAMS];
    uint8_t param_count;
    nt_material_attr_desc_t attr_map[NT_MATERIAL_MAX_ATTR_MAP];
    uint8_t attr_map_count;
    nt_material_entity_param_desc_t entity_params[NT_MAX_PER_ENTITY_PARAMS];
    uint8_t entity_param_count;
    nt_blend_mode_t blend_mode;
    bool depth_test;
    bool depth_write;
    nt_cull_mode_t cull_mode;
    const char *label;
} nt_material_create_desc_t;

/* ---- Init descriptor ---- */

typedef struct {
    uint16_t max_materials; /* 0 = use NT_MAX_MATERIALS default */
} nt_material_desc_t;

/* ---- Material info (read-only query for render module) ---- */

typedef struct {
    uint32_t resolved_vs;
    uint32_t resolved_fs;
    uint32_t resolved_tex[NT_MATERIAL_MAX_TEXTURES];
    uint32_t tex_name_hashes[NT_MATERIAL_MAX_TEXTURES];
    uint8_t tex_count;
    float params[NT_MATERIAL_MAX_PARAMS][4];
    uint32_t param_name_hashes[NT_MATERIAL_MAX_PARAMS];
    uint8_t param_count;
    uint32_t attr_map_hashes[NT_MATERIAL_MAX_ATTR_MAP];
    uint8_t attr_map_locations[NT_MATERIAL_MAX_ATTR_MAP];
    uint8_t attr_map_count;
    uint32_t entity_param_hashes[NT_MAX_PER_ENTITY_PARAMS];
    uint8_t entity_param_count;
    nt_blend_mode_t blend_mode;
    bool depth_test;
    bool depth_write;
    nt_cull_mode_t cull_mode;
    uint32_t version;
    bool ready;
} nt_material_info_t;

/* ---- Lifecycle ---- */

nt_result_t nt_material_init(const nt_material_desc_t *desc);
void nt_material_shutdown(void);
void nt_material_step(void);

/* ---- Create / Destroy / Query ---- */

nt_material_t nt_material_create(const nt_material_create_desc_t *desc);
void nt_material_destroy(nt_material_t mat);
bool nt_material_valid(nt_material_t mat);
const nt_material_info_t *nt_material_get_info(nt_material_t mat);

#endif /* NT_MATERIAL_H */
