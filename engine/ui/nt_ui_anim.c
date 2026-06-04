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
    NT_ASSERT(isfinite(t->scale) && t->scale > 0.0F && "nt_ui_anim: target.scale must be finite > 0");
    NT_ASSERT(isfinite(t->off_x) && isfinite(t->off_y) && "nt_ui_anim: target.offset must be finite");
    NT_ASSERT(isfinite(t->opacity) && t->opacity >= 0.0F && t->opacity <= 1.0F && "nt_ui_anim: target.opacity must be finite in [0,1]");
    NT_ASSERT(isfinite(t->tint_t) && t->tint_t >= 0.0F && t->tint_t <= 1.0F && "nt_ui_anim: target.tint_t must be finite in [0,1]");
    // #region slot-map + lerp
    /* On full probe chain evict tail not base — evicting base re-evicts next
     * frame and thrashes two ids competing for the same bucket. */
    const uint32_t base = id & (uint32_t)(NT_UI_ANIM_SLOTS - 1);
    nt_ui_anim_interaction_t *a = NULL;
    for (uint32_t k = 0; k < NT_UI_ANIM_PROBE_MAX; ++k) {
        nt_ui_anim_interaction_t *cand = &ctx->anim[(base + k) & (uint32_t)(NT_UI_ANIM_SLOTS - 1)];
        if (!cand->valid || cand->id == id) {
            a = cand;
            break;
        }
    }
    if (a == NULL) {
        /* Counter surfaces the lost-easing degradation so games can dial NT_UI_ANIM_SLOTS. */
        ctx->anim_collision_count++;
        a = &ctx->anim[(base + NT_UI_ANIM_PROBE_MAX - 1U) & (uint32_t)(NT_UI_ANIM_SLOTS - 1)];
    }
    const bool fresh = (!a->valid) || (a->id != id);
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
        /* k clamped to 1 caps overshoot (dt can be 0 on the first frame). */
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
