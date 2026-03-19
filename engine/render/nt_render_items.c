#include "render/nt_render_items.h"

#include <stdlib.h>

#include "entity/nt_entity.h"
#include "transform_comp/nt_transform_comp.h"

/* ---- View depth calculation ---- */

float nt_calc_view_depth(uint32_t entity_id, const float view_pos[3], const float view_fwd[3]) {
    nt_entity_t entity = {.id = entity_id};
    const float *m = nt_transform_comp_world_matrix(entity);

    /* Extract world position from column 3 of the 4x4 matrix (column-major) */
    float dx = m[12] - view_pos[0];
    float dy = m[13] - view_pos[1];
    float dz = m[14] - view_pos[2];

    return dx * view_fwd[0] + dy * view_fwd[1] + dz * view_fwd[2];
}

/* ---- Sort by key ---- */

static int compare_render_items(const void *a, const void *b) {
    const nt_render_item_t *ia = (const nt_render_item_t *)a;
    const nt_render_item_t *ib = (const nt_render_item_t *)b;
    return (ia->sort_key > ib->sort_key) - (ia->sort_key < ib->sort_key);
}

void nt_sort_by_key(nt_render_item_t *items, uint32_t count) {
    if (count < 2) {
        return;
    }
    qsort(items, count, sizeof(nt_render_item_t), compare_render_items);
}
