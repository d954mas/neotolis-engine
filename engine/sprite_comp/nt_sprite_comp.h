#ifndef NT_SPRITE_COMP_H
#define NT_SPRITE_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "resource/nt_resource.h"

/* ---- Flag bits (packed in uint8_t flags) ---- */

#define NT_SPRITE_FLAG_FLIP_X (1U << 0)
#define NT_SPRITE_FLAG_FLIP_Y (1U << 1)
#define NT_SPRITE_FLAG_ORIGIN_OV (1U << 2)

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

/* ---- Per-entity operations ---- */

bool nt_sprite_comp_add(nt_entity_t entity);
bool nt_sprite_comp_has(nt_entity_t entity);
void nt_sprite_comp_remove(nt_entity_t entity);

/* ---- Region binding ---- */

void nt_sprite_comp_set_region(nt_entity_t entity, nt_resource_t atlas, uint16_t region_index);
void nt_sprite_comp_set_region_by_hash(nt_entity_t entity, nt_resource_t atlas, uint64_t name_hash);

/* ---- Origin override ---- */

void nt_sprite_comp_set_origin(nt_entity_t entity, float origin_x, float origin_y);
void nt_sprite_comp_reset_origin(nt_entity_t entity);

/* ---- Flip control ---- */

void nt_sprite_comp_set_flip(nt_entity_t entity, bool flip_x, bool flip_y);

/* ---- Read accessors (return const pointer into dense SoA array) ---- */

const nt_resource_t *nt_sprite_comp_atlas(nt_entity_t entity);
const uint16_t *nt_sprite_comp_region_index(nt_entity_t entity);
const float *nt_sprite_comp_origin(nt_entity_t entity); /* float[2] */
const uint8_t *nt_sprite_comp_flags(nt_entity_t entity);

#endif /* NT_SPRITE_COMP_H */
