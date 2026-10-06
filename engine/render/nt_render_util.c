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
    return (nt_drawable_comp_color(entity) >> 24) != 0; /* alpha byte 0: alpha below 1/510 */
}
