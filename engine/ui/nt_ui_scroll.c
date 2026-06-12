#include "ui/nt_ui_scroll.h"

#include <math.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_state.h"

const nt_ui_widget_def_t NT_UI_SCROLL_DEF = {
    .name = "nt_scroll",
    .pill_color = 0xFF40C0B0U,
    ._reserved = 0U,
};

const nt_ui_widget_def_t NT_UI_SCROLLBAR_DEF = {
    .name = "nt_scrollbar",
    .pill_color = 0xFF40A0C0U,
    ._reserved = 0U,
};

/* Per-axis scrollbar id derives from the scroll id (scroll_id ^ salt) so each bar has
 * its OWN id for its OWN single nt_ui_anim value_t fade AND its own thumb-drag hit-test,
 * never colliding with the container's value_t (Pitfall 6; RESEARCH Open Q3). */
#define NT_UI_SCROLLBAR_VERT_SALT 0x5CB00000U
#define NT_UI_SCROLLBAR_HORIZ_SALT 0x5CB10000U
static inline uint32_t scrollbar_id(uint32_t scroll_id, int axis) { return nt_ui_derived_id(scroll_id, (axis == 1) ? NT_UI_SCROLLBAR_VERT_SALT : NT_UI_SCROLLBAR_HORIZ_SALT); }

/* Below this eased fade the AUTO_HIDE bar is not worth a draw call — skip its emit. */
#define NT_UI_SCROLLBAR_FADE_EPS 0.01F

/* All physics is in Clay's NEGATIVE-down sign convention: childOffset is negative
 * going down/right, content moves up/left. Clamp bounds are [-(content-container), 0].
 * Only the input edge (wheel) and scrollbar-thumb mapping flip sign. */

// #region tunables index
enum {
    NT_UI_SCROLL_T_FRICTION = 0,   /* per-60fps momentum decay base */
    NT_UI_SCROLL_T_WHEEL_EASE = 1, /* pos eases toward target at this rate */
    NT_UI_SCROLL_T_RUBBER_C = 2,   /* overscroll coefficient (0 = none) */
    NT_UI_SCROLL_T_BOUNCE = 3,     /* bounce-back spring ease */
    NT_UI_SCROLL_T_WHEEL_STEP = 4, /* px per wheel unit */
    NT_UI_SCROLL_T_COUNT = 5,
};
// #endregion

/* Below this |vel| (px/s) momentum is dead — zero it so pos settles exactly. */
#define NT_UI_SCROLL_VEL_EPS 0.5F
/* Below this |target-pos| scroll-to/wheel-ease has arrived. */
#define NT_UI_SCROLL_POS_EPS 0.25F
/* Release-fling velocity tracked over a TIME WINDOW (iOS/Android VelocityTracker), not per-frame:
 * mouse events arrive slower than render frames, so a fast drag alternates moving / zero-delta
 * frames. Hard-zeroing on a zero-delta frame made the fling a lottery on the release frame's
 * parity. Instead a moving frame blends toward the instantaneous sample, a zero-delta frame DECAYS
 * toward 0 — both with dt-based factors so the window is wall-time, not frame, defined.
 * TAU_GAIN small -> snappy build; TAU_DECAY ~20ms keeps a single empty frame's velocity (fling
 * survives event quantization) yet drains a real ~100ms+ hold below the fling threshold. */
#define NT_UI_SCROLL_VEL_TAU_GAIN 0.03F
#define NT_UI_SCROLL_VEL_TAU_DECAY 0.02F
#define NT_UI_SCROLL_VEL_MAX 8000.0F
/* A tap on a flinging container faster than this (px/s) stops the fling and is swallowed (iOS). */
#define NT_UI_SCROLL_FLING_STOP_PX 50.0F
/* Overscroll: per-60fps-frame velocity decay while the fling is rubber-compressing past the edge.
 * Aggressive enough that total overshoot depth is dt-invariant (vel bleeds out over the same wall
 * time at any step rate), then the bounce spring settles the remainder. */
#define NT_UI_SCROLL_OVERSCROLL_DECAY 0.55F

static inline float scroll_clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/* iOS UIScrollView rubber-band: compresses large overscroll asymptotically.
 * d = raw over-edge distance, dim = container dimension on that axis, c ~ 0.55. */
static float nt_ui_rubber_band(float d, float dim, float c) {
    if (dim <= 0.0F || c <= 0.0F) {
        return d;
    }
    const float ad = fabsf(d);
    const float compressed = (c * ad * dim) / ((c * ad) + dim);
    return (d < 0.0F) ? -compressed : compressed;
}

/* Per-axis clamp bound: childOffset min = -(content-container) when content
 * overflows, else 0 (no scroll). max is always 0 (Clay sign). */
static inline float scroll_min_bound(float content, float container) {
    const float over = content - container;
    return (over > 0.0F) ? -over : 0.0F;
}

