#include "sprite_comp/nt_sprite_comp.h"

#include <stdlib.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#include "resource/nt_resource.h"

/* ---- Static state ---- */

static nt_comp_storage_t s_storage;

/* ---- SoA data arrays ---- */

static nt_resource_t *s_atlas;
static uint16_t *s_region_index;
static float (*s_origin)[2];
static uint8_t *s_flags;

/* ---- Callbacks ---- */

static void sprite_default(uint16_t idx) {
    s_atlas[idx] = NT_RESOURCE_INVALID;
    s_region_index[idx] = 0;
    s_origin[idx][0] = 0.0F;
    s_origin[idx][1] = 0.0F;
    s_flags[idx] = 0;
}

static void sprite_swap(uint16_t dst, uint16_t src) {
    s_atlas[dst] = s_atlas[src];
    s_region_index[dst] = s_region_index[src];
    memcpy(s_origin[dst], s_origin[src], sizeof(float) * 2);
    s_flags[dst] = s_flags[src];
}

static void sprite_on_destroy(nt_entity_t entity) {
    if (nt_comp_storage_has(&s_storage, entity)) {
        nt_comp_storage_remove(&s_storage, entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_sprite_comp_init(const nt_sprite_comp_desc_t *desc) {
    if (!desc || desc->capacity == 0) {
        return NT_ERR_INVALID_ARG;
    }

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, sprite_default, sprite_swap);
    if (res != NT_OK) {
        return res;
    }

    uint16_t cap = desc->capacity;
    s_atlas = (nt_resource_t *)calloc(cap, sizeof(nt_resource_t));
    s_region_index = (uint16_t *)calloc(cap, sizeof(uint16_t));
    s_origin = calloc(cap, sizeof(*s_origin));
    s_flags = (uint8_t *)calloc(cap, sizeof(uint8_t));

    if (!s_atlas || !s_region_index || !s_origin || !s_flags) {
        nt_sprite_comp_shutdown();
        return NT_ERR_INIT_FAILED;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "sprite",
        .has = nt_sprite_comp_has,
        .on_destroy = sprite_on_destroy,
    });

    return NT_OK;
}

void nt_sprite_comp_shutdown(void) {
    free(s_atlas);
    free(s_region_index);
    free(s_origin);
    free(s_flags);
    s_atlas = NULL;
    s_region_index = NULL;
    s_origin = NULL;
    s_flags = NULL;
    nt_comp_storage_shutdown(&s_storage);
}

/* ---- Per-entity operations ---- */

bool nt_sprite_comp_add(nt_entity_t entity) { return nt_comp_storage_add(&s_storage, entity) != NT_INVALID_COMP_INDEX; }

bool nt_sprite_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_sprite_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }

/* ---- Region binding ---- */

void nt_sprite_comp_set_region(nt_entity_t entity, nt_resource_t atlas, uint16_t region_index) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    NT_ASSERT(nt_resource_get_user_data(atlas) != NULL);
    NT_ASSERT(region_index < nt_atlas_region_count(atlas));
    s_atlas[idx] = atlas;
    s_region_index[idx] = region_index;
    s_flags[idx] &= (uint8_t)~NT_SPRITE_FLAG_ORIGIN_OV;
}

void nt_sprite_comp_set_region_by_hash(nt_entity_t entity, nt_resource_t atlas, uint64_t name_hash) {
    uint32_t result = nt_atlas_find_region(atlas, name_hash);
    NT_ASSERT(result != NT_ATLAS_INVALID_REGION);
    NT_ASSERT(result <= UINT16_MAX);
    nt_sprite_comp_set_region(entity, atlas, (uint16_t)result);
}

/* ---- Origin override ---- */

void nt_sprite_comp_set_origin(nt_entity_t entity, float origin_x, float origin_y) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    s_origin[idx][0] = origin_x;
    s_origin[idx][1] = origin_y;
    s_flags[idx] |= (uint8_t)NT_SPRITE_FLAG_ORIGIN_OV;
}

void nt_sprite_comp_reset_origin(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    s_flags[idx] &= (uint8_t)~NT_SPRITE_FLAG_ORIGIN_OV;
}

/* ---- Flip control ---- */

void nt_sprite_comp_set_flip(nt_entity_t entity, bool flip_x, bool flip_y) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    uint8_t mask = (uint8_t)~(NT_SPRITE_FLAG_FLIP_X | NT_SPRITE_FLAG_FLIP_Y);
    uint8_t bits = (uint8_t)((flip_x ? NT_SPRITE_FLAG_FLIP_X : 0U) | (flip_y ? NT_SPRITE_FLAG_FLIP_Y : 0U));
    s_flags[idx] = (uint8_t)((s_flags[idx] & mask) | bits);
}

/* ---- Read accessors ---- */

const nt_resource_t *nt_sprite_comp_atlas(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_atlas[idx];
}

const uint16_t *nt_sprite_comp_region_index(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_region_index[idx];
}

const float *nt_sprite_comp_origin(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return s_origin[idx];
}

const uint8_t *nt_sprite_comp_flags(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_flags[idx];
}
