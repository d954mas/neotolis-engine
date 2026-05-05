#ifndef NT_DRAWABLE_COMP_H
#define NT_DRAWABLE_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "hash/nt_hash.h"

typedef struct {
    uint16_t capacity;
} nt_drawable_comp_desc_t;

/* ---- Defaults ---- */

static inline nt_drawable_comp_desc_t nt_drawable_comp_desc_defaults(void) {
    return (nt_drawable_comp_desc_t){
        .capacity = 256,
    };
}

nt_result_t nt_drawable_comp_init(const nt_drawable_comp_desc_t *desc);
void nt_drawable_comp_shutdown(void);

bool nt_drawable_comp_add(nt_entity_t entity);
bool nt_drawable_comp_has(nt_entity_t entity);
void nt_drawable_comp_remove(nt_entity_t entity);

nt_hash32_t *nt_drawable_comp_tag(nt_entity_t entity);
bool *nt_drawable_comp_visible(nt_entity_t entity);
float *nt_drawable_comp_color(nt_entity_t entity); /* float[4] rgba */

/* Update both the float color and the pre-packed RGBA8 mirror. Prefer this
 * over writing through the float[4] pointer when the renderer needs the
 * packed value — it avoids a per-frame float→u8 conversion in the hot
 * loop. */
void nt_drawable_comp_set_color(nt_entity_t entity, float r, float g, float b, float a);

/* Recompute the packed RGBA8 from the current float color. Call this after
 * mutating the color through the writable pointer returned by
 * nt_drawable_comp_color() — otherwise the renderer will see stale packed
 * bytes. set_color does both writes for you; this exists for code paths
 * that already wrote the float and just want to sync. */
void nt_drawable_comp_repack_color(nt_entity_t entity);

/* ---- Bulk SoA view (read-only) ----
 *
 * Lets renderers join with sprite/transform data without per-entity accessor
 * overhead. Pointers stable for module lifetime; values shift on add/remove. */
typedef struct {
    uint16_t count;
    const uint16_t *sparse_indices; /* entity_index -> dense_idx; NT_INVALID_COMP_INDEX if absent */
    const float (*color)[4];        /* dense_idx -> rgba float4 */
    const uint32_t *colors_packed;  /* dense_idx -> RGBA8 packed (little-endian) */
    const bool *visible;
} nt_drawable_comp_view_t;

nt_drawable_comp_view_t nt_drawable_comp_view(void);

#endif /* NT_DRAWABLE_COMP_H */