// #region integrator
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scroll_integrate(nt_ui_scroll_state_t *s, const float wheel[2], float dt, const float content[2], const float container[2], const float tunables[NT_UI_SCROLL_T_COUNT]) {
    const float friction = tunables[NT_UI_SCROLL_T_FRICTION];
    const float wheel_ease = tunables[NT_UI_SCROLL_T_WHEEL_EASE];
    const float rubber_c = tunables[NT_UI_SCROLL_T_RUBBER_C];
    const float bounce = tunables[NT_UI_SCROLL_T_BOUNCE];
    const float wheel_step = tunables[NT_UI_SCROLL_T_WHEEL_STEP];
    const bool dragging = (s->flags & NT_UI_SCROLL_FLAG_DRAGGING) != 0U;
    const bool scroll_to = (s->flags & NT_UI_SCROLL_FLAG_SCROLL_TO) != 0U;
    bool activity = dragging || scroll_to;

    for (int a = 0; a < 2; ++a) {
        const float lo = scroll_min_bound(content[a], container[a]);
        const float hi = 0.0F;

        /* Wheel adds to target (input edge already feeds Clay-sign via begin; here wheel[] arrives
         * in Clay sign). pos eases toward target — no teleport. User input kills any in-flight
         * momentum so the easing branch picks up the new target THIS frame (no swallowed notch). */
        if (wheel[a] != 0.0F) {
            s->target[a] += wheel[a] * wheel_step;
            s->target[a] = scroll_clampf(s->target[a], lo, hi);
            s->vel[a] = 0.0F;
            activity = true;
        }

        if (dragging) {
            /* Active drag: pos is a PURE FUNCTION of the raw (1:1) finger position — clamped
             * in-bounds, rubber-banded past the edge, RECOMPUTED FROM RAW each frame (never
             * iterated on its own output). A finger held still leaves raw unchanged -> pos
             * frozen (no creep), in and out of bounds. target/vel synced for release. */
            if (s->raw[a] < lo || s->raw[a] > hi) {
                const float edge = (s->raw[a] < lo) ? lo : hi;
                const float over = s->raw[a] - edge; /* RAW over-edge distance from the anchor */
                s->pos[a] = (rubber_c > 0.0F) ? (edge + nt_ui_rubber_band(over, container[a], rubber_c)) : edge;
            } else {
                s->pos[a] = s->raw[a];
            }
            s->target[a] = s->pos[a];
            continue; /* drag owns this axis fully this frame; no momentum/ease/bounce */
        }

        /* Smooth-wheel + scroll-to: ease pos toward target until it ARRIVES — not
         * only on the wheel frame, or the offset stalls before reaching target. */
        const bool easing = scroll_to || (fabsf(s->target[a] - s->pos[a]) > NT_UI_SCROLL_POS_EPS && fabsf(s->vel[a]) <= NT_UI_SCROLL_VEL_EPS);
        if (easing) {
            /* wheel_ease 0 = instant (matches nt_ui_anim speed-0 convention), no teleport otherwise. */
            float k = (wheel_ease <= 0.0F) ? 1.0F : wheel_ease * dt;
            k = scroll_clampf(k, 0.0F, 1.0F);
            s->pos[a] += (s->target[a] - s->pos[a]) * k;
            s->vel[a] = 0.0F; /* eased motion owns pos; momentum stands down */
        } else {
            /* Momentum/fling: exponential decay, frame-rate independent. target
             * tracks pos so a later wheel/scroll-to starts from the rest point. */
            if (fabsf(s->vel[a]) > NT_UI_SCROLL_VEL_EPS) {
                s->pos[a] += s->vel[a] * dt;
                s->vel[a] *= powf(friction, dt * 60.0F);
                s->target[a] = scroll_clampf(s->pos[a], lo, hi);
                activity = true; /* a fling in motion keeps the AUTO_HIDE bar awake */
            } else {
                s->vel[a] = 0.0F;
            }
        }

        /* Released + out of bounds: critically-damped pull back to the clamped edge. */
        if (s->pos[a] < lo || s->pos[a] > hi) {
            const float edge = (s->pos[a] < lo) ? lo : hi;
            /* Fling overshoot: don't kill vel instantly (one frame of vel*dt is FPS-dependent —
             * half the depth at 120fps). Decay it aggressively per dt-normalized frame so the
             * rubber compresses to the SAME visual depth regardless of step rate, then hand off
             * to the bounce spring once vel has bled down. */
            if (fabsf(s->vel[a]) > NT_UI_SCROLL_VEL_EPS) {
                s->vel[a] *= powf(NT_UI_SCROLL_OVERSCROLL_DECAY, dt * 60.0F);
            } else {
                s->vel[a] = 0.0F; /* bounce absorbs the remainder */
            }
            /* A rest point can never be out of bounds: re-clamp target so a stale (e.g. content just
             * shrank) target can't drag pos back past the edge via the easing branch — pos animates to
             * the NEW bound instead of fighting it. Gate on solved dims: a frame-1 (container==0)
             * read would otherwise clamp a valid pending scroll-to target to 0 (1-frame-lag trap). */
            if (container[a] > 0.0F) {
                s->target[a] = scroll_clampf(s->target[a], lo, hi);
            }
            float kb = bounce * dt;
            kb = scroll_clampf(kb, 0.0F, 1.0F);
            s->pos[a] += (edge - s->pos[a]) * kb;
            if (fabsf(s->pos[a] - edge) < NT_UI_SCROLL_POS_EPS) {
                s->pos[a] = edge;
            }
        } else {
            /* In-bounds: keep target inside so the next wheel/scroll-to starts clamped. */
            s->target[a] = scroll_clampf(s->target[a], lo, hi);
            s->pos[a] = scroll_clampf(s->pos[a], lo, hi);
        }
        /* raw tracks the settled pos so the NEXT drag anchors from the rest point (not a stale overdrag). */
        s->raw[a] = s->pos[a];
    }

    /* scroll-to clears when both axes have arrived. */
    if (scroll_to) {
        const float dx = s->target[0] - s->pos[0];
        const float dy = s->target[1] - s->pos[1];
        if (fabsf(dx) < NT_UI_SCROLL_POS_EPS && fabsf(dy) < NT_UI_SCROLL_POS_EPS) {
            s->pos[0] = s->target[0];
            s->pos[1] = s->target[1];
            s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_SCROLL_TO;
        }
    }

    /* AUTO_HIDE activity timer: any scroll motion this frame resets the idle clock; otherwise it
     * accumulates dt (no wall clock in the hot path). The bar's fade reads this linger. */
    s->idle = activity ? 0.0F : (s->idle + dt);
}
// #endregion

static void style_to_tunables(const nt_ui_scroll_style_t *style, float tun[NT_UI_SCROLL_T_COUNT]) {
    tun[NT_UI_SCROLL_T_FRICTION] = style->friction;
    tun[NT_UI_SCROLL_T_WHEEL_EASE] = style->wheel_ease_speed;
    tun[NT_UI_SCROLL_T_RUBBER_C] = style->rubber_band_c;
    tun[NT_UI_SCROLL_T_BOUNCE] = style->bounce_speed;
    tun[NT_UI_SCROLL_T_WHEEL_STEP] = style->wheel_step_px;
}

nt_ui_scroll_style_t nt_ui_scroll_style_defaults(void) {
    /* Mobile-game feel: 0.92 decay reads snappier than Clay's 0.95; 18/40 ease+step
     * give a smooth single-notch wheel; 0.55 is the iOS rubber-band reference. */
    return (nt_ui_scroll_style_t){
        .scroll_x = false,
        .scroll_y = true,
        .friction = 0.92F,
        .wheel_ease_speed = 18.0F,
        .rubber_band_c = 0.55F,
        .bounce_speed = 12.0F,
        .wheel_step_px = 40.0F,
        .bar_visibility = NT_UI_SCROLLBAR_ALWAYS,
        .bar_placement = NT_UI_SCROLLBAR_OVERLAY,
        .bar_thickness = 12.0F,
        .bar_thumb_min_px = 24.0F,
        .bar_fade_speed = 6.0F,
        .bar_hide_delay = 1.2F,
        .track_ref = {0},
        .thumb_ref = {0},
        .track_tint = 0xFFFFFFFFU,
        .thumb_tint = 0xFFFFFFFFU,
    };
}

