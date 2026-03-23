#include "render/nt_render_items.h"

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

    return (dx * view_fwd[0]) + (dy * view_fwd[1]) + (dz * view_fwd[2]);
}
