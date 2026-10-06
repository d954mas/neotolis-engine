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
/* RGBA8 0xAABBGGRR (color/nt_color.h: nt_color_pack, nt_color_with_alpha); default white. */
uint32_t nt_drawable_comp_color(nt_entity_t entity);
void nt_drawable_comp_set_color(nt_entity_t entity, uint32_t color);
void nt_drawable_comp_set_visible(nt_entity_t entity, bool visible);

/* Bulk SoA view — pointers stable for module lifetime, values shift on add/remove. */
typedef struct {
    uint16_t count;
    const uint16_t *sparse_indices; /* entity_index -> dense_idx; NT_INVALID_COMP_INDEX if absent */
    const uint32_t *colors_packed;  /* dense_idx -> RGBA8 0xAABBGGRR */
    const bool *visible;
} nt_drawable_comp_view_t;

nt_drawable_comp_view_t nt_drawable_comp_view(void);

#endif /* NT_DRAWABLE_COMP_H */