// #region last-fed clip offset (test readback)
#ifdef NT_TEST_ACCESS
static float s_last_child_offset[2] = {0.0F, 0.0F};
static uint32_t s_last_scroll_id = 0U;
static uint8_t s_last_bar_emitted_axes = 0U;       /* bit0 = x, bit1 = y */
static float s_last_bar_thumb_len[2] = {0.0F};     /* per-axis thumb length */
static float s_last_bar_thumb_off[2] = {0.0F};     /* per-axis thumb offset along the track */
static float s_last_bar_track_len[2] = {0.0F};     /* per-axis track length */
static float s_last_bar_opacity[2] = {0.0F, 0.0F}; /* per-axis eased fade opacity */
#endif
// #endregion

/* The container's prev-frame layout bbox, fetched ONCE per scroll_begin / pane_offset call and
 * threaded down to every hit-test (drag-check, wheel-gather, bar origin) so Clay_GetElementData
 * runs a single time per container per frame. found==false on frame 1 (bbox not solved yet) — the
 * 1-frame lag we accept; callers treat !found as "no hit". */
typedef struct {
    Clay_BoundingBox box;
    bool found;
} nt_scroll_bbox_t;

static inline nt_scroll_bbox_t scroll_fetch_bbox(uint32_t id) {
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    return (nt_scroll_bbox_t){.box = d.boundingBox, .found = d.found};
}

/* Is the framebuffer-px point (px,py) inside the (prev-frame) container bbox? !found -> false.
 * 2D ctx: capture press_pos (UI-space px) and bbox (Clay Y-down px) align. */
static inline bool point_in_bbox(const nt_scroll_bbox_t *bb, float px, float py) {
    if (!bb->found) {
        return false;
    }
    return px >= bb->box.x && px <= (bb->box.x + bb->box.width) && py >= bb->box.y && py <= (bb->box.y + bb->box.height);
}

/* The ONE wheel-gather path: accumulate enabled-axis wheel deltas from pointers hovering the
 * container, in Clay's negative-down sign (vertical flips wheel_dy). Used by both scroll_begin
 * and the internal pane helper so the loop + sign convention live in a single place. */
static void scroll_gather_wheel(const nt_pointer_t *pointers, uint32_t count, const nt_scroll_bbox_t *bb, bool scroll_x, bool scroll_y, float out_wheel[2]) {
    out_wheel[0] = 0.0F;
    out_wheel[1] = 0.0F;
    for (uint32_t i = 0; i < count; ++i) {
        const nt_pointer_t *p = &pointers[i];
        if ((p->wheel_dx != 0.0F || p->wheel_dy != 0.0F) && point_in_bbox(bb, p->x, p->y)) {
            if (scroll_x) {
                out_wheel[0] += p->wheel_dx;
            }
            if (scroll_y) {
                out_wheel[1] += -p->wheel_dy; /* input edge: flip to Clay's negative-down */
            }
        }
    }
}

/* Time-windowed release-fling velocity for one axis. A moving frame blends toward the instantaneous
 * sample (delta/dt) with a dt-based gain so a dt hitch can't explode it, then clamps the magnitude.
 * A zero-delta frame DECAYS toward 0 with a dt-based factor (NOT a hard-zero): one empty frame keeps
 * most of the velocity so a fling survives the mouse arriving slower than the render loop, while a
 * sustained hold drains below the fling threshold. dt<=0 has no fresh sample and no wall time to
 * decay over, so it hard-zeroes (a prior fling can't leak past this drag frame). */
static float scroll_sample_vel(float prev_vel, float delta, float dt) {
    if (dt <= 0.0F) {
        return 0.0F;
    }
    if (delta == 0.0F) {
        return prev_vel - (prev_vel * (1.0F - expf(-dt / NT_UI_SCROLL_VEL_TAU_DECAY)));
    }
    float v = prev_vel + (((delta / dt) - prev_vel) * (1.0F - expf(-dt / NT_UI_SCROLL_VEL_TAU_GAIN)));
    if (v > NT_UI_SCROLL_VEL_MAX) {
        v = NT_UI_SCROLL_VEL_MAX;
    } else if (v < -NT_UI_SCROLL_VEL_MAX) {
        v = -NT_UI_SCROLL_VEL_MAX;
    }
    return v;
}

/* Route one incremental drag delta (drag_x, drag_y in fb px) into the RAW scroll pos (1:1,
 * accumulates even past the edge — the integrator rubber-bands raw->display each frame). vel is
 * SAMPLED (time-windowed + clamp) for the release fling; see scroll_sample_vel for the decay contract. */
static void scroll_route_drag(nt_ui_scroll_state_t *s, const nt_ui_scroll_style_t *style, float drag_x, float drag_y, float dt) {
    if (style->scroll_x) {
        s->raw[0] += drag_x;
        s->vel[0] = scroll_sample_vel(s->vel[0], drag_x, dt);
    }
    if (style->scroll_y) {
        s->raw[1] += drag_y;
        s->vel[1] = scroll_sample_vel(s->vel[1], drag_y, dt);
    }
}

/* Shrink the cumulative-from-press delta by the tap threshold along the drag direction on the
 * LATCHING frame only. Without this the crossing frame dumps the whole accumulated dead-zone
 * (8+ px) into pos AND into vel/dt — a tiny finger move "flies away". Once latched the dead-zone
 * is spent, so subsequent frames pass the per-frame delta through untouched (true 1:1 tracking). */
static void scroll_consume_threshold(bool latched, float *ddx, float *ddy) {
    if (latched) {
        return; /* dead-zone already consumed; route the full per-frame delta 1:1 */
    }
    const float len = sqrtf(((*ddx) * (*ddx)) + ((*ddy) * (*ddy)));
    if (len <= NT_UI_SCROLL_STEAL_THRESHOLD_PX) {
        *ddx = 0.0F;
        *ddy = 0.0F;
        return;
    }
    const float keep = (len - NT_UI_SCROLL_STEAL_THRESHOLD_PX) / len; /* strip the threshold radius */
    *ddx *= keep;
    *ddy *= keep;
}

