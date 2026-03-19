#ifndef NT_DRAWABLE_COMP_H
#define NT_DRAWABLE_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"

typedef struct {
    uint16_t capacity;
} nt_drawable_comp_desc_t;

nt_result_t nt_drawable_comp_init(const nt_drawable_comp_desc_t *desc);
void nt_drawable_comp_shutdown(void);

bool nt_drawable_comp_add(nt_entity_t entity);
bool nt_drawable_comp_has(nt_entity_t entity);
void nt_drawable_comp_remove(nt_entity_t entity);

uint16_t *nt_drawable_comp_tag(nt_entity_t entity);
bool *nt_drawable_comp_visible(nt_entity_t entity);
float *nt_drawable_comp_color(nt_entity_t entity); /* float[4] rgba */

#endif /* NT_DRAWABLE_COMP_H */
