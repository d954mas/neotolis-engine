#include "material/nt_material.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"

/* ---- Handle encoding ---- */

#define NT_MAT_SLOT_SHIFT 16
#define NT_MAT_SLOT_MASK 0xFFFF

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

    /* Slot management */
    uint32_t id; /* generation << 16 | slot_index, 0 = free */
} nt_material_slot_t;

/* ---- Module state ---- */

static struct {
    nt_material_slot_t *slots; /* [capacity+1], index 0 reserved */
    uint32_t *free_queue;      /* stack of free slot indices */
    uint32_t queue_top;
    uint32_t capacity;
    bool initialized;
} s_mat;

/* ---- Lifecycle ---- */

nt_result_t nt_material_init(const nt_material_desc_t *desc) {
    if (s_mat.initialized) {
        return NT_ERR_INIT_FAILED;
    }

    uint32_t cap = NT_MAX_MATERIALS;
    if (desc && desc->max_materials > 0) {
        cap = desc->max_materials;
    }

    s_mat.slots = (nt_material_slot_t *)calloc(cap + 1, sizeof(nt_material_slot_t));
    if (!s_mat.slots) {
        return NT_ERR_INIT_FAILED;
    }

    s_mat.free_queue = (uint32_t *)malloc(cap * sizeof(uint32_t));
    if (!s_mat.free_queue) {
        free(s_mat.slots);
        s_mat.slots = NULL;
        return NT_ERR_INIT_FAILED;
    }

    /* Fill free queue: stack with indices 1..capacity, lowest index on top */
    s_mat.queue_top = cap;
    for (uint32_t i = 0; i < cap; i++) {
        s_mat.free_queue[i] = cap - i;
    }

    s_mat.capacity = cap;
    s_mat.initialized = true;
    return NT_OK;
}

