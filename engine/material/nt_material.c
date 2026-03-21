#include "material/nt_material.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "pool/nt_pool.h"

/* ---- Internal slot struct ---- */

typedef struct {
    /* Read-only query data -- MUST be first member so nt_material_info_t* can alias */
    nt_material_info_t info;

    /* Creation-time resource handles (not in info) */
    nt_resource_t vs_resource;
    nt_resource_t fs_resource;
    nt_resource_t tex_resources[NT_MATERIAL_MAX_TEXTURES];

    /* Change tracking */
    uint32_t last_vs;
    uint32_t last_fs;
} nt_material_slot_t;

/* ---- Module state ---- */

static struct {
    nt_pool_t pool;
    nt_material_slot_t *slots; /* [capacity+1], index 0 reserved */
    bool initialized;
} s_mat;

/* ---- Lifecycle ---- */

nt_result_t nt_material_init(const nt_material_desc_t *desc) {
    NT_ASSERT(!s_mat.initialized);      /* double init */
    NT_ASSERT(desc);                    /* NULL descriptor */
    NT_ASSERT(desc->max_materials > 0); /* must specify capacity */
    if (s_mat.initialized || !desc || desc->max_materials == 0) {
        return NT_ERR_INIT_FAILED;
    }

    nt_pool_init(&s_mat.pool, desc->max_materials);

    s_mat.slots = (nt_material_slot_t *)calloc((size_t)desc->max_materials + 1, sizeof(nt_material_slot_t));
    NT_ASSERT_ALWAYS(s_mat.slots); /* alloc fail at init = fatal */

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_material_step(void) {
    NT_ASSERT(s_mat.initialized); /* step before init is a dev mistake */
    if (!s_mat.initialized) {
        return;
    }

    for (uint32_t i = 1; i <= s_mat.pool.capacity; i++) {
        if (!nt_pool_slot_alive(&s_mat.pool, i)) {
            continue;
        }

        nt_material_slot_t *mat = &s_mat.slots[i];

        /* Resolve shaders */
        uint32_t vs = nt_resource_get(mat->vs_resource);
        uint32_t fs = nt_resource_get(mat->fs_resource);

        /* Change detection (shaders only — texture changes don't affect pipeline) */
        if (vs != mat->last_vs || fs != mat->last_fs) {
            mat->info.version++;
            mat->last_vs = vs;
            mat->last_fs = fs;
        }

        mat->info.resolved_vs = vs;
        mat->info.resolved_fs = fs;
        mat->info.ready = (vs != 0 && fs != 0);

        /* Resolve textures */
        for (uint8_t t = 0; t < mat->info.tex_count; t++) {
            mat->info.resolved_tex[t] = nt_resource_get(mat->tex_resources[t]);
        }
    }
}

/* ---- Create / Destroy / Query ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_material_t nt_material_create(const nt_material_create_desc_t *desc) {
    NT_ASSERT(s_mat.initialized); /* create before init */
    NT_ASSERT(desc);              /* NULL descriptor */
    if (!s_mat.initialized || !desc) {
        return NT_MATERIAL_INVALID;
    }

    uint32_t id = nt_pool_alloc(&s_mat.pool);
    if (id == 0) {
        NT_LOG_ERROR("pool full -- increase max_materials");
        return NT_MATERIAL_INVALID;
    }

    uint32_t slot_index = nt_pool_slot_index(id);
    nt_material_slot_t *slot = &s_mat.slots[slot_index];

    /* Clear slot */
    memset(slot, 0, sizeof(*slot));

    /* Store resource handles */
    slot->vs_resource = desc->vs;
    slot->fs_resource = desc->fs;

    /* Textures */
    NT_ASSERT_ALWAYS(desc->texture_count <= NT_MATERIAL_MAX_TEXTURES);
    slot->info.tex_count = desc->texture_count;
    for (uint8_t i = 0; i < desc->texture_count; i++) {
        slot->tex_resources[i] = desc->textures[i].resource;
        slot->info.tex_name_hashes[i] = desc->textures[i].name ? nt_hash32_str(desc->textures[i].name).value : 0;
        slot->info.tex_names[i] = desc->textures[i].name; /* must be static storage */
    }

    /* Params */
    NT_ASSERT_ALWAYS(desc->param_count <= NT_MATERIAL_MAX_PARAMS);
    slot->info.param_count = desc->param_count;
    for (uint8_t i = 0; i < desc->param_count; i++) {
        memcpy(slot->info.params[i], desc->params[i].value, sizeof(float) * 4);
        slot->info.param_name_hashes[i] = desc->params[i].name ? nt_hash32_str(desc->params[i].name).value : 0;
        slot->info.param_names[i] = desc->params[i].name;
    }

    /* Attr map */
    NT_ASSERT_ALWAYS(desc->attr_map_count <= NT_MATERIAL_MAX_ATTR_MAP);
    slot->info.attr_map_count = desc->attr_map_count;
    for (uint8_t i = 0; i < desc->attr_map_count; i++) {
        slot->info.attr_map_hashes[i] = desc->attr_map[i].stream_name ? nt_hash32_str(desc->attr_map[i].stream_name).value : 0;
        slot->info.attr_map_locations[i] = desc->attr_map[i].location;
    }

    /* Entity params */
    NT_ASSERT_ALWAYS(desc->entity_param_count <= NT_MAX_PER_ENTITY_PARAMS);
    slot->info.entity_param_count = desc->entity_param_count;
    for (uint8_t i = 0; i < desc->entity_param_count; i++) {
        slot->info.entity_param_hashes[i] = desc->entity_params[i].name ? nt_hash32_str(desc->entity_params[i].name).value : 0;
    }

    /* Render state */
    slot->info.blend_mode = desc->blend_mode;
    slot->info.depth_test = desc->depth_test;
    slot->info.depth_write = desc->depth_write;
    slot->info.cull_mode = desc->cull_mode;

    /* Debug label (caller must ensure static storage / string literal) */
    slot->info.label = desc->label;

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

const nt_material_info_t *nt_material_get_info(nt_material_t mat) {
    NT_ASSERT(s_mat.initialized); /* query before init */
    if (mat.id == 0 || !s_mat.initialized) {
        return NULL;
    }
    if (!nt_pool_valid(&s_mat.pool, mat.id)) {
        return NULL;
    }
    uint32_t slot_index = nt_pool_slot_index(mat.id);
    return &s_mat.slots[slot_index].info;
}
