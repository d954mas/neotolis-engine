#include "render/nt_render_util.h"

#include "drawable_comp/nt_drawable_comp.h"

/* ---- Visibility check ---- */

bool nt_render_is_visible(nt_entity_t entity) {
    if (!nt_entity_is_alive(entity)) {
        return false;
    }
    if (!nt_entity_is_enabled(entity)) {
        return false;
    }
    if (!nt_drawable_comp_has(entity)) {
        return false;
    }
    if (!*nt_drawable_comp_visible(entity)) {
        return false;
    }
    float *color = nt_drawable_comp_color(entity);
    if (color[3] <= 0.0F) {
        return false;
    }
    return true;
}
