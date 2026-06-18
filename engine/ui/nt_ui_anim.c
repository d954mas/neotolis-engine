#include "ui/nt_ui_anim.h"

#include <math.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_internal.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat fail-early assert chain, not control flow
static void anim_assert_target(const nt_ui_anim_target_t *t) {
    NT_ASSERT(t != NULL && "nt_ui_anim: target must be non-NULL");
    NT_ASSERT(isfinite(t->scale_x) && t->scale_x > 0.0F && isfinite(t->scale_y) && t->scale_y > 0.0F && isfinite(t->scale_z) && t->scale_z > 0.0F && "nt_ui_anim: target.scale_xyz must be finite > 0");
    NT_ASSERT(isfinite(t->off_x) && isfinite(t->off_y) && isfinite(t->off_z) && "nt_ui_anim: target.offset_xyz must be finite");
    NT_ASSERT(isfinite(t->rot_x) && isfinite(t->rot_y) && isfinite(t->rot_z) && "nt_ui_anim: target.rotation_xyz must be finite");
    NT_ASSERT(isfinite(t->opacity) && t->opacity >= 0.0F && t->opacity <= 1.0F && "nt_ui_anim: target.opacity must be finite in [0,1]");
    NT_ASSERT(isfinite(t->tint_t) && t->tint_t >= 0.0F && t->tint_t <= 1.0F && "nt_ui_anim: target.tint_t must be finite in [0,1]");
    NT_ASSERT(isfinite(t->value_t) && t->value_t >= 0.0F && t->value_t <= 1.0F && "nt_ui_anim: target.value_t must be finite in [0,1]");
}

/* Snap every field to target + (re)claim the cell for id. Shared by the no-slot fast path, the
 * first-touch (no-flash) path, and nt_ui_anim_seed. */
static void anim_snap_to(nt_ui_anim_interaction_t *a, uint32_t id, const nt_ui_anim_target_t *t, uint16_t tick) {
    a->id = id;
    a->valid = true;
    a->last_touch = tick;
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
}

/* Probe the open-addressed table for id; on probe-exhaust evict the STALEST slot in the window (oldest
 * last_touch), not blindly the tail — a tail-evict bleeds a slot that WAS touched this frame between
 * widgets. Stamps last_touch and returns the slot (matching id, empty, or evicted). */
static nt_ui_anim_interaction_t *anim_slot(nt_ui_context_t *ctx, uint32_t id) {
    const uint16_t tick = (uint16_t)ctx->current_generation;
    const uint32_t base = id & (uint32_t)(NT_UI_ANIM_SLOTS - 1);
    nt_ui_anim_interaction_t *a = NULL;
    nt_ui_anim_interaction_t *stalest = NULL;
    uint16_t stalest_age = 0U;
    for (uint32_t k = 0; k < NT_UI_ANIM_PROBE_MAX; ++k) {
        nt_ui_anim_interaction_t *cand = &ctx->anim[(base + k) & (uint32_t)(NT_UI_ANIM_SLOTS - 1)];
        if (!cand->valid || cand->id == id) {
            a = cand;
            break;
        }
        /* Wrap-safe age: ticks behind the current frame. The slot least recently touched wins. */
        const uint16_t age = (uint16_t)(tick - cand->last_touch);
        if (stalest == NULL || age > stalest_age) {
            stalest = cand;
            stalest_age = age;
        }
    }
    if (a == NULL) {
        /* Counter surfaces the lost-easing degradation so games can dial NT_UI_ANIM_SLOTS. */
        ctx->anim_collision_count++;
        a = stalest;
    }
    a->last_touch = tick;
    return a;
}

const nt_ui_anim_interaction_t *nt_ui_anim_seed(nt_ui_context_t *ctx, uint32_t id, const nt_ui_anim_target_t *t) {
    NT_ASSERT(ctx != NULL && "nt_ui_anim_seed: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_anim_seed: id 0 is the no-widget sentinel");
    anim_assert_target(t);
    nt_ui_anim_interaction_t *a = anim_slot(ctx, id);
    anim_snap_to(a, id, t, (uint16_t)ctx->current_generation);
    return a;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
const nt_ui_anim_interaction_t *nt_ui_anim(nt_ui_context_t *ctx, uint32_t id, const nt_ui_anim_target_t *t, float state_speed, float value_speed) {
    NT_ASSERT(ctx != NULL && "nt_ui_anim: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_anim: id 0 is the no-widget sentinel");
    NT_ASSERT(isfinite(state_speed) && state_speed >= 0.0F && "nt_ui_anim: state_speed must be finite >= 0");
    NT_ASSERT(isfinite(value_speed) && value_speed >= 0.0F && "nt_ui_anim: value_speed must be finite >= 0");
    anim_assert_target(t);

    const uint16_t tick = (uint16_t)ctx->current_generation;

    /* No-slot fast path: both speeds 0 => pure snap, nothing to retain across frames. Fill a per-ctx
     * scratch and return it (valid only until the next nt_ui_anim call) so the many non-animating widgets
     * never consume an anim-pool slot — the pool stays free for the few that actually tween. */
    if (state_speed == 0.0F && value_speed == 0.0F) {
        anim_snap_to(&ctx->anim_snap, id, t, tick);
        return &ctx->anim_snap;
    }

    // #region slot-map + lerp
    nt_ui_anim_interaction_t *a = anim_slot(ctx, id);
    if (!a->valid || a->id != id) {
        /* First-touch / replace-on-collision: snap cur=target, no flash. */
        anim_snap_to(a, id, t, tick);
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