/* A capture this container must NOT steal: its OWN scrollbar (floats inside the bbox, a thumb
 * drag must reach the bar), or any nested scroll/scrollbar widget (the inner scroller owns its
 * own gesture). nt_ui_widget_lookup keys off the widget def registered at that id last frame. */
static bool scroll_capture_excluded(const nt_ui_context_t *ctx, uint32_t scroll_id, uint32_t cap_id) {
    if (cap_id == scrollbar_id(scroll_id, 0) || cap_id == scrollbar_id(scroll_id, 1)) {
        return true;
    }
    const nt_ui_widget_def_t *def = nt_ui_widget_lookup(ctx, cap_id);
    return def == &NT_UI_SCROLL_DEF || def == &NT_UI_SCROLLBAR_DEF;
}

/* Per-pointer gesture arbitration (D-59-04, Pitfall 2). Ownership is assigned at the POINTER-DOWN
 * EDGE to the container whose bbox holds the press point and is NEVER reassigned until release — the
 * iOS/Android contract. The single owner per pointer lives in ctx->captures[i].active_id; once this
 * container claims it, every OTHER container's STEAL/FREE-PRESS path sees a foreign-owned capture and
 * leaves it alone (the cross-container adoption bug — a held finger dragged over a neighbour can no
 * longer latch it). Three mutually-exclusive cases per pointer:
 *   - OWNED  : cap->active_id == our id — route the per-frame delta (cap->pos re-anchored each frame),
 *              hold DRAGGING/LATCHED. We own the capture so we also clear it on release here.
 *   - STEAL  : an INNER non-excluded widget owns it, press began in our bbox, threshold crossed — CLAIM
 *              the capture (active_id = our id, not 0: the edge-gated free-press below would otherwise
 *              re-adopt a still-held finger next frame). Claiming away cancels the inner widget (its
 *              resolve sees a foreign owner -> no click).
 *   - CANDIDATE: cap->active_id == 0 AND this is the press EDGE (btn.is_pressed) inside our bbox —
 *              record the free-press anchor. Threshold crossing CLAIMS the capture and routes. A
 *              merely-held (is_down, no edge) pointer never starts a gesture (kills the adoption bug).
 * Claimed captures must be re-marked seen every frame or begin's orphan-clear wipes them.
 * Nested/overlapping scrolls sharing a press point: the first to CLAIM wins, i.e. the OUTER container
 * (declared first, scroll_begin runs first) — the current contract (no nested scrolls shipped yet). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scroll_drag_check(nt_ui_context_t *ctx, uint32_t id, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s, const nt_scroll_bbox_t *bb, const float content[2],
                              const float container[2]) {
    /* Per-axis scrollability: a gesture only arms/steals on an axis that is BOTH enabled and can
     * actually move (content overflows). A container whose content fits takes no gestures at all,
     * so taps + drags pass through to inner widgets untouched (no no-op scroll stealing clicks). */
    const bool can_x = style->scroll_x && (scroll_min_bound(content[0], container[0]) < 0.0F);
    const bool can_y = style->scroll_y && (scroll_min_bound(content[1], container[1]) < 0.0F);
    if (!can_x && !can_y) {
        s->flags &= (uint8_t)~(NT_UI_SCROLL_FLAG_DRAGGING | NT_UI_SCROLL_FLAG_DRAG_LATCHED | NT_UI_SCROLL_FLAG_FREE_PRESS);
        return;
    }

    bool any_drag = false;
    bool any_free_press = false;
    const float dt = ctx->frame_dt;
    for (uint32_t i = 0; i < ctx->frame_pointer_count; ++i) {
        nt_ui_capture_t *cap = &ctx->captures[i];
        const nt_pointer_t *p = &ctx->frame_pointers[i];
        const nt_button_state_t btn = p->buttons[NT_BUTTON_LEFT];

        // #region OWNED: we already own this pointer's gesture
        if (cap->active_id == id) {
            if (!btn.is_down) {
                cap->active_id = 0U; /* release: we own the capture, so we end it (fling owns the rest) */
                continue;
            }
            ctx->capture_seen[i] = 1U; /* keep our claim alive past begin's orphan-clear */
            /* We own the capture but are not a stepped widget, so cap->pos isn't refreshed for us —
             * track the live pointer ourselves so the per-frame delta is current. */
            cap->pos[0] = p->x;
            cap->pos[1] = p->y;
            float ddx = cap->pos[0] - cap->press_pos[0];
            float ddy = cap->pos[1] - cap->press_pos[1];
            scroll_route_drag(s, style, ddx, ddy, dt);
            cap->press_pos[0] = cap->pos[0]; /* re-anchor: next frame's delta is per-frame, 1:1 */
            cap->press_pos[1] = cap->pos[1];
            any_drag = true;
            continue;
        }
        // #endregion

        // #region STEAL: an inner widget owns it; take over past the threshold
        const bool inner_capture = cap->active_id != 0U && !scroll_capture_excluded(ctx, id, cap->active_id);
        if (inner_capture) {
            if (!point_in_bbox(bb, cap->press_pos[0], cap->press_pos[1])) {
                continue; /* press began outside us — another container's gesture */
            }
            float ddx = cap->pos[0] - cap->press_pos[0];
            float ddy = cap->pos[1] - cap->press_pos[1];
            const float axis_move = (can_x ? fabsf(ddx) : 0.0F) + (can_y ? fabsf(ddy) : 0.0F);
            if (axis_move < NT_UI_SCROLL_STEAL_THRESHOLD_PX) {
                continue; /* tap (below threshold) leaves the inner capture -> inner clicks */
            }
            cap->active_id = id;       /* CLAIM (not 0): hold the gesture so it can't be re-adopted */
            ctx->capture_seen[i] = 1U; /* keep the claim past begin's orphan-clear */
            scroll_consume_threshold(false, &ddx, &ddy);
            scroll_route_drag(s, style, ddx, ddy, dt);
            cap->press_pos[0] = cap->pos[0]; /* re-anchor: next frame's delta is per-frame, 1:1 */
            cap->press_pos[1] = cap->pos[1];
            s->flags |= NT_UI_SCROLL_FLAG_DRAG_LATCHED;
            any_drag = true;
            continue;
        }
        if (cap->active_id != 0U) {
            continue; /* captured by THIS container's bar or an excluded nested scroller */
        }
        // #endregion

        // #region CANDIDATE: free pointer; only the press EDGE inside our bbox arms a gesture
        if (!btn.is_down) {
            continue; /* not held — nothing to track */
        }
        const bool armed = (s->flags & NT_UI_SCROLL_FLAG_FREE_PRESS) != 0U;
        if (!armed) {
            /* Edge-gate: only a fresh press (is_pressed) inside the bbox arms. A finger that entered
             * while already held (no edge) NEVER starts a gesture — the cross-container adoption fix. */
            if (!btn.is_pressed || !point_in_bbox(bb, p->x, p->y)) {
                continue;
            }
            /* Tap-to-stop (iOS): a press landing on a FLINGING container stops the fling instantly and
             * CLAIMS the capture so the tap can't click moving inner content. A press on a resting
             * container falls through to normal arming (inner widgets stay tappable). */
            const bool flinging = (can_x && fabsf(s->vel[0]) > NT_UI_SCROLL_FLING_STOP_PX) || (can_y && fabsf(s->vel[1]) > NT_UI_SCROLL_FLING_STOP_PX);
            if (flinging) {
                s->vel[0] = 0.0F;
                s->vel[1] = 0.0F;
                s->target[0] = s->pos[0];
                s->target[1] = s->pos[1];
                s->raw[0] = s->pos[0];
                s->raw[1] = s->pos[1];
                cap->active_id = id;       /* swallow the tap: own the gesture so inner content can't click */
                ctx->capture_seen[i] = 1U; /* keep the claim past begin's orphan-clear */
                cap->press_pos[0] = p->x;
                cap->press_pos[1] = p->y;
                cap->pos[0] = p->x;
                cap->pos[1] = p->y;
                any_drag = true; /* mark DRAGGING so the integrator freezes pos (drag owns the axis) */
                continue;
            }
            s->free_press_pos[0] = p->x;
            s->free_press_pos[1] = p->y;
            any_free_press = true;
            continue; /* armed this frame; delta accrues from next frame */
        }
        float ddx = p->x - s->free_press_pos[0];
        float ddy = p->y - s->free_press_pos[1];
        const float axis_move = (can_x ? fabsf(ddx) : 0.0F) + (can_y ? fabsf(ddy) : 0.0F);
        any_free_press = true;
        if (axis_move < NT_UI_SCROLL_STEAL_THRESHOLD_PX) {
            continue; /* still within the tap threshold — not yet a drag */
        }
        cap->active_id = id;       /* CLAIM: from here the OWNED case routes this pointer */
        ctx->capture_seen[i] = 1U; /* keep the claim past begin's orphan-clear */
        cap->press_pos[0] = p->x;
        cap->press_pos[1] = p->y;
        cap->pos[0] = p->x;
        cap->pos[1] = p->y;
        scroll_consume_threshold(false, &ddx, &ddy);
        scroll_route_drag(s, style, ddx, ddy, dt);
        s->flags |= NT_UI_SCROLL_FLAG_DRAG_LATCHED;
        any_drag = true;
        // #endregion
    }
    if (any_drag) {
        s->flags |= NT_UI_SCROLL_FLAG_DRAGGING;
    } else {
        s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_DRAGGING;
        s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_DRAG_LATCHED; /* gesture ended: next drag re-arms the dead-zone */
    }
    if (any_free_press) {
        s->flags |= NT_UI_SCROLL_FLAG_FREE_PRESS;
    } else {
        s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_FREE_PRESS;
    }
}

