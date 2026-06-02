#include "ui/nt_ui_anim.h"

#include <math.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_internal.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
const nt_ui_anim_interaction_t *nt_ui_anim(nt_ui_context_t *ctx, uint32_t id, const nt_ui_anim_target_t *t, float transition_speed) {
    NT_ASSERT(ctx != NULL && "nt_ui_anim: ctx must be non-NULL");
    NT_ASSERT(t != NULL && "nt_ui_anim: target must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_anim: id 0 is the no-widget sentinel");
    NT_ASSERT(isfinite(transition_speed) && transition_speed >= 0.0F && "nt_ui_anim: transition_speed must be finite >= 0");
    NT_ASSERT(isfinite(t->scale) && isfinite(t->opacity) && "nt_ui_anim: target scale/opacity must be finite");
    // #region slot-map + lerp
    /* id is already a hash; no rehash. */
    const uint32_t slot = id & (uint32_t)(NT_UI_ANIM_SLOTS - 1);
    nt_ui_anim_interaction_t *a = &ctx->anim[slot];
    const bool fresh = (!a->valid) || (a->id != id); /* empty OR collision → re-seed */
    if (fresh) {
        /* First-touch / replace-on-collision: snap cur=target, no flash. */
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
        /* dt can be 0 on the first frame; k clamped to 1 caps overshoot. */
        float k = transition_speed * ctx->frame_dt;
        if (k > 1.0F) {
            k = 1.0F;
        }
        a->scale += (t->scale - a->scale) * k;
        a->off_x += (t->off_x - a->off_x) * k;
        a->off_y += (t->off_y - a->off_y) * k;
        a->opacity += (t->opacity - a->opacity) * k;
        a->tint_t += (t->tint_t - a->tint_t) * k;
    }
    return a;
    // #endregion
}
