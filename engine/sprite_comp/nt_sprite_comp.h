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

/* Strict fast path: atlas must already be READY. Resolves immediately and sets
 * RESOLVED. Does NOT raise s_sync_dirty — there is nothing to sync. If the
 * atlas later republishes, the next sync_resources() catches it via the cached
 * atlas_revision gate inside resolve_dense, so callers do not need to re-bind. */
void nt_sprite_comp_set_region(nt_entity_t entity, nt_resource_t atlas, uint16_t region_index);

/* Async-friendly path: stores atlas + region hash, then resolves explicitly in
 * nt_sprite_comp_sync_resources(). */
void nt_sprite_comp_bind_by_hash(nt_entity_t entity, nt_resource_t atlas, uint64_t name_hash);

/* ---- Origin override ---- */

void nt_sprite_comp_set_origin(nt_entity_t entity, float origin_x, float origin_y);
void nt_sprite_comp_reset_origin(nt_entity_t entity);

/* ---- Flip control ---- */

void nt_sprite_comp_set_flip(nt_entity_t entity, bool flip_x, bool flip_y);

/* ---- Read accessors (return const pointer into dense SoA array) ----
 *
 * Note on const: unlike drawable_comp / material_comp which return mutable
 * pointers, sprite_comp accessors are strictly read-only. Sprite fields are
 * coupled through resolve (atlas/region_hash drive the cached region_index,
 * authored origin, and RESOLVED flag) — direct mutation would silently break
 * those invariants. Use the dedicated set_region / bind_by_hash / set_origin /
 * set_flip entry points to mutate state. */

const nt_resource_t *nt_sprite_comp_atlas(nt_entity_t entity);
const uint64_t *nt_sprite_comp_region_hash(nt_entity_t entity);
/* Cached region index. Undefined when nt_sprite_comp_is_resolved() is false —
 * callers MUST gate reads on is_resolved(). The stored value is not a sentinel;
 * 0 is a valid region index for a resolved sprite. */
const uint16_t *nt_sprite_comp_region_index(nt_entity_t entity);
/* Effective origin (float[2]). Invariant: when ORIGIN_OV is set, returns the
 * value stored by set_origin(). Otherwise origin reflects the resolved region's
 * authored origin, or (0, 0) when not resolved. The renderer should only read
 * origin for resolved sprites; non-resolved (0, 0) is a safety default, not a
 * meaningful position. */
const float *nt_sprite_comp_origin(nt_entity_t entity);
const uint8_t *nt_sprite_comp_flags(nt_entity_t entity);

typedef struct {
    const nt_texture_region_t *region;
    const float (*cached_pos)[2];
    const float (*cached_uv)[2];
    const uint16_t *indices;
    nt_resource_t page_resource;
} nt_sprite_resolved_region_t;

/* ---- Bulk SoA view for systems iterating dense data (renderer, debug, etc.) ----
 *
 * Returns base pointers into the dense SoA arrays plus the live count. All
 * arrays are indexed by the same dense_idx in [0, count). Pointers are stable
 * for the lifetime of the module (allocated once at init), but values shift
 * under add/remove (swap-and-pop) — do not cache the view across mutations.
 *
 * Reading region_index / origin / resolved requires checking
 * flags[i] & RESOLVED, same contract as the per-entity accessors. `resolved`
 * is refreshed by nt_sprite_comp_sync_resources() when atlas publication
 * revisions change, so renderers do not repeat atlas metadata lookups in the
 * per-sprite hot path. */

typedef struct {
    uint16_t count;
    const uint16_t *entity_indices; /* dense_idx -> entity_index, for joining with other comps */
    const uint16_t *sparse_indices; /* entity_index -> dense_idx; NT_INVALID_COMP_INDEX (0xFFFF) means no component */
    const nt_resource_t *atlas;
    const uint64_t *region_hash;
    const uint16_t *region_index;
    const nt_sprite_resolved_region_t *resolved;
    const float (*origin)[2];
    const uint8_t *flags;
} nt_sprite_comp_view_t;

nt_sprite_comp_view_t nt_sprite_comp_view(void);

#endif /* NT_SPRITE_COMP_H */
