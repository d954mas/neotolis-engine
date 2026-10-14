#include "material/nt_material.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "pool/nt_pool.h"

/* ---- Module state ---- */

typedef struct {
    nt_material_info_t info; /* the public, read-only view */
    nt_resource_t tex_resources[NT_MATERIAL_MAX_TEXTURES];
    uint32_t tex_name_hashes[NT_MATERIAL_MAX_TEXTURES];
    nt_sampler_t tex_samplers[NT_MATERIAL_MAX_TEXTURES];
    uint8_t tex_count;
    uint16_t params_size;
    nt_buffer_t params_buffer;                   /* 0 without params, or while a lost context leaves it to the restore */
    uint8_t params[NT_MATERIAL_MAX_PARAM_BYTES]; /* the CPU copy a restore uploads again */
} nt_material_record_t;

static struct {
    nt_pool_t pool;
    nt_material_record_t *slots; /* [capacity+1], index 0 reserved */
    bool initialized;
} s_mat;

static nt_buffer_t make_params_buffer(const nt_material_record_t *rec) {
    return nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .data = rec->params,
        .size = rec->params_size,
        .label = rec->info.label != NULL ? rec->info.label : "material_params",
    });
}

/* ---- Lifecycle ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_material_init(const nt_material_desc_t *desc) {
    NT_ASSERT(!s_mat.initialized);      /* double init */
    NT_ASSERT(desc);                    /* NULL descriptor */
    NT_ASSERT(desc->max_materials > 0); /* must specify capacity */

    nt_pool_init(&s_mat.pool, desc->max_materials);

    s_mat.slots = (nt_material_record_t *)calloc((size_t)desc->max_materials + 1, sizeof(nt_material_record_t));
    NT_ASSERT(s_mat.slots); /* alloc fail at init = fatal */

    s_mat.initialized = true;
}

void nt_material_shutdown(void) {
    if (!s_mat.initialized) {
        return;
    }
    for (uint32_t i = 1; i <= s_mat.pool.capacity; i++) {
        if (nt_pool_slot_alive(&s_mat.pool, i) && s_mat.slots[i].params_buffer.id != 0) {
            nt_gfx_destroy_buffer(s_mat.slots[i].params_buffer);
        }
    }
    free(s_mat.slots);
    nt_pool_shutdown(&s_mat.pool);
    memset(&s_mat, 0, sizeof(s_mat));
}

void nt_material_restore_gpu(void) {
    /* A shared restore sequence may name a module the game never initialized. */
    if (!s_mat.initialized) {
        return;
    }
    for (uint32_t i = 1; i <= s_mat.pool.capacity; i++) {
        nt_material_record_t *rec = &s_mat.slots[i];
        if (!nt_pool_slot_alive(&s_mat.pool, i) || rec->params_size == 0) {
            continue;
        }
        /* Destroy first: a full buffer pool has no headroom for the new name. */
        if (rec->params_buffer.id != 0) {
            nt_gfx_destroy_buffer(rec->params_buffer);
        }
        rec->params_buffer = make_params_buffer(rec);
    }
}

/* ---- Create / Destroy / Query ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_material_t nt_material_create(const nt_material_create_desc_t *desc) {
    NT_ASSERT(s_mat.initialized); /* create before init */
    NT_ASSERT(desc);              /* NULL descriptor */

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
    NT_ASSERT(desc->params_size <= NT_MATERIAL_MAX_PARAM_BYTES && desc->params_size % 16U == 0 && (desc->params != NULL) == (desc->params_size > 0) &&
              "material params: NULL with size 0, or a multiple of 16 up to NT_MATERIAL_MAX_PARAM_BYTES");

    uint32_t id = nt_pool_alloc(&s_mat.pool);
    if (id == 0) {
        NT_LOG_ERROR("pool full -- increase max_materials");
        return NT_MATERIAL_INVALID;
    }

    nt_material_record_t *rec = &s_mat.slots[nt_pool_slot_index(id)];
    /* The whole record: a slot reused in the same frame must not inherit the previous buffer. */
    memset(rec, 0, sizeof(*rec));
    nt_material_info_t *info = &rec->info;

    info->program = desc->program;

    /* Textures */
    NT_ASSERT(desc->texture_count <= NT_MATERIAL_MAX_TEXTURES);
    rec->tex_count = desc->texture_count;
    for (uint8_t i = 0; i < desc->texture_count; i++) {
        /* A slot with no uniform name is a declaration nothing can bind. */
        NT_ASSERT(desc->textures[i].name != NULL && "material texture slot needs a sampler uniform name");
        rec->tex_resources[i] = desc->textures[i].resource;
        rec->tex_name_hashes[i] = nt_hash32_str(desc->textures[i].name).value;
        rec->tex_samplers[i] = desc->textures[i].sampler;
        for (uint8_t j = 0; j < i; j++) {
            /* Two slots on one sampler name would fight over its unit at every draw. */
            NT_ASSERT(rec->tex_name_hashes[j] != rec->tex_name_hashes[i] && "two material texture slots name the same sampler uniform");
        }
    }

    /* Attr map */
    info->attr_map_count = desc->attr_map_count;
    for (uint8_t i = 0; i < desc->attr_map_count; i++) {
        info->attr_map_hashes[i] = desc->attr_map[i].stream_name ? nt_hash32_str(desc->attr_map[i].stream_name).value : 0;
        info->attr_map_locations[i] = desc->attr_map[i].location;
        memcpy(info->attr_map_defaults[i], desc->attr_map[i].default_value, sizeof(float) * 4);
    }
    info->has_attr_defaults = desc->has_attr_defaults;

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

    /* Debug label (caller must ensure static storage / string literal) */
    info->label = desc->label;

    /* Params: on a lost context the buffer stays 0 until nt_material_restore_gpu. */
    rec->params_size = desc->params_size;
    if (desc->params_size > 0) {
        memcpy(rec->params, desc->params, desc->params_size);
        rec->params_buffer = make_params_buffer(rec);
    }

    return (nt_material_t){.id = id};
}

