#include "material/nt_material.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "pool/nt_pool.h"

/* ---- Internal slot struct ---- */

/* A material that never gets a program renders nothing, and the renderers'
 * not-ready branches skip it in silence -- the game filters it out before the
 * engine ever sees it, so no assert can fire. Only the module that owns `ready`
 * can notice. Steps, not seconds: a slow pack legitimately delays readiness. */
#define NT_MATERIAL_NOT_READY_WARN_STEPS 600

typedef struct {
    /* Read-only query data -- MUST be first member so nt_material_info_t* can alias */
    nt_material_info_t info;

    /* Creation-time resource handles (not in info) */
    nt_resource_t tex_resources[NT_MATERIAL_MAX_TEXTURES];

    uint16_t not_ready_steps; /* consecutive steps without a program */
    bool warned_not_ready;    /* one-shot latch, re-armed when the material goes ready */
    bool ever_ready;          /* cleared only at creation; drives the destroy-time warn */
} nt_material_slot_t;

/* ---- Module state ---- */

static struct {
    nt_pool_t pool;
    nt_material_slot_t *slots; /* [capacity+1], index 0 reserved */
    bool initialized;
#ifdef NT_TEST_ACCESS
    uint32_t not_ready_warn_count;
    uint32_t never_ready_destroy_count;
#endif
} s_mat;

static const char *material_label(const nt_material_info_t *info) { return info->label != NULL ? info->label : "(unlabeled)"; }