/* Scratch element_data carrying layer + whole-bar opacity (no transform). */
static nt_ui_element_data_t *scrollbar_make_data(uint8_t layer, float opacity) {
    nt_ui_element_data_t *d = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(d != NULL && "nt_ui_scrollbar: scratch alloc failed (element_data)");
    *d = (nt_ui_element_data_t){.user_data = NULL, .layer = layer, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_OPACITY, .transform = nt_ui_transform_defaults(), .opacity = opacity};
    return d;
}

/* True when this axis overflows (content longer than the container). */
static inline bool scrollbar_overflows(float content, float container) { return content > container + 0.5F; }

#ifdef NT_TEST_ACCESS
/* Visibility predicate mirroring scrollbar_emit_axis: AUTO emits only on overflow;
 * ALWAYS / AUTO_HIDE register whenever the container has solved dims. Test-only. */
static bool scrollbar_was_emitted(const nt_ui_scroll_style_t *style, float content, float container) {
    if (container <= 0.0F) {
        return false;
    }
    if (style->bar_visibility == NT_UI_SCROLLBAR_AUTO) {
        return scrollbar_overflows(content, container);
    }
    return true;
}
#endif

/* Thumb length on the track ∝ container/content, clamped to bar_thumb_min_px. */
static float scrollbar_thumb_len(float track_len, float content, float container, float min_px) {
    if (content <= 0.0F) {
        return track_len;
    }
    float len = track_len * (container / content);
    if (len < min_px) {
        len = min_px;
    }
    if (len > track_len) {
        len = track_len;
    }
    return len;
}

/* Thumb offset along the track from the current scroll pos (Clay NEGATIVE-down: pos is
 * <= 0, so -pos / over maps [0..1] top→bottom). */
static float scrollbar_thumb_pos(float pos, float content, float container, float track_len, float thumb_len) {
    const float over = content - container;
    if (over <= 0.0F) {
        return 0.0F;
    }
    float frac = -pos / over; /* 0 at top/left, 1 at bottom/right */
    frac = scroll_clampf(frac, 0.0F, 1.0F);
    return frac * (track_len - thumb_len);
}

/* Min effective cross-axis thickness for the bar's HIT area (touch target); the visual
 * bar_thickness stays slim. The pad is split across the two cross-axis sides. */
#define NT_UI_SCROLLBAR_HIT_MIN_PX 24.0F

/* Cross-axis hit pad so a thin visual bar still has a >= NT_UI_SCROLLBAR_HIT_MIN_PX touch target.
 * axis 1 (vertical bar): pad left/right; axis 0 (horizontal): pad top/bottom. */
static void scrollbar_hit_pad(int axis, float thickness, int16_t out[4]) {
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    const float grow = (NT_UI_SCROLLBAR_HIT_MIN_PX - thickness) * 0.5F;
    if (grow <= 0.0F) {
        return;
    }
    const int16_t g = (int16_t)ceilf(grow);
    if (axis == 1) {
        out[0] = g; /* left */
        out[1] = g; /* right */
    } else {
        out[2] = g; /* top */
        out[3] = g; /* bottom */
    }
}