void nt_material_destroy(nt_material_t mat) {
    NT_ASSERT(s_mat.initialized); /* destroy before init */
    if (mat.id == 0) {
        return;
    }
    if (!nt_pool_valid(&s_mat.pool, mat.id)) {
        return; /* a stale handle: already destroyed */
    }
    const nt_buffer_t buffer = s_mat.slots[nt_pool_slot_index(mat.id)].params_buffer;
    if (buffer.id != 0) {
        nt_gfx_destroy_buffer(buffer);
    }
    nt_pool_free(&s_mat.pool, mat.id);
}

bool nt_material_valid(nt_material_t mat) {
    if (!s_mat.initialized) {
        return false;
    }
    return nt_pool_valid(&s_mat.pool, mat.id);
}

/* Returns the record of a valid handle, or NULL */
static nt_material_record_t *get_record(nt_material_t mat) {
    NT_ASSERT(s_mat.initialized && "material module not initialized");
    if (mat.id == 0 || !nt_pool_valid(&s_mat.pool, mat.id)) {
        return NULL;
    }
    return &s_mat.slots[nt_pool_slot_index(mat.id)];
}

const nt_material_info_t *nt_material_get_info(nt_material_t mat) {
    const nt_material_record_t *rec = get_record(mat);
    return rec != NULL ? &rec->info : NULL;
}

void nt_material_set_program(nt_material_t mat, nt_program_t program) {
    nt_material_record_t *rec = get_record(mat);
    NT_ASSERT(rec && "set_program on invalid material handle");
    /* A plain store: assigning the handle the material already holds changes
     * nothing, so a per-frame gate can call this unconditionally. */
    rec->info.program = program;
}

/* ---- Params and apply ---- */

void nt_material_set_params(nt_material_t mat, const void *params, uint32_t size) {
    nt_material_record_t *rec = get_record(mat);
    NT_ASSERT(rec && "set_params on invalid material handle");
    NT_ASSERT(params != NULL && size == rec->params_size && size > 0 && "set_params: size differs from the create-time params_size");
    if (memcmp(rec->params, params, size) == 0) {
        return;
    }
    memcpy(rec->params, params, size);
    /* While lost the copy is all there is: the restore uploads it. */
    if (rec->params_buffer.id != 0) {
        nt_gfx_update_buffer(rec->params_buffer, 0, rec->params, size);
    }
}

void nt_material_apply(nt_material_t mat, const nt_gfx_texture_binding_t *draw_textures, uint8_t count) {
    const nt_material_record_t *rec = get_record(mat);
    NT_ASSERT(rec && "nt_material_apply on invalid material handle");
    NT_ASSERT((draw_textures != NULL || count == 0) && rec->tex_count + count <= NT_GFX_MAX_TEXTURE_SLOTS && "nt_material_apply: draw textures");
    nt_gfx_bind_uniform_buffer(NT_GFX_SLOT_MATERIAL, rec->params_buffer);
    nt_gfx_texture_binding_t bindings[NT_GFX_MAX_TEXTURE_SLOTS];
    for (uint8_t t = 0; t < rec->tex_count; t++) {
        bindings[t] = (nt_gfx_texture_binding_t){
            .name = {.value = rec->tex_name_hashes[t]},
            .texture = {.id = nt_resource_get(rec->tex_resources[t])},
            .sampler = rec->tex_samplers[t],
        };
    }
    for (uint8_t t = 0; t < count; t++) {
        bindings[rec->tex_count + t] = draw_textures[t];
    }
    nt_gfx_apply_texture_bindings(bindings, (uint8_t)(rec->tex_count + count));
}
