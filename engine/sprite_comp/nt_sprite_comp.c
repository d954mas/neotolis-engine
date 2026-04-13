#include "sprite_comp/nt_sprite_comp.h"

#include <stdlib.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#include "resource/nt_resource.h"

/* ---- Static state ---- */

static nt_comp_storage_t s_storage;
static uint32_t s_last_publication_epoch;
static bool s_sync_dirty;

/* ---- SoA data arrays ---- */

static nt_resource_t *s_atlas;
static uint64_t *s_region_hash;
static uint16_t *s_region_index;   /* cached resolved index */
static uint32_t *s_atlas_revision; /* cached atlas snapshot revision */
static float (*s_origin)[2];       /* effective origin */
static uint8_t *s_flags;

/* ---- Helpers ---- */

static void sprite_clear_resolved(uint16_t idx) {
    s_region_index[idx] = 0;
    s_atlas_revision[idx] = 0;
    s_flags[idx] &= (uint8_t)~NT_SPRITE_FLAG_RESOLVED;
}

static void sprite_set_authored_origin(uint16_t idx, const nt_texture_region_t *region) {
    NT_ASSERT(region != NULL);
    if ((s_flags[idx] & NT_SPRITE_FLAG_ORIGIN_OV) != 0) {
        return;
    }

    s_origin[idx][0] = region->origin_x;
    s_origin[idx][1] = region->origin_y;
}

static void sprite_resolve_dense(uint16_t idx) {
    nt_resource_t atlas = s_atlas[idx];
    if (atlas.id == 0) {
        sprite_clear_resolved(idx);
        return;
    }

    if (!nt_resource_is_ready(atlas)) {
        sprite_clear_resolved(idx);
        return;
    }

    uint32_t revision = nt_atlas_revision(atlas);
    if ((s_flags[idx] & NT_SPRITE_FLAG_RESOLVED) != 0 && s_atlas_revision[idx] == revision) {
        return;
    }

    uint32_t resolved = nt_atlas_find_region(atlas, s_region_hash[idx]);
    NT_ASSERT(resolved != NT_ATLAS_INVALID_REGION);
    NT_ASSERT(resolved <= UINT16_MAX);

    s_region_index[idx] = (uint16_t)resolved;
    s_atlas_revision[idx] = revision;
    s_flags[idx] |= (uint8_t)NT_SPRITE_FLAG_RESOLVED;
    sprite_set_authored_origin(idx, nt_atlas_get_region(atlas, resolved));
}

/* ---- Callbacks ---- */

static void sprite_default(uint16_t idx) {
    s_atlas[idx] = NT_RESOURCE_INVALID;
    s_region_hash[idx] = 0;
    s_region_index[idx] = 0;
    s_atlas_revision[idx] = 0;
    s_origin[idx][0] = 0.0F;
    s_origin[idx][1] = 0.0F;
    s_flags[idx] = 0;
}

static void sprite_swap(uint16_t dst, uint16_t src) {
    s_atlas[dst] = s_atlas[src];
    s_region_hash[dst] = s_region_hash[src];
    s_region_index[dst] = s_region_index[src];
    s_atlas_revision[dst] = s_atlas_revision[src];
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
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->capacity > 0);

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, sprite_default, sprite_swap);
    if (res != NT_OK) {
        return res;
    }

    uint16_t cap = desc->capacity;
    s_atlas = (nt_resource_t *)calloc(cap, sizeof(nt_resource_t));
    s_region_hash = (uint64_t *)calloc(cap, sizeof(uint64_t));
    s_region_index = (uint16_t *)calloc(cap, sizeof(uint16_t));
    s_atlas_revision = (uint32_t *)calloc(cap, sizeof(uint32_t));
    s_origin = calloc(cap, sizeof(*s_origin));
    s_flags = (uint8_t *)calloc(cap, sizeof(uint8_t));

    if (!s_atlas || !s_region_hash || !s_region_index || !s_atlas_revision || !s_origin || !s_flags) {
        nt_sprite_comp_shutdown();
        return NT_ERR_INIT_FAILED;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "sprite",
        .has = nt_sprite_comp_has,
        .on_destroy = sprite_on_destroy,
    });

    s_last_publication_epoch = 0;
    s_sync_dirty = false;

    return NT_OK;
}

