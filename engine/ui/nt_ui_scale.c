#include "ui/nt_ui_scale.h"

#include <math.h>

#include "core/nt_assert.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_scale_t nt_ui_compute_scale(const nt_ui_scale_desc_t *desc, float fb_w, float fb_h) {
    NT_ASSERT(desc != NULL && "nt_ui_compute_scale: desc must be non-NULL");
    NT_ASSERT(desc->ref_w > 0.0F && desc->ref_h > 0.0F && "nt_ui_compute_scale: desc ref dims must be positive");
    NT_ASSERT(fb_w >= 0.0F && fb_h >= 0.0F && "nt_ui_compute_scale: fb dims must be non-negative");

    nt_ui_scale_t out = {0};
    out.fb_w = fb_w;
    out.fb_h = fb_h;

    /* Zero framebuffer (minimized, orientation change) -- emit no-op scale. */
    if (fb_w == 0.0F || fb_h == 0.0F) {
        out.logical_w = desc->ref_w;
        out.logical_h = desc->ref_h;
        out.scale_x = 1.0F;
        out.scale_y = 1.0F;
        return out;
    }

    const float sx = fb_w / desc->ref_w;
    const float sy = fb_h / desc->ref_h;

    switch (desc->mode) {
    case NT_UI_SCALE_STRETCH:
        out.logical_w = desc->ref_w;
        out.logical_h = desc->ref_h;
        out.scale_x = sx;
        out.scale_y = sy;
        return out;
    case NT_UI_SCALE_LETTERBOX: {
        const float s = fminf(sx, sy);
        out.logical_w = desc->ref_w;
        out.logical_h = desc->ref_h;
        out.scale_x = s;
        out.scale_y = s;
        out.offset_x = (fb_w - desc->ref_w * s) * 0.5F;
        out.offset_y = (fb_h - desc->ref_h * s) * 0.5F;
        return out;
    }
    case NT_UI_SCALE_CROP: {
        const float s = fmaxf(sx, sy);
        out.logical_w = desc->ref_w;
        out.logical_h = desc->ref_h;
        out.scale_x = s;
        out.scale_y = s;
        out.offset_x = (fb_w - desc->ref_w * s) * 0.5F; /* negative when wider */
        out.offset_y = (fb_h - desc->ref_h * s) * 0.5F;
        return out;
    }
    case NT_UI_SCALE_EXPAND: {
        const float s = fminf(sx, sy);
        out.logical_w = fb_w / s; /* >= ref_w */
        out.logical_h = fb_h / s;
        out.scale_x = s;
        out.scale_y = s;
        return out;
    }
    }
    NT_ASSERT(false && "nt_ui_compute_scale: unknown mode");
    out.logical_w = desc->ref_w;
    out.logical_h = desc->ref_h;
    out.scale_x = 1.0F;
    out.scale_y = 1.0F;
    return out;
}

nt_pointer_t nt_ui_scale_apply_pointer(const nt_ui_scale_t *s, nt_pointer_t physical) {
    NT_ASSERT(s != NULL && "nt_ui_scale_apply_pointer: s must be non-NULL");
    NT_ASSERT(s->scale_x > 0.0F && s->scale_y > 0.0F && "nt_ui_scale_apply_pointer: scale must be positive");
    physical.x = (physical.x - s->offset_x) / s->scale_x;
    physical.y = (physical.y - s->offset_y) / s->scale_y;
    return physical;
}

/* Logical {0..logical_w, 0..logical_h} -> physical inside fb. LETTERBOX/CROP
 * offsets expand ortho range past logical so bars/crop are part of world. */
nt_ui_scale_ortho_t nt_ui_scale_ortho(const nt_ui_scale_t *s) {
    NT_ASSERT(s != NULL && "nt_ui_scale_ortho: s must be non-NULL");
    NT_ASSERT(s->scale_x > 0.0F && s->scale_y > 0.0F && "nt_ui_scale_ortho: scale must be positive");
    nt_ui_scale_ortho_t out;
    const float ox_logical = s->offset_x / s->scale_x;
    const float oy_logical = s->offset_y / s->scale_y;
    out.left = -ox_logical;
    out.right = s->logical_w + ox_logical;
    out.bottom = -oy_logical;
    out.top = s->logical_h + oy_logical;
    return out;
}

nt_ui_target_t nt_ui_scale_make_target(const nt_ui_scale_t *s) {
    NT_ASSERT(s != NULL && "nt_ui_scale_make_target: s must be non-NULL");
    nt_ui_target_t t;
    t.mode = NT_UI_TARGET_SCALED;
    t.viewport[0] = 0.0F;
    t.viewport[1] = 0.0F;
    t.viewport[2] = s->logical_w;
    t.viewport[3] = s->logical_h;
    t.fb_size[0] = s->fb_w;
    t.fb_size[1] = s->fb_h;
    t.fb_offset[0] = s->offset_x;
    t.fb_offset[1] = s->offset_y;
    return t;
}
