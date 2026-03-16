#ifndef NT_RENDER_STATE_COMP_H
#define NT_RENDER_STATE_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"

typedef struct {
    uint16_t tag;   /* pass/group filter (0 = default/all passes) */
    bool visible;   /* render visibility (false = skip in render) */
    float color[4]; /* per-entity tint + alpha (sent as u_color uniform) */
} nt_render_state_comp_t;

typedef struct {
    uint16_t capacity;
} nt_render_state_comp_desc_t;

nt_result_t nt_render_state_comp_init(const nt_render_state_comp_desc_t *desc);
void nt_render_state_comp_shutdown(void);
nt_render_state_comp_t *nt_render_state_comp_add(nt_entity_t entity);
nt_render_state_comp_t *nt_render_state_comp_get(nt_entity_t entity);
bool nt_render_state_comp_has(nt_entity_t entity);
void nt_render_state_comp_remove(nt_entity_t entity);

#endif /* NT_RENDER_STATE_COMP_H */