/* Thumb-drag + track-click for one axis. Reads the bar's prev-frame bbox, steals nothing
 * from the container (different id). Drag delta along the axis maps back into s->pos
 * (Clay sign); a track click (off the thumb) smooth-jumps via nt_ui_scroll_to. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scrollbar_interact(nt_ui_context_t *ctx, uint32_t scroll_id, uint32_t bar_id, int axis, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s, float content, float container,
                               float bar_origin, float track_len, float thumb_off, float thumb_len, bool *out_active) {
    *out_active = false;
    int16_t pad[4];
    scrollbar_hit_pad(axis, style->bar_thickness, pad);
    const float over = content - container;
    if (over <= 0.0F || track_len - thumb_len <= 0.0F) {
        (void)nt_ui_step_interaction_padded(ctx, bar_id, pad); /* keep the id registered (consistent hit-test) */
        return;
    }
    const nt_ui_interaction_t in = nt_ui_step_interaction_padded(ctx, bar_id, pad);
    const float pointer_a = (axis == 1) ? in.pos[1] : in.pos[0];
    const float press_a = (axis == 1) ? in.press_pos[1] : in.press_pos[0];

    if (in.pressed_now) {
        const float thumb_lo = bar_origin + thumb_off;
        const bool on_thumb = press_a >= thumb_lo && press_a <= (thumb_lo + thumb_len);
        if (on_thumb) {
            /* Grab the thumb where it was clicked: remember the press offset INSIDE the thumb so the
             * held mapping keeps that point under the cursor (no jump-to-center on press). */
            s->thumb_grab = press_a - thumb_lo;
        } else {
            /* Track click (off the thumb): smooth-jump the clicked fraction, and seed the grab to the
             * thumb center so a continued drag from a track click maps from the thumb's middle. */
            s->thumb_grab = thumb_len * 0.5F;
            float frac = (press_a - bar_origin) / (track_len - thumb_len);
            frac = scroll_clampf(frac, 0.0F, 1.0F);
            const float target = -(frac * over);
            if (axis == 1) {
                nt_ui_scroll_to(ctx, scroll_id, s->pos[0], target);
            } else {
                nt_ui_scroll_to(ctx, scroll_id, target, s->pos[1]);
            }
        }
    }
    if (in.pressed) {
        /* Held: map the pointer so the GRABBED point of the thumb stays under the cursor (true 1:1
         * thumb drag). Off-thumb track clicks seeded the grab to thumb-center above. */
        float frac = (pointer_a - bar_origin - s->thumb_grab) / (track_len - thumb_len);
        frac = scroll_clampf(frac, 0.0F, 1.0F);
        s->pos[axis] = -(frac * over);
        s->target[axis] = s->pos[axis];
        s->vel[axis] = 0.0F;
        s->idle = 0.0F;                                    /* bar-drag is activity (keeps AUTO_HIDE awake) */
        s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_SCROLL_TO; /* drag overrides any pending jump */
        *out_active = true;
    }
    (void)style;
}

/* Emits one axis' 2-piece slice9 bar (track + thumb) as floating children of the open
 * scroll container, handles thumb-drag/track-click, and drives the AUTO_HIDE fade with
 * ONE nt_ui_anim call on the bar's derived id. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scrollbar_emit_axis(nt_ui_context_t *ctx, uint32_t scroll_id, int axis, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s, const float content[2], const float container[2],
                                const nt_scroll_bbox_t *cbb) {
    const float clen = container[axis];
    const float ccontent = content[axis];
    if (clen <= 0.0F) {
        return; /* dims not solved yet (frame 1) */
    }
    /* AUTO hides entirely when there is no overflow; ALWAYS / AUTO_HIDE always register. */
    if (style->bar_visibility == NT_UI_SCROLLBAR_AUTO && !scrollbar_overflows(ccontent, clen)) {
        return;
    }

    const uint32_t bar_id = scrollbar_id(scroll_id, axis);
    const float track_len = clen;
    const float thumb_len = scrollbar_thumb_len(track_len, ccontent, clen, style->bar_thumb_min_px);
    const float thumb_off = scrollbar_thumb_pos(s->pos[axis], ccontent, clen, track_len, thumb_len);

    /* Bar origin in the container's local space (floating attaches at the container edge). Reuse the
     * container bbox cached by scroll_begin (one fetch/frame); frame 1 (!found) leaves it at 0. */
    float bar_origin = 0.0F;
    if (cbb->found) {
        bar_origin = (axis == 1) ? cbb->box.y : cbb->box.x;
    }

    bool dragging = false;
    scrollbar_interact(ctx, scroll_id, bar_id, axis, style, s, ccontent, clen, bar_origin, track_len, thumb_off, thumb_len, &dragging);

    /* AUTO_HIDE: show on scroll ACTIVITY (idle within the linger window), on thumb-drag, or on
     * bar-hover; fade out after the idle linger elapses. The integrator resets s->idle on any scroll
     * motion (drag/wheel/fling/scroll-to), so the bar appears the moment content moves and lingers
     * ~bar_hide_delay before fading. ALWAYS / AUTO stay opaque. ONE anim. */
    float opacity = 1.0F;
    if (style->bar_visibility == NT_UI_SCROLLBAR_AUTO_HIDE) {
        const nt_ui_interaction_t hov = nt_ui_query_interaction(ctx, bar_id);
        const bool active = s->idle < style->bar_hide_delay;
        const float fade_target = (active || dragging || hov.hovered) ? 1.0F : 0.0F;
        nt_ui_anim_target_t tgt = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = fade_target};
        const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, bar_id, &tgt, 0.0F, style->bar_fade_speed);
        opacity = scroll_clampf(a->value_t, 0.0F, 1.0F);
    }
#ifdef NT_TEST_ACCESS
    /* Single write site — geometry + opacity are final by here (incl. the faded case). */
    s_last_bar_thumb_len[axis] = thumb_len;
    s_last_bar_thumb_off[axis] = thumb_off;
    s_last_bar_track_len[axis] = track_len;
    s_last_bar_opacity[axis] = opacity;
