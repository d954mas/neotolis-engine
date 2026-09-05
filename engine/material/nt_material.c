#include "material/nt_material.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "pool/nt_pool.h"

/* ---- Module state ---- */

static struct {
    nt_pool_t pool;
    nt_material_info_t *slots; /* [capacity+1], index 0 reserved */
    bool initialized;
} s_mat;

/* ---- Lifecycle ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_material_init(const nt_material_desc_t *desc) {
    NT_ASSERT(!s_mat.initialized);      /* double init */
    NT_ASSERT(desc);                    /* NULL descriptor */
    NT_ASSERT(desc->max_materials > 0); /* must specify capacity */
    if (s_mat.initialized || !desc || desc->max_materials == 0) {
        return NT_ERR_INIT_FAILED;
    }

    nt_pool_init(&s_mat.pool, desc->max_materials);

    s_mat.slots = (nt_material_info_t *)calloc((size_t)desc->max_materials + 1, sizeof(nt_material_info_t));
    NT_ASSERT(s_mat.slots); /* alloc fail at init = fatal */

    s_mat.initialized = true;
    return NT_OK;
}

void nt_material_shutdown(void) {
    if (!s_mat.initialized) {
        return;
    }
    free(s_mat.slots);
    nt_pool_shutdown(&s_mat.pool);
    memset(&s_mat, 0, sizeof(s_mat));
}

/* ---- Create / Destroy / Query ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_material_t nt_material_create(const nt_material_create_desc_t *desc) {
    NT_ASSERT(s_mat.initialized); /* create before init */
    NT_ASSERT(desc);              /* NULL descriptor */
    if (!s_mat.initialized || !desc) {
        return NT_MATERIAL_INVALID;
    }

    /* Range-check before a slot is taken: the cache keys pack these into fixed bit
     * lanes. Unconditional -- a disabled blend still has to hold valid values. */
    NT_ASSERT(desc->blend.src_rgb <= NT_BLEND_SRC_ALPHA_SATURATE && desc->blend.dst_rgb <= NT_BLEND_SRC_ALPHA_SATURATE && desc->blend.src_alpha <= NT_BLEND_SRC_ALPHA_SATURATE &&
              desc->blend.dst_alpha <= NT_BLEND_SRC_ALPHA_SATURATE && "invalid blend factor");
    NT_ASSERT(desc->blend.op_rgb <= NT_BLEND_OP_MAX && desc->blend.op_alpha <= NT_BLEND_OP_MAX && "invalid blend op");
    NT_ASSERT((uint32_t)desc->cull_mode <= NT_CULL_FRONT && "invalid cull_mode -- use NT_CULL_NONE/BACK/FRONT");
    NT_ASSERT(desc->attr_map_count <= NT_MATERIAL_MAX_ATTR_MAP);
    for (uint8_t i = 0; i < desc->attr_map_count; i++) {
        NT_ASSERT(desc->attr_map[i].location < NT_GFX_MAX_VERTEX_ATTRS && "attr_map location out of range");
    }

    uint32_t id = nt_pool_alloc(&s_mat.pool);
    if (id == 0) {
        NT_LOG_ERROR("pool full -- increase max_materials");
        return NT_MATERIAL_INVALID;
    }

    uint32_t slot_index = nt_pool_slot_index(id);
    nt_material_info_t *info = &s_mat.slots[slot_index];

    /* Clear slot */
    memset(info, 0, sizeof(*info));

    info->program = desc->program;

    /* Textures */
    NT_ASSERT(desc->texture_count <= NT_MATERIAL_MAX_TEXTURES);
    info->tex_count = desc->texture_count;
    for (uint8_t i = 0; i < desc->texture_count; i++) {
        /* A slot with no uniform name is a declaration nothing can bind. */
        NT_ASSERT(desc->textures[i].name != NULL && "material texture slot needs a sampler uniform name");
        info->tex_resources[i] = desc->textures[i].resource;
        info->tex_name_hashes[i] = nt_hash32_str(desc->textures[i].name).value;
        info->tex_samplers[i] = desc->textures[i].sampler;
        for (uint8_t j = 0; j < i; j++) {
            /* Two slots on one sampler name would fight over its unit at every draw. */
            NT_ASSERT(info->tex_name_hashes[j] != info->tex_name_hashes[i] && "two material texture slots name the same sampler uniform");
        }
    }

    /* Params */
    NT_ASSERT(desc->param_count <= NT_MATERIAL_MAX_PARAMS);
    info->param_count = desc->param_count;
    for (uint8_t i = 0; i < desc->param_count; i++) {
        NT_ASSERT(desc->params[i].name != NULL && "material param needs a uniform name");
        memcpy(info->params[i], desc->params[i].value, sizeof(float) * 4);
        info->param_name_hashes[i] = nt_hash32_str(desc->params[i].name).value;
    }

    /* Attr map */
    info->attr_map_count = desc->attr_map_count;
    for (uint8_t i = 0; i < desc->attr_map_count; i++) {
        info->attr_map_hashes[i] = desc->attr_map[i].stream_name ? nt_hash32_str(desc->attr_map[i].stream_name).value : 0;
        info->attr_map_locations[i] = desc->attr_map[i].location;
    }

    /* Entity params */
    NT_ASSERT(desc->entity_param_count <= NT_MAX_PER_ENTITY_PARAMS);
    info->entity_param_count = desc->entity_param_count;
    for (uint8_t i = 0; i < desc->entity_param_count; i++) {
        info->entity_param_hashes[i] = desc->entity_params[i].name ? nt_hash32_str(desc->entity_params[i].name).value : 0;
    }

    /* Render state */
    memcpy(info->blend.constant_color, desc->blend.constant_color, sizeof(info->blend.constant_color));
    info->blend.src_rgb = desc->blend.src_rgb;
    info->blend.dst_rgb = desc->blend.dst_rgb;
    info->blend.src_alpha = desc->blend.src_alpha;
    info->blend.dst_alpha = desc->blend.dst_alpha;
    info->blend.op_rgb = desc->blend.op_rgb;
    info->blend.op_alpha = desc->blend.op_alpha;
    info->blend.enabled = desc->blend.enabled;
    info->depth_test = desc->depth_test;
    info->depth_write = desc->depth_write;
    info->cull_mode = desc->cull_mode;
    NT_ASSERT((uint32_t)desc->color_mode <= NT_COLOR_MODE_FLOAT4 && "invalid color_mode -- use NT_COLOR_MODE_NONE/RGBA8/FLOAT4");
    info->color_mode = desc->color_mode;

    /* Debug label (caller must ensure static storage / string literal) */
    info->label = desc->label;

    return (nt_material_t){.id = id};
}

