#include "ui/nt_ui_anim.h"

#include <math.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_internal.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
const nt_ui_anim_interaction_t *nt_ui_anim(nt_ui_context_t *ctx, uint32_t id, const nt_ui_anim_target_t *t, float state_speed, float value_speed) {
    NT_ASSERT(ctx != NULL && "nt_ui_anim: ctx must be non-NULL");
    NT_ASSERT(t != NULL && "nt_ui_anim: target must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_anim: id 0 is the no-widget sentinel");
    NT_ASSERT(isfinite(state_speed) && state_speed >= 0.0F && "nt_ui_anim: state_speed must be finite >= 0");
    NT_ASSERT(isfinite(value_speed) && value_speed >= 0.0F && "nt_ui_anim: value_speed must be finite >= 0");
    NT_ASSERT(isfinite(t->scale_x) && t->scale_x > 0.0F && isfinite(t->scale_y) && t->scale_y > 0.0F && isfinite(t->scale_z) && t->scale_z > 0.0F && "nt_ui_anim: target.scale_xyz must be finite > 0");
    NT_ASSERT(isfinite(t->off_x) && isfinite(t->off_y) && isfinite(t->off_z) && "nt_ui_anim: target.offset_xyz must be finite");
    NT_ASSERT(isfinite(t->rot_x) && isfinite(t->rot_y) && isfinite(t->rot_z) && "nt_ui_anim: target.rotation_xyz must be finite");
    NT_ASSERT(isfinite(t->opacity) && t->opacity >= 0.0F && t->opacity <= 1.0F && "nt_ui_anim: target.opacity must be finite in [0,1]");
    NT_ASSERT(isfinite(t->tint_t) && t->tint_t >= 0.0F && t->tint_t <= 1.0F && "nt_ui_anim: target.tint_t must be finite in [0,1]");
    NT_ASSERT(isfinite(t->value_t) && t->value_t >= 0.0F && t->value_t <= 1.0F && "nt_ui_anim: target.value_t must be finite in [0,1]");
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
        a->scale_x = t->scale_x;
        a->scale_y = t->scale_y;
        a->scale_z = t->scale_z;
        a->off_x = t->off_x;
        a->off_y = t->off_y;
        a->off_z = t->off_z;
        a->rot_x = t->rot_x;
        a->rot_y = t->rot_y;
        a->rot_z = t->rot_z;
        a->opacity = t->opacity;
        a->tint_t = t->tint_t;
        a->value_t = t->value_t;
        return a;
    }
    // #region ease state fields (state_speed)
    if (state_speed == 0.0F) {
        a->scale_x = t->scale_x;
        a->scale_y = t->scale_y;
        a->scale_z = t->scale_z;
        a->off_x = t->off_x;
        a->off_y = t->off_y;
        a->off_z = t->off_z;
        a->rot_x = t->rot_x;
        a->rot_y = t->rot_y;
        a->rot_z = t->rot_z;
        a->opacity = t->opacity;
        a->tint_t = t->tint_t;
    } else {
        /* k clamped to 1 caps overshoot (dt can be 0 on the first frame). */
        float k = state_speed * ctx->frame_dt;
        if (k > 1.0F) {
            k = 1.0F;
        }
        a->scale_x += (t->scale_x - a->scale_x) * k;
        a->scale_y += (t->scale_y - a->scale_y) * k;
        a->scale_z += (t->scale_z - a->scale_z) * k;
        a->off_x += (t->off_x - a->off_x) * k;
        a->off_y += (t->off_y - a->off_y) * k;
        a->off_z += (t->off_z - a->off_z) * k;
        a->rot_x += (t->rot_x - a->rot_x) * k;
        a->rot_y += (t->rot_y - a->rot_y) * k;
        a->rot_z += (t->rot_z - a->rot_z) * k;
        a->opacity += (t->opacity - a->opacity) * k;
        a->tint_t += (t->tint_t - a->tint_t) * k;
    }
    // #endregion
    // #region ease value_t (value_speed, independent)
    if (value_speed == 0.0F) {
        a->value_t = t->value_t;
    } else {
        float vk = value_speed * ctx->frame_dt;
        if (vk > 1.0F) {
            vk = 1.0F;
        }
        a->value_t += (t->value_t - a->value_t) * vk;
    }
    // #endregion
    return a;
    // #endregion
}