#endif
    if (opacity < NT_UI_SCROLLBAR_FADE_EPS) {
        return; /* fully faded — nothing to draw (the id stays registered for next-frame hover) */
    }

    const uint8_t layer = 0U;
    const float thickness = style->bar_thickness;
    /* Track spans the full axis on the trailing edge; thumb is offset along the axis. */
    Clay_ElementDeclaration track_decl;
    Clay_ElementDeclaration thumb_decl;
    if (axis == 1) { /* vertical bar on the right edge */
        track_decl = (Clay_ElementDeclaration){
            .layout = {.sizing = {CLAY_SIZING_FIXED(thickness), CLAY_SIZING_FIXED(track_len)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP}},
        };
        thumb_decl = (Clay_ElementDeclaration){
            .layout = {.sizing = {CLAY_SIZING_FIXED(thickness), CLAY_SIZING_FIXED(thumb_len)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .offset = {.x = 0.0F, .y = thumb_off}, .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP}},
        };
    } else { /* horizontal bar on the bottom edge */
        track_decl = (Clay_ElementDeclaration){
            .layout = {.sizing = {CLAY_SIZING_FIXED(track_len), CLAY_SIZING_FIXED(thickness)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM}},
        };
        thumb_decl = (Clay_ElementDeclaration){
            .layout = {.sizing = {CLAY_SIZING_FIXED(thumb_len), CLAY_SIZING_FIXED(thickness)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .offset = {.x = thumb_off, .y = 0.0F}, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM}},
        };
    }

    /* Track piece is the registered bar id (hit-test target for drag + track-click). */
    nt_atlas_region_ref_t track_ref = style->track_ref;
    nt_atlas_resolve_ref(&track_ref);
    track_decl.id = (Clay_ElementId){.id = bar_id};
    track_decl.userData = (void *)scrollbar_make_data(layer, opacity);
    if (track_ref.atlas.id != 0U && track_ref.region != NT_ATLAS_INVALID_REGION) {
        nt_ui_image_payload_t *tp = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(tp != NULL && "nt_ui_scrollbar: scratch alloc failed (track payload)");
        *tp = (nt_ui_image_payload_t){.atlas = track_ref.atlas, .region_index = track_ref.region, .slice9_scale = 1.0F};
        track_decl.image = (Clay_ImageElementConfig){.imageData = tp};
        track_decl.backgroundColor = nt_ui_unpack_tint(style->track_tint);
    }
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(track_decl);
    int16_t reg_pad[4];
    scrollbar_hit_pad(axis, thickness, reg_pad); /* same pad the interact step used (inspector overlay parity) */
    nt_ui_widget_register(ctx, bar_id, &NT_UI_SCROLLBAR_DEF, reg_pad);
    nt_ui_clay_priv_close_element();

    /* Thumb piece is a separate floating image (no own id; the track owns the hit-test). */
    nt_atlas_region_ref_t thumb_ref = style->thumb_ref;
    nt_atlas_resolve_ref(&thumb_ref);
    if (thumb_ref.atlas.id != 0U && thumb_ref.region != NT_ATLAS_INVALID_REGION) {
        nt_ui_image_style_t thumb_style = nt_ui_image_style_defaults();
        thumb_style.color_packed = style->thumb_tint;
        nt_ui_image(ctx, scrollbar_make_data(layer, opacity), &thumb_ref, &thumb_style, &thumb_decl);
    }
}

/* Emits enabled-axis scrollbars as floating children of the open scroll container. */
static void scrollbar_emit(nt_ui_context_t *ctx, uint32_t scroll_id, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s, const float content[2], const float container[2],
                           const nt_scroll_bbox_t *cbb) {
    if (style->scroll_x) {
        scrollbar_emit_axis(ctx, scroll_id, 0, style, s, content, container, cbb);
    }
    if (style->scroll_y) {
        scrollbar_emit_axis(ctx, scroll_id, 1, style, s, content, container, cbb);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_scroll_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const nt_ui_scroll_style_t *style, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_scroll_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_scroll_begin: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(id != 0U && "nt_ui_scroll_begin: id must be non-zero");
    NT_ASSERT(style != NULL && "nt_ui_scroll_begin: style must be non-NULL");
    NT_ASSERT(isfinite(style->friction) && style->friction > 0.0F && style->friction <= 1.0F && "nt_ui_scroll_begin: friction must be in (0,1]");
    NT_ASSERT(isfinite(style->wheel_ease_speed) && style->wheel_ease_speed >= 0.0F && "nt_ui_scroll_begin: wheel_ease_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->wheel_step_px) && "nt_ui_scroll_begin: wheel_step_px must be finite");
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_scroll_begin: decl->id must be 0 (scroll id passed separately)");
        NT_ASSERT(decl->clip.horizontal == false && decl->clip.vertical == false && "nt_ui_scroll_begin: decl->clip must be zero (style controls)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_scroll_begin: decl->userData must be NULL (data param controls)");
    }

    nt_ui_scroll_state_t *s = nt_ui_state(ctx, id, (uint32_t)sizeof *s, NT_UI_STATE_TAG('s', 'c', 'r', 'l'));
    NT_ASSERT(s != NULL && "nt_ui_scroll_begin: state pool returned NULL");

    /* Clamp bounds from Clay's post-layout measurement (found==false on frame 1 ->
     * dims 0 -> bounds default to 0, accepted 1-frame lag). */
    const Clay_ScrollContainerData scd = Clay_GetScrollContainerData((Clay_ElementId){.id = id});
    const float content[2] = {scd.contentDimensions.width, scd.contentDimensions.height};
    const float container[2] = {scd.scrollContainerDimensions.width, scd.scrollContainerDimensions.height};

    /* One bbox fetch per container per frame — shared by drag-check, wheel-gather, and bar origin. */
    const nt_scroll_bbox_t cbb = scroll_fetch_bbox(id);

    /* Drag arbitration (steal + free-press) updates DRAGGING + routes drag delta before integrate. */
    scroll_drag_check(ctx, id, style, s, &cbb, content, container);

    /* Wheel: only the container the pointer is over receives it (Clay sign = -wheel_dy). */
    float wheel[2];
    scroll_gather_wheel(ctx->frame_pointers, ctx->frame_pointer_count, &cbb, style->scroll_x, style->scroll_y, wheel);

    float tun[NT_UI_SCROLL_T_COUNT];
    style_to_tunables(style, tun);
    scroll_integrate(s, wheel, ctx->frame_dt, content, container, tun);

    Clay_ElementDeclaration final = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    final.id = (Clay_ElementId){.id = id};
    final.clip = (Clay_ClipElementConfig){
        .horizontal = style->scroll_x, .vertical = style->scroll_y, .childOffset = (Clay_Vector2){.x = s->pos[0], .y = s->pos[1]}, /* OURS, never Clay's */
    };
    final.userData = (void *)data;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(final);
    nt_ui_widget_register(ctx, id, &NT_UI_SCROLL_DEF, NULL);

    /* Scrollbars float over the container edges (escape the clip, no layout cost). They
     * read THIS frame's offset (s->pos) but the bbox/content dims at a 1-frame lag. */
    scrollbar_emit(ctx, id, style, s, content, container, &cbb);

#ifdef NT_TEST_ACCESS
    s_last_child_offset[0] = s->pos[0];
    s_last_child_offset[1] = s->pos[1];
    s_last_scroll_id = id;
    s_last_bar_emitted_axes = 0U;
    if (style->scroll_x && scrollbar_was_emitted(style, content[0], container[0])) {
        s_last_bar_emitted_axes |= 1U;
    }
    if (style->scroll_y && scrollbar_was_emitted(style, content[1], container[1])) {
        s_last_bar_emitted_axes |= 2U;
    }
#endif
}

void nt_ui_scroll_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_scroll_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_scroll_end: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    nt_ui_clay_priv_close_element();
}