void nt_material_destroy(nt_material_t mat) {
    NT_ASSERT(s_mat.initialized); /* destroy before init */
    if (mat.id == 0 || !s_mat.initialized) {
        return;
    }
    nt_pool_free(&s_mat.pool, mat.id);
}

bool nt_material_valid(nt_material_t mat) {
    if (!s_mat.initialized) {
        return false;
    }
    return nt_pool_valid(&s_mat.pool, mat.id);
}

/* Returns mutable info pointer for a valid handle, or NULL */
static nt_material_info_t *get_mutable_info(nt_material_t mat) {
    NT_ASSERT(s_mat.initialized && "material module not initialized");
    if (!s_mat.initialized || mat.id == 0) {
        return NULL;
    }
    if (!nt_pool_valid(&s_mat.pool, mat.id)) {
        return NULL;
    }
    return &s_mat.slots[nt_pool_slot_index(mat.id)];
}

const nt_material_info_t *nt_material_get_info(nt_material_t mat) { return get_mutable_info(mat); }

void nt_material_set_program(nt_material_t mat, nt_program_t program) {
    nt_material_info_t *info = get_mutable_info(mat);
    NT_ASSERT(info && "set_program on invalid material handle");
    /* A plain store: assigning the handle the material already holds changes
     * nothing, so a per-frame gate can call this unconditionally. */
    info->program = program;
}

/* ---- Runtime param mutation ---- */

/* Returns param index for given name hash, or -1 if not found */
static int find_param_index(const nt_material_info_t *info, uint32_t name_hash) {
    for (uint8_t i = 0; i < info->param_count; i++) {
        if (info->param_name_hashes[i] == name_hash) {
            return (int)i;
        }
    }
    return -1;
}

bool nt_material_has_param_h(nt_material_t mat, nt_hash32_t name_hash) {
    const nt_material_info_t *info = get_mutable_info(mat);
    if (!info) {
        return false;
    }
    return find_param_index(info, name_hash.value) >= 0;
}

bool nt_material_has_param(nt_material_t mat, const char *name) { return nt_material_has_param_h(mat, nt_hash32_str(name)); }

/* Resolve info + param index, or return -1 on failure (asserts in debug) */
static int resolve_param(nt_material_t mat, uint32_t name_hash, nt_material_info_t **out_info) {
    nt_material_info_t *info = get_mutable_info(mat);
    NT_ASSERT(info && "set_param on invalid material handle");
    if (!info) {
        return -1;
    }
    int idx = find_param_index(info, name_hash);
    NT_ASSERT(idx >= 0 && "material param not found -- use nt_material_has_param to check");
    if (idx < 0) {
        return -1;
    }
    *out_info = info;
    return idx;
}

void nt_material_set_param_h(nt_material_t mat, nt_hash32_t name_hash, const float value[4]) {
    nt_material_info_t *info = NULL;
    int idx = resolve_param(mat, name_hash.value, &info);
    if (idx < 0) {
        return;
    }
    memcpy(info->params[idx], value, sizeof(float) * 4);
}

void nt_material_set_param(nt_material_t mat, const char *name, const float value[4]) { nt_material_set_param_h(mat, nt_hash32_str(name), value); }

void nt_material_set_param_component_h(nt_material_t mat, nt_hash32_t name_hash, uint8_t index, float value) {
    NT_ASSERT(index <= 3 && "component index must be 0-3");
    if (index > 3) {
        return;
    }
    nt_material_info_t *info = NULL;
    int idx = resolve_param(mat, name_hash.value, &info);
    if (idx < 0) {
        return;
    }
    info->params[idx][index] = value;
}

void nt_material_set_param_component(nt_material_t mat, const char *name, uint8_t index, float value) { nt_material_set_param_component_h(mat, nt_hash32_str(name), index, value); }