#ifdef NT_TEST_ACCESS
uint32_t nt_material_test_not_ready_warn_count(void) { return s_mat.not_ready_warn_count; }
uint32_t nt_material_test_never_ready_destroy_count(void) { return s_mat.never_ready_destroy_count; }
uint32_t nt_material_test_not_ready_warn_steps(void) { return NT_MATERIAL_NOT_READY_WARN_STEPS; }
#endif

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

    s_mat.slots = (nt_material_slot_t *)calloc((size_t)desc->max_materials + 1, sizeof(nt_material_slot_t));
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

        if (mat->info.ready) {
            mat->ever_ready = true;
            mat->not_ready_steps = 0;
            mat->warned_not_ready = false; /* re-arm: a later loss warns again */
        } else {
            if (mat->not_ready_steps < NT_MATERIAL_NOT_READY_WARN_STEPS) {
                mat->not_ready_steps++;
            }
            if (!mat->warned_not_ready && mat->not_ready_steps >= NT_MATERIAL_NOT_READY_WARN_STEPS) {
                NT_LOG_WARN("material '%s' has had no program for %u steps -- call nt_material_set_program()", material_label(&mat->info), (uint32_t)mat->not_ready_steps);
                mat->warned_not_ready = true;
#ifdef NT_TEST_ACCESS
                s_mat.not_ready_warn_count++;
#endif
            }
        }

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

    slot->info.program = desc->program;
    slot->info.ready = (desc->program.id != 0);
    /* Latched on assignment, not in step: a material created ready and destroyed
     * before the next step did receive a program. */
    slot->ever_ready = slot->info.ready;

    /* Textures */
    NT_ASSERT(desc->texture_count <= NT_MATERIAL_MAX_TEXTURES);
    slot->info.tex_count = desc->texture_count;
    for (uint8_t i = 0; i < desc->texture_count; i++) {
        slot->tex_resources[i] = desc->textures[i].resource;
        slot->info.tex_name_hashes[i] = desc->textures[i].name ? nt_hash32_str(desc->textures[i].name).value : 0;
        slot->info.tex_names[i] = desc->textures[i].name; /* must be static storage */
        slot->info.resolved_sampler[i] = desc->textures[i].sampler;
    }

    /* Params */
    NT_ASSERT(desc->param_count <= NT_MATERIAL_MAX_PARAMS);
    slot->info.param_count = desc->param_count;
    for (uint8_t i = 0; i < desc->param_count; i++) {
        memcpy(slot->info.params[i], desc->params[i].value, sizeof(float) * 4);
        slot->info.param_name_hashes[i] = desc->params[i].name ? nt_hash32_str(desc->params[i].name).value : 0;
        slot->info.param_names[i] = desc->params[i].name;
    }

    /* Attr map */
    NT_ASSERT(desc->attr_map_count <= NT_MATERIAL_MAX_ATTR_MAP);
    slot->info.attr_map_count = desc->attr_map_count;
    for (uint8_t i = 0; i < desc->attr_map_count; i++) {
        slot->info.attr_map_hashes[i] = desc->attr_map[i].stream_name ? nt_hash32_str(desc->attr_map[i].stream_name).value : 0;
        slot->info.attr_map_locations[i] = desc->attr_map[i].location;
    }

    /* Entity params */
    NT_ASSERT(desc->entity_param_count <= NT_MAX_PER_ENTITY_PARAMS);
    slot->info.entity_param_count = desc->entity_param_count;
    for (uint8_t i = 0; i < desc->entity_param_count; i++) {
        slot->info.entity_param_hashes[i] = desc->entity_params[i].name ? nt_hash32_str(desc->entity_params[i].name).value : 0;
    }

    /* Render state */
    memcpy(slot->info.blend.constant_color, desc->blend.constant_color, sizeof(slot->info.blend.constant_color));
    slot->info.blend.src_rgb = desc->blend.src_rgb;
    slot->info.blend.dst_rgb = desc->blend.dst_rgb;
    slot->info.blend.src_alpha = desc->blend.src_alpha;
    slot->info.blend.dst_alpha = desc->blend.dst_alpha;
    slot->info.blend.op_rgb = desc->blend.op_rgb;
    slot->info.blend.op_alpha = desc->blend.op_alpha;
    slot->info.blend.enabled = desc->blend.enabled;
    slot->info.depth_test = desc->depth_test;
    slot->info.depth_write = desc->depth_write;
    slot->info.cull_mode = desc->cull_mode;
    NT_ASSERT(desc->color_mode <= NT_COLOR_MODE_FLOAT4 && "invalid color_mode -- use NT_COLOR_MODE_NONE/RGBA8/FLOAT4");
    slot->info.color_mode = desc->color_mode;

    const nt_blend_state_t hash_blend = slot->info.blend.enabled ? slot->info.blend : nt_blend_opaque();
    slot->info.render_state_hash = nt_hash64(&hash_blend, sizeof(hash_blend)).value;
    slot->info.render_state_hash = slot->info.render_state_hash * 0x9E3779B97F4A7C15ULL + (uint64_t)slot->info.depth_test;
    slot->info.render_state_hash = slot->info.render_state_hash * 0x9E3779B97F4A7C15ULL + (uint64_t)slot->info.depth_write;
    slot->info.render_state_hash = slot->info.render_state_hash * 0x9E3779B97F4A7C15ULL + (uint64_t)slot->info.cull_mode;
    slot->info.render_state_hash = slot->info.render_state_hash * 0x9E3779B97F4A7C15ULL + (uint64_t)slot->info.color_mode;

    /* Debug label (caller must ensure static storage / string literal) */
    slot->info.label = desc->label;

    return (nt_material_t){.id = id};
}

void nt_material_destroy(nt_material_t mat) {
    NT_ASSERT(s_mat.initialized); /* destroy before init */
    if (mat.id == 0 || !s_mat.initialized) {
        return;
    }
    /* No threshold to tune and no false positive from a slow load: the material
     * is gone and never once rendered. */
    if (nt_pool_valid(&s_mat.pool, mat.id)) {
        nt_material_slot_t *slot = &s_mat.slots[nt_pool_slot_index(mat.id)];
        if (!slot->ever_ready) {
            NT_LOG_WARN("material '%s' destroyed without ever receiving a program", material_label(&slot->info));
#ifdef NT_TEST_ACCESS
            s_mat.never_ready_destroy_count++;
#endif
        }
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
    return &s_mat.slots[nt_pool_slot_index(mat.id)].info;
}

const nt_material_info_t *nt_material_get_info(nt_material_t mat) { return get_mutable_info(mat); }

void nt_material_set_program(nt_material_t mat, nt_program_t program) {
    nt_material_info_t *info = get_mutable_info(mat);
    NT_ASSERT(info && "set_program on invalid material handle");
    if (!info) {
        return;
    }
    if (info->program.id == program.id) {
        return;
    }
    info->program = program;
    info->ready = (program.id != 0);
    info->version++;
    if (program.id != 0) {
        s_mat.slots[nt_pool_slot_index(mat.id)].ever_ready = true;
    }
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
