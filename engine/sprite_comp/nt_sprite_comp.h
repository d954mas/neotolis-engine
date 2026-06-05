#ifndef NT_SPRITE_COMP_H
#define NT_SPRITE_COMP_H

#include "atlas/nt_atlas.h"
#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "resource/nt_resource.h"

/* ---- Flag bits (packed in uint8_t flags) ---- */

#define NT_SPRITE_FLAG_FLIP_X (1U << 0)
#define NT_SPRITE_FLAG_FLIP_Y (1U << 1)
#define NT_SPRITE_FLAG_ORIGIN_OV (1U << 2)
#define NT_SPRITE_FLAG_RESOLVED (1U << 3)
#define NT_SPRITE_FLAG_SLICE9_OV (1U << 4)

/* ---- Descriptor ---- */

typedef struct {
    uint16_t capacity;
} nt_sprite_comp_desc_t;

/* ---- Defaults ---- */

static inline nt_sprite_comp_desc_t nt_sprite_comp_desc_defaults(void) {
    return (nt_sprite_comp_desc_t){
        .capacity = 256,
    };
}

/* ---- Lifecycle ---- */

nt_result_t nt_sprite_comp_init(const nt_sprite_comp_desc_t *desc);
void nt_sprite_comp_shutdown(void);

/* Explicit sync step. Game code should call this after nt_resource_step(). */
void nt_sprite_comp_sync_resources(void);

/* ---- Per-entity operations ---- */

bool nt_sprite_comp_add(nt_entity_t entity);
bool nt_sprite_comp_has(nt_entity_t entity);
void nt_sprite_comp_remove(nt_entity_t entity);
bool nt_sprite_comp_is_resolved(nt_entity_t entity);

/* ---- Region binding ---- */

/* Atlas must be READY; resolves immediately. */
void nt_sprite_comp_set_region(nt_entity_t entity, nt_resource_t atlas, uint16_t region_index);

/* Stores atlas + hash; resolves on next sync_resources(). */
void nt_sprite_comp_bind_by_hash(nt_entity_t entity, nt_resource_t atlas, uint64_t name_hash);

/* ---- Origin override ---- */

void nt_sprite_comp_set_origin(nt_entity_t entity, float origin_x, float origin_y);
void nt_sprite_comp_reset_origin(nt_entity_t entity);

/* ---- Slice9 override ---- */

/* Sets override (sets SLICE9_OV flag). Zeros are a valid override — render
 * as plain stretched quad without slice9 cells. Use reset_slice9 to clear. */
void nt_sprite_comp_set_slice9(nt_entity_t entity, uint16_t l, uint16_t r, uint16_t t, uint16_t b);
void nt_sprite_comp_reset_slice9(nt_entity_t entity);

const uint16_t *nt_sprite_comp_slice9_lrtb(nt_entity_t entity);
bool nt_sprite_comp_has_slice9_override(nt_entity_t entity);

/* Multiplies borders at render. Asserts isfinite(scale) && scale > 0. */
void nt_sprite_comp_set_slice9_scale(nt_entity_t entity, float scale);
float nt_sprite_comp_slice9_scale(nt_entity_t entity);

/* ---- Flip control ---- */

void nt_sprite_comp_set_flip(nt_entity_t entity, bool flip_x, bool flip_y);

/* ---- Read accessors ----
 *
 * Read-only — fields are coupled through resolve; mutate via the dedicated
 * setters above. */

const nt_resource_t *nt_sprite_comp_atlas(nt_entity_t entity);
const uint64_t *nt_sprite_comp_region_hash(nt_entity_t entity);
/* Undefined unless is_resolved() — 0 is a valid index, not a sentinel. */
const uint16_t *nt_sprite_comp_region_index(nt_entity_t entity);
/* ORIGIN_OV → set_origin value; else region's authored origin; (0,0) if not resolved. */
const float *nt_sprite_comp_origin(nt_entity_t entity);
const uint8_t *nt_sprite_comp_flags(nt_entity_t entity);

typedef struct {
    const nt_texture_region_t *region;
    const float (*cached_pos)[2];
    const nt_atlas_vertex_t *raw_vertices;
    const uint16_t *indices;
    nt_resource_t page_resource;
} nt_sprite_resolved_region_t;

/* Bulk SoA view — pointers stable for module lifetime; values shift on add/remove.
 * Reads of region_index/origin/resolved require flags[i] & RESOLVED. */

typedef struct {
    uint16_t count;
    const uint16_t *entity_indices; /* dense_idx -> entity_index, for joining with other comps */
    const uint16_t *sparse_indices; /* entity_index -> dense_idx; NT_INVALID_COMP_INDEX (0xFFFF) means no component */
    const nt_resource_t *atlas;
    const uint64_t *region_hash;
    const uint16_t *region_index;
    const nt_sprite_resolved_region_t *resolved;
    const float (*origin)[2];
    const uint16_t (*slice9_lrtb)[4];
    const float *slice9_scale;
    const uint8_t *flags;
} nt_sprite_comp_view_t;

nt_sprite_comp_view_t nt_sprite_comp_view(void);

#endif /* NT_SPRITE_COMP_H */