void nt_material_shutdown(void) {
    free(s_mat.slots);
    free(s_mat.free_queue);
    memset(&s_mat, 0, sizeof(s_mat));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_material_step(void) {
    if (!s_mat.initialized) {
        return;
    }

    for (uint32_t i = 1; i <= s_mat.capacity; i++) {
        nt_material_slot_t *mat = &s_mat.slots[i];
        if (mat->id == 0) {
            continue; /* free slot */
        }

        /* Resolve shaders */
        uint32_t vs = nt_resource_get(mat->vs_resource);
        uint32_t fs = nt_resource_get(mat->fs_resource);

        /* Change detection */
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

nt_material_t nt_material_create(const nt_material_create_desc_t *desc) {
    if (!s_mat.initialized || !desc) {
        return NT_MATERIAL_INVALID;
    }

    /* Pop from free queue */
    if (s_mat.queue_top == 0) {
        return NT_MATERIAL_INVALID; /* pool full */
    }

    s_mat.queue_top--;
    uint32_t slot_index = s_mat.free_queue[s_mat.queue_top];

    nt_material_slot_t *slot = &s_mat.slots[slot_index];

    /* Increment generation */
    uint32_t prev_gen = slot->id >> NT_MAT_SLOT_SHIFT;
    uint32_t new_gen = prev_gen + 1;
    uint32_t id = (new_gen << NT_MAT_SLOT_SHIFT) | slot_index;
    slot->id = id;

    /* Clear info */
    memset(&slot->info, 0, sizeof(slot->info));

    /* Store resource handles */
    slot->vs_resource = desc->vs;
    slot->fs_resource = desc->fs;

    /* Textures: clamp count */
    uint8_t tex_count = desc->texture_count;
    if (tex_count > NT_MATERIAL_MAX_TEXTURES) {
        tex_count = NT_MATERIAL_MAX_TEXTURES;
    }
    slot->info.tex_count = tex_count;
    for (uint8_t i = 0; i < tex_count; i++) {
        slot->tex_resources[i] = desc->textures[i].resource;
        slot->info.tex_name_hashes[i] = desc->textures[i].name ? nt_hash32_str(desc->textures[i].name).value : 0;
    }

    /* Params: clamp count */
    uint8_t param_count = desc->param_count;
    if (param_count > NT_MATERIAL_MAX_PARAMS) {
        param_count = NT_MATERIAL_MAX_PARAMS;
    }
    slot->info.param_count = param_count;
    for (uint8_t i = 0; i < param_count; i++) {
        memcpy(slot->info.params[i], desc->params[i].value, sizeof(float) * 4);
        slot->info.param_name_hashes[i] = desc->params[i].name ? nt_hash32_str(desc->params[i].name).value : 0;
    }

    /* Attr map: clamp count */
    uint8_t attr_count = desc->attr_map_count;
    if (attr_count > NT_MATERIAL_MAX_ATTR_MAP) {
        attr_count = NT_MATERIAL_MAX_ATTR_MAP;
    }
    slot->info.attr_map_count = attr_count;
    for (uint8_t i = 0; i < attr_count; i++) {
        slot->info.attr_map_hashes[i] = desc->attr_map[i].stream_name ? nt_hash32_str(desc->attr_map[i].stream_name).value : 0;
        slot->info.attr_map_locations[i] = desc->attr_map[i].location;
    }

    /* Entity params: clamp count */
    uint8_t ep_count = desc->entity_param_count;
    if (ep_count > NT_MAX_PER_ENTITY_PARAMS) {
        ep_count = NT_MAX_PER_ENTITY_PARAMS;
    }
    slot->info.entity_param_count = ep_count;
    for (uint8_t i = 0; i < ep_count; i++) {
        slot->info.entity_param_hashes[i] = desc->entity_params[i].name ? nt_hash32_str(desc->entity_params[i].name).value : 0;
    }

    /* Render state */
    slot->info.blend_mode = desc->blend_mode;
    slot->info.depth_test = desc->depth_test;
    slot->info.depth_write = desc->depth_write;
    slot->info.cull_mode = desc->cull_mode;

    /* Initial state */
    slot->info.version = 0;
    slot->info.ready = false;
    slot->last_vs = 0;
    slot->last_fs = 0;

    return (nt_material_t){.id = id};
}

void nt_material_destroy(nt_material_t mat) {
    if (mat.id == 0 || !s_mat.initialized) {
        return;
    }

    uint32_t slot_index = mat.id & NT_MAT_SLOT_MASK;
    if (slot_index == 0 || slot_index > s_mat.capacity) {
        return;
    }

    nt_material_slot_t *slot = &s_mat.slots[slot_index];
    if (slot->id != mat.id) {
        return; /* stale handle -- no-op */
    }

    /* Increment generation, clear slot index bits */
    uint32_t gen = (mat.id >> NT_MAT_SLOT_SHIFT) + 1;
    slot->id = gen << NT_MAT_SLOT_SHIFT;

    /* Push to free queue */
    s_mat.free_queue[s_mat.queue_top] = slot_index;
    s_mat.queue_top++;
}

bool nt_material_valid(nt_material_t mat) {
    if (mat.id == 0 || !s_mat.initialized) {
        return false;
    }

    uint32_t slot_index = mat.id & NT_MAT_SLOT_MASK;
    if (slot_index == 0 || slot_index > s_mat.capacity) {
        return false;
    }

    return s_mat.slots[slot_index].id == mat.id;
}

const nt_material_info_t *nt_material_get_info(nt_material_t mat) {
    if (mat.id == 0 || !s_mat.initialized) {
        return NULL;
    }

    uint32_t slot_index = mat.id & NT_MAT_SLOT_MASK;
    if (slot_index == 0 || slot_index > s_mat.capacity) {
        return NULL;
    }

    nt_material_slot_t *slot = &s_mat.slots[slot_index];
    if (slot->id != mat.id) {
        return NULL; /* stale handle */
    }

    return &slot->info;
}