void nt_ui_scroll_to(nt_ui_context_t *ctx, uint32_t id, float x, float y) {
    NT_ASSERT(ctx != NULL && "nt_ui_scroll_to: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_scroll_to: id must be non-zero");
    NT_ASSERT(isfinite(x) && isfinite(y) && "nt_ui_scroll_to: target must be finite");
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(ctx, id);
    if (s == NULL) {
        return; /* container not yet declared this run; caller scrolls after first frame */
    }
    s->target[0] = x;
    s->target[1] = y;
    s->flags |= NT_UI_SCROLL_FLAG_SCROLL_TO;
}

/* Lightweight scroll for engine-internal clip panes (inspector) that have no widget id /
 * style / scrollbar. Drives the same integrator off prev-frame Clay dims + raw wheel, returns
 * the childOffset to feed clip.childOffset. Instant wheel (no momentum/rubber) — a debug pane
 * shouldn't keep easing between frames. State rides the nt_ui_state pool under `tag`.
 * MUST be called as a statement BEFORE the pane's clip element is opened: the prev-frame
 * Clay_GetScrollContainerData entry is lost once enough sibling elements reuse the layout
 * slots, so reading it inline in the .childOffset initializer returns zero dims. */
Clay_Vector2 nt_ui_internal_scroll_pane_offset(nt_ui_context_t *ctx, uint32_t id, uint32_t tag, bool scroll_x, bool scroll_y) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_scroll_pane_offset: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_internal_scroll_pane_offset: id must be non-zero");
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state(ctx, id, (uint32_t)sizeof *s, tag);

    /* Prev-frame dims; found==false (frame 1, clip not solved) -> 0 -> bounds 0 (1-frame lag). */
    const Clay_ScrollContainerData scd = Clay_GetScrollContainerData((Clay_ElementId){.id = id});
    const float content[2] = {scd.contentDimensions.width, scd.contentDimensions.height};
    const float container[2] = {scd.scrollContainerDimensions.width, scd.scrollContainerDimensions.height};

    /* Wheel only when the pointer is over THIS pane (one bbox fetch), Clay negative-down sign. */
    const nt_scroll_bbox_t cbb = scroll_fetch_bbox(id);
    float wheel[2];
    scroll_gather_wheel(ctx->frame_pointers, ctx->frame_pointer_count, &cbb, scroll_x, scroll_y, wheel);

    /* Instant wheel, no overscroll: wheel_ease 0 -> pos snaps to target, rubber/bounce 0. Named
     * index so the tunable order is compiler-checked (no positional divergence trap). */
    float tun[NT_UI_SCROLL_T_COUNT] = {0};
    tun[NT_UI_SCROLL_T_FRICTION] = 0.92F;
    tun[NT_UI_SCROLL_T_WHEEL_EASE] = 0.0F;
    tun[NT_UI_SCROLL_T_RUBBER_C] = 0.0F;
    tun[NT_UI_SCROLL_T_BOUNCE] = 0.0F;
    tun[NT_UI_SCROLL_T_WHEEL_STEP] = 40.0F;
    scroll_integrate(s, wheel, ctx->frame_dt, content, container, tun);
    return (Clay_Vector2){.x = s->pos[0], .y = s->pos[1]};
}

#ifdef NT_TEST_ACCESS
void nt_ui_scroll_test_integrate(nt_ui_scroll_state_t *s, const float wheel[2], float dt, const float content[2], const float container[2], const float tunables[5]) {
    NT_ASSERT(s != NULL && wheel != NULL && content != NULL && container != NULL && tunables != NULL && "nt_ui_scroll_test_integrate: null arg");
    scroll_integrate(s, wheel, dt, content, container, tunables);
}
float nt_ui_scroll_test_rubber_band(float d, float dim) { return nt_ui_rubber_band(d, dim, 0.55F); }
void nt_ui_scroll_test_last_child_offset(float *out_x, float *out_y) {
    if (out_x != NULL) {
        *out_x = s_last_child_offset[0];
    }
    if (out_y != NULL) {
        *out_y = s_last_child_offset[1];
    }
}
uint32_t nt_ui_scroll_test_last_scroll_id(void) { return s_last_scroll_id; }
uint8_t nt_ui_scroll_test_last_bar_emitted_axes(void) { return s_last_bar_emitted_axes; }
void nt_ui_scroll_test_last_bar_geometry(int axis, float *thumb_len, float *thumb_off, float *track_len, float *opacity) {
    NT_ASSERT(axis == 0 || axis == 1);
    if (thumb_len != NULL) {
        *thumb_len = s_last_bar_thumb_len[axis];
    }
    if (thumb_off != NULL) {
        *thumb_off = s_last_bar_thumb_off[axis];
    }
    if (track_len != NULL) {
        *track_len = s_last_bar_track_len[axis];
    }
    if (opacity != NULL) {
        *opacity = s_last_bar_opacity[axis];
    }
}
uint32_t nt_ui_scroll_test_bar_id(uint32_t scroll_id, int axis) { return scrollbar_id(scroll_id, axis); }
#endif
