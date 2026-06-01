#include "ui/nt_ui_anim.h"

#include "core/nt_assert.h"
#include "ui/nt_ui_internal.h"

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

const nt_ui_anim_interaction_t *nt_ui_anim(nt_ui_context_t *ctx, uint32_t id, const nt_ui_anim_target_t *t, float transition_speed) {
    NT_ASSERT(ctx != NULL && "nt_ui_anim: ctx must be non-NULL");
    NT_ASSERT(t != NULL && "nt_ui_anim: target must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_anim: id 0 is the no-widget sentinel");
    // #region slot-map + lerp
    /* Direct-mapped slot; id is already a hash (D-56-05) so no rehash. */
    const uint32_t slot = id & (uint32_t)(NT_UI_ANIM_SLOTS - 1);
    nt_ui_anim_interaction_t *a = &ctx->anim[slot];
    const bool fresh = (!a->valid) || (a->id != id); /* empty OR collision -> re-seed */
    if (fresh) {
        /* Replace-on-collision + first-touch: snap cur=target, no flash, no scan. */
        a->id = id;
        a->valid = true;
        a->scale = t->scale;
        a->off_x = t->off_x;
        a->off_y = t->off_y;
        a->opacity = t->opacity;
        a->tint_t = t->tint_t;
        return a;
    }
    if (transition_speed == 0.0F) {
        a->scale = t->scale;
        a->off_x = t->off_x;
        a->off_y = t->off_y;
        a->opacity = t->opacity;
        a->tint_t = t->tint_t;
    } else {
        const float k = clampf(transition_speed * ctx->frame_dt, 0.0F, 1.0F);
        a->scale += (t->scale - a->scale) * k;
        a->off_x += (t->off_x - a->off_x) * k;
        a->off_y += (t->off_y - a->off_y) * k;
        a->opacity += (t->opacity - a->opacity) * k;
        a->tint_t += (t->tint_t - a->tint_t) * k;
    }
    return a;
    // #endregion
}