void nt_sprite_comp_shutdown(void) {
    free(s_atlas);
    free(s_region_hash);
    free(s_region_index);
    free(s_atlas_revision);
    free(s_origin);
    free(s_flags);
    s_atlas = NULL;
    s_region_hash = NULL;
    s_region_index = NULL;
    s_atlas_revision = NULL;
    s_origin = NULL;
    s_flags = NULL;
    s_last_publication_epoch = 0;
    s_sync_dirty = false;
    nt_comp_storage_shutdown(&s_storage);
}

void nt_sprite_comp_sync_resources(void) {
    uint16_t count = nt_comp_storage_count(&s_storage);
    uint32_t publication_epoch = nt_resource_publication_epoch();
    if (count == 0) {
        s_last_publication_epoch = publication_epoch;
        s_sync_dirty = false;
        return;
    }

    if (!s_sync_dirty && s_last_publication_epoch == publication_epoch) {
        return;
    }

    for (uint16_t i = 0; i < count; i++) {
        sprite_resolve_dense(i);
    }
    s_last_publication_epoch = publication_epoch;
    s_sync_dirty = false;
}

/* ---- Per-entity operations ---- */

bool nt_sprite_comp_add(nt_entity_t entity) { return nt_comp_storage_add(&s_storage, entity) != NT_INVALID_COMP_INDEX; }

bool nt_sprite_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_sprite_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }

bool nt_sprite_comp_is_resolved(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return (s_flags[idx] & NT_SPRITE_FLAG_RESOLVED) != 0;
}

/* ---- Region binding ---- */

void nt_sprite_comp_set_region(nt_entity_t entity, nt_resource_t atlas, uint16_t region_index) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_sprite_comp_set_region requires a READY atlas");
    if (!nt_resource_is_ready(atlas)) {
        sprite_clear_resolved(idx);
        return;
    }

    NT_ASSERT(region_index < nt_atlas_region_count(atlas) && "sprite region index out of range");
    if (region_index >= nt_atlas_region_count(atlas)) {
        sprite_clear_resolved(idx);
        return;
    }

    const nt_texture_region_t *region = nt_atlas_get_region(atlas, region_index);
    NT_ASSERT(region->name_hash != NT_ATLAS_TOMBSTONE_HASH && "sprite region points at a tombstone");
    if (region->name_hash == NT_ATLAS_TOMBSTONE_HASH) {
        sprite_clear_resolved(idx);
        return;
    }

    s_atlas[idx] = atlas;
    s_region_hash[idx] = region->name_hash;
    s_region_index[idx] = region_index;
    s_atlas_revision[idx] = nt_atlas_revision(atlas);
    s_flags[idx] |= (uint8_t)NT_SPRITE_FLAG_RESOLVED;
    sprite_set_authored_origin(idx, region);
}

void nt_sprite_comp_bind_by_hash(nt_entity_t entity, nt_resource_t atlas, uint64_t name_hash) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    NT_ASSERT(atlas.id != 0 && "nt_sprite_comp_bind_by_hash requires a valid atlas handle");
    if (atlas.id == 0) {
        s_atlas[idx] = NT_RESOURCE_INVALID;
        s_region_hash[idx] = 0;
        sprite_clear_resolved(idx);
        if ((s_flags[idx] & NT_SPRITE_FLAG_ORIGIN_OV) == 0) {
            s_origin[idx][0] = 0.0F;
            s_origin[idx][1] = 0.0F;
        }
        return;
    }

    s_atlas[idx] = atlas;
    s_region_hash[idx] = name_hash;
    sprite_clear_resolved(idx);
    s_sync_dirty = true;
    if ((s_flags[idx] & NT_SPRITE_FLAG_ORIGIN_OV) == 0) {
        s_origin[idx][0] = 0.0F;
        s_origin[idx][1] = 0.0F;
    }
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

    if ((s_flags[idx] & NT_SPRITE_FLAG_RESOLVED) != 0) {
        sprite_set_authored_origin(idx, nt_atlas_get_region(s_atlas[idx], s_region_index[idx]));
        return;
    }

    s_origin[idx][0] = 0.0F;
    s_origin[idx][1] = 0.0F;
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

const uint64_t *nt_sprite_comp_region_hash(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_region_hash[idx];
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
