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

/* Per-axis scrollbar id derives from the scroll id so each bar owns its own fade value_t and
 * thumb-drag hit-test, never colliding with the container's value_t. */
static inline uint32_t scrollbar_id(uint32_t scroll_id, int axis) { return nt_ui_derived_id(scroll_id, (axis == 1) ? 0x5CB00000U : 0x5CB10000U); }

const nt_ui_widget_def_t NT_UI_SCROLLBAR_DEF = {
    .name = "nt_scrollbar",
    .pill_color = 0xFF40A0C0U,
    ._reserved = 0U,
};

/* Below this eased fade the AUTO_HIDE bar is not worth a draw call — skip its emit. */
#define NT_UI_SCROLLBAR_FADE_EPS 0.01F

/* A freshly-created scroll cell zero-inits idle=0, which on frame 1 reads as "just scrolled" and
 * pops the AUTO_HIDE bar fully visible before it fades (startup flash). Seed idle large so a new
 * cell is born past ANY bar_hide_delay (per-style) → bar starts hidden; real activity resets to 0. */
#define NT_UI_SCROLL_BORN_IDLE 1.0e9F

/* All physics is in Clay's NEGATIVE-down sign convention: childOffset is negative
 * going down/right, content moves up/left. Clamp bounds are [-(content-container), 0].
 * Only the input edge (wheel) and scrollbar-thumb mapping flip sign. */

/* Below this |vel| (px/s) momentum is dead — zero it so pos settles exactly. */
#define NT_UI_SCROLL_VEL_EPS 0.5F
/* Below this |target-pos| scroll-to/wheel-ease has arrived. */
#define NT_UI_SCROLL_POS_EPS 0.25F
/* Release-fling velocity tracked over a TIME WINDOW (iOS/Android VelocityTracker), not per-frame:
 * pointer events arrive slower than render frames, so a fast drag alternates moving / zero-delta
 * frames and per-frame velocity would depend on release-frame parity. A moving frame blends toward
 * the instantaneous sample, a zero-delta frame decays toward 0, both dt-based so the window is
 * wall-time. TAU_DECAY ~20ms survives one empty frame's quantization yet drains a real hold. */
#define NT_UI_SCROLL_VEL_TAU_GAIN 0.03F
#define NT_UI_SCROLL_VEL_TAU_DECAY 0.02F
#define NT_UI_SCROLL_VEL_MAX 8000.0F
/* Overscroll: per-60fps-frame velocity decay while the fling compresses past the edge.
 * Tuned so total overshoot depth is dt-invariant, then the bounce spring settles the rest. */
#define NT_UI_SCROLL_OVERSCROLL_DECAY 0.55F

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
static void scroll_integrate(nt_ui_scroll_state_t *s, const float wheel[2], float dt, const float content[2], const float container[2], const nt_ui_scroll_style_t *style, bool dragging) {
    const float friction = style->friction;
    const float wheel_ease = style->wheel_ease_speed;
    const float rubber_c = style->rubber_band_c;
    const float bounce = style->bounce_speed;
    const float wheel_step = style->wheel_step_px;
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
            s->target[a] = nt_ui_clampf(s->target[a], lo, hi);
            s->vel[a] = 0.0F;
            activity = true;
        }

        if (dragging) {
            /* Active drag: pos is a pure function of raw (1:1 finger pos), recomputed from raw
             * each frame so a held-still finger never creeps. Clamped in-bounds, rubber-banded
             * past the edge; target tracks pos for the release fling. */
            if (s->raw[a] < lo || s->raw[a] > hi) {
                const float edge = (s->raw[a] < lo) ? lo : hi;
                const float over = s->raw[a] - edge; /* RAW over-edge distance from the anchor */
                if (rubber_c > 0.0F) {
                    s->pos[a] = edge + nt_ui_rubber_band(over, container[a], rubber_c);
                } else if (container[a] > 0.0F) {
                    s->pos[a] = edge;
                    s->raw[a] = edge; /* hard edge: a reversing finger moves the content at once */
                }
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
            k = nt_ui_clampf(k, 0.0F, 1.0F);
            s->pos[a] += (s->target[a] - s->pos[a]) * k;
            s->vel[a] = 0.0F; /* eased motion owns pos; momentum stands down */
        } else {
            /* Momentum/fling: exponential decay, frame-rate independent. target
             * tracks pos so a later wheel/scroll-to starts from the rest point. */
            if (fabsf(s->vel[a]) > NT_UI_SCROLL_VEL_EPS) {
                s->pos[a] += s->vel[a] * dt;
                s->vel[a] *= powf(friction, dt * 60.0F);
                s->target[a] = nt_ui_clampf(s->pos[a], lo, hi);
                activity = true; /* a fling in motion keeps the AUTO_HIDE bar awake */
            } else {
                s->vel[a] = 0.0F;
            }
        }

        /* Released + out of bounds: critically-damped pull back to the clamped edge. */
        if (s->pos[a] < lo || s->pos[a] > hi) {
            const float edge = (s->pos[a] < lo) ? lo : hi;
            /* Fling overshoot: decay vel per dt-normalized frame (not instant kill, which is
             * FPS-dependent) so the rubber compresses to the same depth at any step rate, then
             * hand off to the bounce spring once vel has bled down. */
            if (rubber_c == 0.0F && container[a] > 0.0F) {
                s->pos[a] = edge; /* hard edge for flings too; unsolved dims (0) keep pos */
                s->vel[a] = 0.0F;
            } else if (fabsf(s->vel[a]) > NT_UI_SCROLL_VEL_EPS) {
                s->vel[a] *= powf(NT_UI_SCROLL_OVERSCROLL_DECAY, dt * 60.0F);
            } else {
                s->vel[a] = 0.0F; /* bounce absorbs the remainder */
            }
            /* Re-clamp target so a stale target (e.g. content shrank) can't drag pos back past the
             * edge via the easing branch. Gated on solved dims: a frame-1 (container==0) read would
             * clamp a valid pending scroll-to target to 0. */
            if (container[a] > 0.0F) {
                s->target[a] = nt_ui_clampf(s->target[a], lo, hi);
            }
            float kb = bounce * dt;
            kb = nt_ui_clampf(kb, 0.0F, 1.0F);
            s->pos[a] += (edge - s->pos[a]) * kb;
            if (fabsf(s->pos[a] - edge) < NT_UI_SCROLL_POS_EPS) {
                s->pos[a] = edge;
            }
        } else {
            /* In-bounds: keep target inside so the next wheel/scroll-to starts clamped. */
            s->target[a] = nt_ui_clampf(s->target[a], lo, hi);
            s->pos[a] = nt_ui_clampf(s->pos[a], lo, hi);
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
static uint8_t s_last_bar_layer[2] = {0U, 0U};     /* per-axis draw layer of the emitted bar */
#endif
// #endregion

/* Time-windowed release-fling velocity for one axis. A moving frame blends toward the instantaneous
 * sample with a dt-based gain (clamped); a zero-delta frame decays toward 0 with a dt-based factor so
 * one empty frame keeps most velocity (fling survives event quantization) yet a held finger drains.
 * dt<=0 hard-zeroes: no fresh sample and no wall time to decay over. */
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

// #region gesture arbiter
/* A tap on a flinging scroll faster than this (dp/s) stops the fling and is swallowed (iOS). */
#define NT_UI_SCROLL_FLING_STOP_DP 50.0F

/* One decision per pointer and frame: a press starts a gesture record on the declaration chain of
 * the front-most record under it; once the pointer crosses the drag slop on an axis, the strongest
 * crossing among the pressed widget's own drag axes and the chain's scrollable axes takes it. */

void nt_ui_internal_scroll_push(nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx->scroll_top < (uint16_t)NT_UI_SCROLL_STACK_CAP && "nt_ui_scroll: nesting exceeds NT_UI_SCROLL_STACK_CAP");
    ctx->scroll_stack[ctx->scroll_top++] = id;
}

void nt_ui_internal_scroll_pop(nt_ui_context_t *ctx, bool barrier) {
    NT_ASSERT(ctx->scroll_top > 0U && "nt_ui_scroll: unbalanced scroll/popup end");
    --ctx->scroll_top;
    NT_ASSERT((ctx->scroll_stack[ctx->scroll_top] == 0U) == barrier && "nt_ui_scroll: scroll and popup begin/end interleaved");
}

/* A scroll on a chain and the axes it can move on in last frame's layout. Clay keeps a scroll's data
 * only while it is declared, so a vanished scroll is no link. */
typedef struct {
    nt_ui_scroll_state_t *s;
    uint8_t axes; /* NT_UI_DRAG_X | NT_UI_DRAG_Y */
} gesture_link_t;

static gesture_link_t gesture_link(nt_ui_context_t *ctx, uint32_t id) {
    gesture_link_t link = {NULL, 0U};
    if (id == 0U || !nt_ui_state_has_tag(ctx, id, NT_UI_SCROLL_TAG)) {
        return link;
    }
    const Clay_ScrollContainerData scd = Clay_GetScrollContainerData((Clay_ElementId){.id = id});
    if (scd.found) {
        link.s = (nt_ui_scroll_state_t *)nt_ui_state_find(ctx, id);
        link.axes = nt_ui_internal_scroll_axes(&scd);
    }
    return link;
}

/* Parents come from one declaration tree per frame (Clay rejects a duplicate id), so a chain ends. */
static gesture_link_t gesture_parent(nt_ui_context_t *ctx, gesture_link_t link, uint32_t *id) {
    *id = link.s->parent;
    return gesture_link(ctx, *id);
}

#if NT_UI_DEBUG_TOOLS
static bool gesture_is_pane(nt_ui_context_t *ctx, uint32_t id) {
    const gesture_link_t link = gesture_link(ctx, id);
    return link.s != NULL && (link.s->flags & NT_UI_SCROLL_FLAG_PANE) != 0U;
}
#endif

/* How a hit on a registry record counts: a scroll surface only while it can move (a fitting list is
 * see-through); an inspector pane wins outright over any game record. */
nt_ui_hit_kind_t nt_ui_internal_scroll_surface_kind(nt_ui_context_t *ctx, uint32_t id) {
    const gesture_link_t link = gesture_link(ctx, id);
    if (link.axes == 0U) {
        return NT_UI_HIT_NONE;
    }
    return ((link.s->flags & NT_UI_SCROLL_FLAG_PANE) != 0U) ? NT_UI_HIT_PANE : NT_UI_HIT;
}

/* How strongly displacement d crossed the slop on any axis of `axes`: the largest |d| / slop, or 0. */
static float gesture_crossing(const float d[2], const float slop[2], uint8_t axes) {
    float best = 0.0F;
    for (int a = 0; a < 2; ++a) {
        const float r = fabsf(d[a]) / slop[a];
        if ((axes & (1U << a)) != 0U && r >= 1.0F && r > best) {
            best = r;
        }
    }
    return best;
}

static bool gesture_owned(const nt_ui_context_t *ctx, uint32_t id) {
    for (uint32_t j = 0; j < ctx->frame_pointer_count; ++j) {
        if (ctx->captures[j].active_id == id) {
            return true;
        }
    }
    return false;
}

/* The newest pointer takes a scroll (Android): older owners of its content or bars let go, also of
 * any gesture they still had pending. */
static void scroll_take(nt_ui_context_t *ctx, uint32_t id, uint32_t self) {
    const uint32_t bar_x = scrollbar_id(id, 0);
    const uint32_t bar_y = scrollbar_id(id, 1);
    for (uint32_t j = 0; j < ctx->frame_pointer_count; ++j) {
        const uint32_t held = ctx->captures[j].active_id;
        if (j != self && (held == id || held == bar_x || held == bar_y)) {
            ctx->captures[j].active_id = 0U;
            ctx->captures[j].scroll_scope = 0U;
        }
    }
}

/* Press edge. A press on a bar that can move takes its scroll from older pointers and decides at
 * once (a thumb has no slop); a flinging, unowned scroll on the chain stops and swallows the tap
 * (iOS); a chain that cannot scroll decides at once, so widgets in fitting panels are never pending. */
static void gesture_press(nt_ui_context_t *ctx, uint32_t i) {
    nt_ui_capture_t *cap = &ctx->captures[i];
    const nt_pointer_t *p = &ctx->frame_pointers[i];
    const uint32_t scope = ctx->pointer_scope[i];
    cap->scroll_scope = 0U;
    cap->press_pos[0] = p->x;
    cap->press_pos[1] = p->y;
    cap->pos[0] = p->x;
    cap->pos[1] = p->y;
    const uint32_t hot = ctx->pointer_hot[i].id;
    const uint8_t scope_axes = gesture_link(ctx, scope).axes;
    if ((hot == scrollbar_id(scope, 0) && (scope_axes & NT_UI_DRAG_X) != 0U) || (hot == scrollbar_id(scope, 1) && (scope_axes & NT_UI_DRAG_Y) != 0U)) {
        scroll_take(ctx, scope, i);
        return;
    }
    uint32_t id = scope;
    bool can_scroll = false;
    for (gesture_link_t link = gesture_link(ctx, id); link.s != NULL; link = gesture_parent(ctx, link, &id)) {
        if (link.axes == 0U) {
            continue;
        }
        can_scroll = true;
        bool flinging = false;
        for (int a = 0; a < 2; ++a) {
            flinging = flinging || (((link.axes & (1U << a)) != 0U) && fabsf(link.s->vel[a]) > NT_UI_SCROLL_FLING_STOP_DP * ctx->layout_per_dp[a]);
        }
        if (flinging && cap->active_id == 0U && !gesture_owned(ctx, id)) {
            link.s->vel[0] = 0.0F;
            link.s->vel[1] = 0.0F;
            link.s->target[0] = link.s->pos[0];
            link.s->target[1] = link.s->pos[1];
            link.s->raw[0] = link.s->pos[0];
            link.s->raw[1] = link.s->pos[1];
            cap->active_id = id;
            cap->hold_live = false; /* a claim is not a press: the slot's hold belongs to an earlier one */
            return;
        }
    }
    if (can_scroll) {
        cap->scroll_scope = scope;
    }
}

/* Claim: the excess past the slop is routed on the first crossing frame (no 8 dp loss, no jump); a
 * claim on a later frame anchors at the pointer. The owner path does the routing once. */
static void gesture_claim(nt_ui_context_t *ctx, uint32_t i, uint32_t id, gesture_link_t link, const float d[2], const float slop[2]) {
    nt_ui_capture_t *cap = &ctx->captures[i];
    const nt_pointer_t *p = &ctx->frame_pointers[i];
    const float prev[2] = {cap->pos[0] - cap->press_pos[0], cap->pos[1] - cap->press_pos[1]};
    const bool first_crossing = gesture_crossing(prev, slop, link.axes) == 0.0F;
    scroll_take(ctx, id, i);
    for (int a = 0; a < 2; ++a) {
        float route = 0.0F;
        if (first_crossing && (link.axes & (1U << a)) != 0U && fabsf(d[a]) > slop[a]) {
            route = (d[a] > 0.0F) ? (d[a] - slop[a]) : (d[a] + slop[a]);
        }
        cap->pos[a] = ((a == 0) ? p->x : p->y) - route;
        link.s->vel[a] = 0.0F;
    }
    link.s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_SCROLL_TO;
    cap->active_id = id;
    cap->hold_live = false; /* a claim is not a press: the slot's hold belongs to an earlier one */
}

/* Pending gesture: the strongest crossing wins; on a tie the pressed widget, then the inner scroll.
 * Nothing crossed keeps it pending, through a release too: the widget sees a pending release, a tap
 * (nt_ui_begin drops the scope next frame). */
static void gesture_decide(nt_ui_context_t *ctx, uint32_t i, const float slop[2]) {
    nt_ui_capture_t *cap = &ctx->captures[i];
    const nt_pointer_t *p = &ctx->frame_pointers[i];
    const float d[2] = {p->x - cap->press_pos[0], p->y - cap->press_pos[1]};
    float best = (cap->active_id != 0U) ? gesture_crossing(d, slop, cap->drag_axes) : 0.0F;
    uint32_t winner = 0U;
    gesture_link_t win = {NULL, 0U};
    uint32_t id = cap->scroll_scope;
    for (gesture_link_t link = gesture_link(ctx, id); link.s != NULL; link = gesture_parent(ctx, link, &id)) {
        const float r = gesture_crossing(d, slop, link.axes);
        if (r > best) {
            best = r;
            winner = id;
            win = link;
        }
    }
    if (best == 0.0F) {
        cap->pos[0] = p->x; /* last processed position: tells the first crossing frame apart */
        cap->pos[1] = p->y;
        return;
    }
    cap->scroll_scope = 0U;
    if (winner != 0U) {
        gesture_claim(ctx, i, winner, win, d, slop);
    }
}

/* This frame's wheel goes, per axis, to the first scroll on the hovered chain that can move on it. */
static void gesture_route_wheel(nt_ui_context_t *ctx, uint32_t i) {
    uint32_t id = ctx->pointer_scope[i];
    for (gesture_link_t link = gesture_link(ctx, id); link.s != NULL; link = gesture_parent(ctx, link, &id)) {
        for (int a = 0; a < 2; ++a) {
            if (ctx->wheel_owner[i][a] == 0U && (link.axes & (1U << a)) != 0U) {
                ctx->wheel_owner[i][a] = id;
            }
        }
    }
}

void nt_ui_internal_scroll_arbitrate(nt_ui_context_t *ctx) {
    const float slop[2] = {ctx->gesture_drag_slop_dp * ctx->layout_per_dp[0], ctx->gesture_drag_slop_dp * ctx->layout_per_dp[1]};
    for (uint32_t i = 0; i < ctx->frame_pointer_count; ++i) {
        nt_ui_capture_t *cap = &ctx->captures[i];
        const nt_button_state_t btn = ctx->frame_pointers[i].buttons[NT_BUTTON_LEFT];
#if NT_UI_DEBUG_TOOLS
        if (i == ctx->frame_primary && ctx->inspector_pointer_consumed) {
            /* The sidebar owns this pointer: only its panes scroll, and a game drag that entered it ends. */
            if (gesture_link(ctx, cap->active_id).s != NULL && !gesture_is_pane(ctx, cap->active_id)) {
                cap->active_id = 0U;
            }
            if (!gesture_is_pane(ctx, cap->scroll_scope)) {
                cap->scroll_scope = 0U;
            }
            if (!gesture_is_pane(ctx, ctx->pointer_scope[i])) {
                ctx->pointer_scope[i] = 0U;
            }
        }
#endif
        gesture_route_wheel(ctx, i);
        if (btn.is_pressed) {
            gesture_press(ctx, i);
            if (!btn.is_down) {
                cap->scroll_scope = 0U; /* pressed and released within one frame: a tap */
            }
        } else if (cap->scroll_scope != 0U) {
            gesture_decide(ctx, i, slop);
        }
    }
}
// #endregion

// #region owner path
/* The pointer that owns this container's drag moves it 1:1 on its scrollable axes (an axis that
 * stopped overflowing settles at rest). A released owner routes its last move and keeps the capture
 * through its release frame (children still see a foreign owner); nt_ui_begin frees it. Returns
 * whether an owner dragged this frame, the release frame included. */
static bool scroll_route_owner(nt_ui_context_t *ctx, uint32_t id, nt_ui_scroll_state_t *s, uint8_t axes) {
    bool dragged = false;
    for (uint32_t i = 0; i < ctx->frame_pointer_count; ++i) {
        nt_ui_capture_t *cap = &ctx->captures[i];
        if (cap->active_id != id) {
            continue;
        }
        const nt_pointer_t *p = &ctx->frame_pointers[i];
        const float d[2] = {p->x - cap->pos[0], p->y - cap->pos[1]};
        for (int a = 0; a < 2; ++a) {
            if ((axes & (1U << a)) != 0U) {
                s->raw[a] += d[a];
                s->vel[a] = scroll_sample_vel(s->vel[a], d[a], ctx->frame_dt);
            } else {
                s->raw[a] = 0.0F;
                s->pos[a] = 0.0F;
                s->vel[a] = 0.0F;
            }
        }
        cap->pos[0] = p->x;
        cap->pos[1] = p->y;
        dragged = true;
        if (cap->active_id == id && p->buttons[NT_BUTTON_LEFT].is_down) {
            ctx->capture_seen[i] = 1U;
        }
    }
    return dragged;
}

/* One frame of input for a scroll: chain link + surface record, this frame's wheel, and whether its
 * owner dragged it (the integrator then follows raw). */
static bool scroll_gather_input(nt_ui_context_t *ctx, uint32_t id, nt_ui_scroll_state_t *s, const Clay_ScrollContainerData *scd, float out_wheel[2]) {
    s->parent = nt_ui_internal_scroll_open(ctx);
    nt_ui_internal_register_interactive(ctx, id, NULL, id); /* the surface: empty-area presses start here */

    out_wheel[0] = 0.0F;
    out_wheel[1] = 0.0F;
    for (uint32_t i = 0; i < ctx->frame_pointer_count; ++i) {
        const nt_pointer_t *p = &ctx->frame_pointers[i];
        if (ctx->wheel_owner[i][0] == id) {
            out_wheel[0] += p->wheel_dx;
        }
        if (ctx->wheel_owner[i][1] == id) {
            out_wheel[1] += -p->wheel_dy; /* input edge: flip to Clay's negative-down */
        }
    }
    return scroll_route_owner(ctx, id, s, nt_ui_internal_scroll_axes(scd));
}
// #endregion

/* Scratch element_data carrying layer + whole-bar opacity (no transform). */
static nt_ui_element_data_t *scrollbar_make_data(uint8_t layer, float opacity) {
    nt_ui_element_data_t *d = NT_ARENA_ALLOC(&g_nt_mem_scratch, nt_ui_element_data_t);
    *d = (nt_ui_element_data_t){.user_data = NULL, .layer = layer, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_OPACITY, .transform = nt_ui_transform_defaults(), .opacity = opacity};
    return d;
}

#ifdef NT_TEST_ACCESS
/* Visibility predicate mirroring scrollbar_emit_axis: AUTO emits only on overflow;
 * ALWAYS / AUTO_HIDE register whenever the container has solved dims. Test-only. */
static bool scrollbar_was_emitted(const nt_ui_scroll_style_t *style, float content, float container) {
    if (container <= 0.0F) {
        return false;
    }
    if (style->bar_visibility == NT_UI_SCROLLBAR_AUTO) {
        return nt_ui_internal_overflows(content, container);
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
    frac = nt_ui_clampf(frac, 0.0F, 1.0F);
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

/* Thumb-drag + track-click for one axis, on the bar's own id and prev-frame bbox.
 * Drag delta along the axis maps back into s->pos
 * (Clay sign); a track click (off the thumb) smooth-jumps via nt_ui_scroll_to. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scrollbar_interact(nt_ui_context_t *ctx, uint32_t scroll_id, uint32_t bar_id, int axis, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s, float content, float container,
                               float bar_origin, float track_len, float thumb_off, float thumb_len, bool *out_active) {
    *out_active = false;
    int16_t pad[4];
    scrollbar_hit_pad(axis, style->bar_thickness, pad);
    const float over = content - container;
    const nt_ui_interaction_t in = nt_ui_step(ctx, bar_id, &(nt_ui_step_desc_t){.pad_lrtb = pad});
    if (!nt_ui_internal_overflows(content, container) || track_len - thumb_len <= 0.0F) {
        return; /* registered for a consistent hit-test, but nothing to drag */
    }
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
            frac = nt_ui_clampf(frac, 0.0F, 1.0F);
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
        frac = nt_ui_clampf(frac, 0.0F, 1.0F);
        s->pos[axis] = -(frac * over);
        s->target[axis] = s->pos[axis];
        s->raw[axis] = s->pos[axis]; /* a content drag that takes over continues from here */
        s->vel[axis] = 0.0F;
        s->idle = 0.0F;                                    /* bar-drag is activity (keeps AUTO_HIDE awake) */
        s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_SCROLL_TO; /* drag overrides any pending jump */
        *out_active = true;
    }
}

/* Emits one axis' 2-piece slice9 bar (track + thumb) as floating children of the open
 * scroll container, handles thumb-drag/track-click, and drives the AUTO_HIDE fade with
 * ONE nt_ui_anim call on the bar's derived id. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scrollbar_emit_axis(nt_ui_context_t *ctx, uint32_t scroll_id, int axis, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s, const float content[2], const float container[2],
                                const Clay_ElementData *cbb, uint8_t bar_layer) {
    const float clen = container[axis];
    const float ccontent = content[axis];
    if (clen <= 0.0F) {
        return; /* dims not solved yet (frame 1) */
    }
    /* AUTO hides entirely when there is no overflow; ALWAYS / AUTO_HIDE always register. */
    if (style->bar_visibility == NT_UI_SCROLLBAR_AUTO && !nt_ui_internal_overflows(ccontent, clen)) {
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
        bar_origin = (axis == 1) ? cbb->boundingBox.y : cbb->boundingBox.x;
    }

    bool dragging = false;
    scrollbar_interact(ctx, scroll_id, bar_id, axis, style, s, ccontent, clen, bar_origin, track_len, thumb_off, thumb_len, &dragging);

    /* AUTO_HIDE: visible on scroll activity (idle within the linger window), thumb-drag, or bar-hover;
     * fades out once s->idle passes bar_hide_delay. The integrator resets s->idle on any scroll motion,
     * so the bar appears the moment content moves. One anim call. */
    float opacity = 1.0F;
    if (style->bar_visibility == NT_UI_SCROLLBAR_AUTO_HIDE) {
        const nt_ui_interaction_t hov = nt_ui_query_interaction(ctx, bar_id);
        const bool active = s->idle < style->bar_hide_delay;
        const float fade_target = (active || dragging || hov.hovered) ? 1.0F : 0.0F;
        nt_ui_anim_target_t tgt = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = fade_target};
        const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, bar_id, &tgt, 0.0F, style->bar_fade_speed);
        opacity = nt_ui_clampf(a->value_t, 0.0F, 1.0F);
    }
#ifdef NT_TEST_ACCESS
    /* Single write site — geometry + opacity are final by here (incl. the faded case). */
    s_last_bar_thumb_len[axis] = thumb_len;
    s_last_bar_thumb_off[axis] = thumb_off;
    s_last_bar_track_len[axis] = track_len;
    s_last_bar_opacity[axis] = opacity;
    s_last_bar_layer[axis] = bar_layer;
#endif
    if (opacity < NT_UI_SCROLLBAR_FADE_EPS) {
        return; /* fully faded — nothing to draw (the id stays registered for next-frame hover) */
    }

    /* Bar draws on the container's own layer (NOT hardcoded 0): a scroll whose content sits on a higher
     * layer (e.g. a dropdown list on the IMAGE layer over an opaque panel) would otherwise emit its bar
     * on layer 0, sorted UNDER the panel and invisible. Floating still draws over siblings of the SAME
     * layer; matching the content layer keeps the bar above the panel fill. */
    const uint8_t layer = bar_layer;
    const float thickness = style->bar_thickness;
    /* Track spans the full axis on the trailing edge; thumb is offset along the axis. */
    Clay_ElementDeclaration track_decl;
    Clay_ElementDeclaration thumb_decl;
    /* clipTo the attached parent so a nested inner scroll's bar can't leak past an outer clip (and the
     * bar stays inside the container viewport). z-order/draw-on-top is unaffected — Clay clipTo only
     * adds a SCISSOR around the floating root, it does not touch zIndex sorting. */
    if (axis == 1) { /* vertical bar on the right edge */
        track_decl = (Clay_ElementDeclaration){
            .layout = {.sizing = {CLAY_SIZING_FIXED(thickness), CLAY_SIZING_FIXED(track_len)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP}},
        };
        thumb_decl = (Clay_ElementDeclaration){
            .layout = {.sizing = {CLAY_SIZING_FIXED(thickness), CLAY_SIZING_FIXED(thumb_len)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                         .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,
                         .offset = {.x = 0.0F, .y = thumb_off},
                         .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP}},
        };
    } else { /* horizontal bar on the bottom edge */
        track_decl = (Clay_ElementDeclaration){
            .layout = {.sizing = {CLAY_SIZING_FIXED(track_len), CLAY_SIZING_FIXED(thickness)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                         .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,
                         .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM}},
        };
        thumb_decl = (Clay_ElementDeclaration){
            .layout = {.sizing = {CLAY_SIZING_FIXED(thumb_len), CLAY_SIZING_FIXED(thickness)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                         .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,
                         .offset = {.x = thumb_off, .y = 0.0F},
                         .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM}},
        };
    }

    /* Track piece is the registered bar id (hit-test target for drag + track-click). */
    nt_atlas_region_ref_t track_ref = style->track_ref;
    nt_atlas_resolve_ref(&track_ref);
    track_decl.id = (Clay_ElementId){.id = bar_id};
    track_decl.userData = (void *)scrollbar_make_data(layer, opacity);
    if (track_ref.atlas.id != 0U && track_ref.region != NT_ATLAS_INVALID_REGION) {
        nt_ui_image_payload_t *tp = NT_ARENA_ALLOC(&g_nt_mem_scratch, nt_ui_image_payload_t);
        *tp = (nt_ui_image_payload_t){.atlas = track_ref.atlas, .region_index = track_ref.region, .slice9_scale = 1.0F};
        track_decl.image = (Clay_ImageElementConfig){.imageData = tp};
        track_decl.backgroundColor = nt_ui_unpack_tint(style->track_tint);
    }
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(track_decl);
    int16_t reg_pad[4];
    scrollbar_hit_pad(axis, thickness, reg_pad); /* same pad the interact step used (inspector overlay parity) */
    nt_ui_widget_register(ctx, bar_id, &NT_UI_SCROLLBAR_DEF, reg_pad, true);
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
                           const Clay_ElementData *cbb, uint8_t bar_layer) {
    if (style->scroll_x) {
        scrollbar_emit_axis(ctx, scroll_id, 0, style, s, content, container, cbb, bar_layer);
    }
    if (style->scroll_y) {
        scrollbar_emit_axis(ctx, scroll_id, 1, style, s, content, container, cbb, bar_layer);
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
    NT_ASSERT(isfinite(style->rubber_band_c) && style->rubber_band_c >= 0.0F && "nt_ui_scroll_begin: rubber_band_c must be finite >= 0");
    NT_ASSERT(isfinite(style->bounce_speed) && style->bounce_speed >= 0.0F && "nt_ui_scroll_begin: bounce_speed must be finite >= 0");
    NT_ASSERT((style->rubber_band_c == 0.0F || style->bounce_speed > 0.0F) && "nt_ui_scroll_begin: a rubber band needs bounce_speed > 0 to return");
    NT_ASSERT(isfinite(style->bar_thickness) && style->bar_thickness >= 0.0F && "nt_ui_scroll_begin: bar_thickness must be finite >= 0");
    NT_ASSERT(isfinite(style->bar_thumb_min_px) && style->bar_thumb_min_px >= 0.0F && "nt_ui_scroll_begin: bar_thumb_min_px must be finite >= 0");
    NT_ASSERT(isfinite(style->bar_fade_speed) && style->bar_fade_speed >= 0.0F && "nt_ui_scroll_begin: bar_fade_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->bar_hide_delay) && style->bar_hide_delay >= 0.0F && "nt_ui_scroll_begin: bar_hide_delay must be finite >= 0");
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_scroll_begin: decl->id must be 0 (scroll id passed separately)");
        NT_ASSERT(decl->clip.horizontal == false && decl->clip.vertical == false && "nt_ui_scroll_begin: decl->clip must be zero (style controls)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_scroll_begin: decl->userData must be NULL (data param controls)");
    }

    const bool fresh_state = nt_ui_state_find(ctx, id) == NULL;
    nt_ui_scroll_state_t *s = nt_ui_state(ctx, id, (uint32_t)sizeof *s, NT_UI_SCROLL_TAG);
    NT_ASSERT(s != NULL && "nt_ui_scroll_begin: state pool returned NULL");
    if (fresh_state) {
        s->idle = NT_UI_SCROLL_BORN_IDLE; /* born idle: AUTO_HIDE bar starts hidden, no frame-1 flash (any activity resets to 0) */
    }

    /* Clamp bounds from Clay's post-layout measurement (found==false on frame 1 ->
     * dims 0 -> bounds default to 0, accepted 1-frame lag). */
    const Clay_ScrollContainerData scd = Clay_GetScrollContainerData((Clay_ElementId){.id = id});
    const float content[2] = {scd.contentDimensions.width, scd.contentDimensions.height};
    const float container[2] = {scd.scrollContainerDimensions.width, scd.scrollContainerDimensions.height};

    /* One bbox fetch per container per frame — the bar origin. */
    const Clay_ElementData cbb = Clay_GetElementData((Clay_ElementId){.id = id});

    float wheel[2];
    const bool dragging = scroll_gather_input(ctx, id, s, &scd, wheel);
    nt_ui_internal_scroll_push(ctx, id);

    scroll_integrate(s, wheel, ctx->frame_dt, content, container, style, dragging);

    Clay_ElementDeclaration final = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    final.id = (Clay_ElementId){.id = id};
    final.clip = (Clay_ClipElementConfig){
        .horizontal = style->scroll_x, .vertical = style->scroll_y, .childOffset = (Clay_Vector2){.x = s->pos[0], .y = s->pos[1]}, /* OURS, never Clay's */
    };
    final.userData = (void *)data;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(final);
    nt_ui_widget_register(ctx, id, &NT_UI_SCROLL_DEF, NULL, true);

    /* Scrollbars float over the container edges (escape the clip, no layout cost). They
     * read THIS frame's offset (s->pos) but the bbox/content dims at a 1-frame lag. The bar draws on
     * the container's own layer so it isn't buried under higher-layer content (e.g. a dropdown panel). */
    const uint8_t bar_layer = (data != NULL) ? data->layer : 0U;
    scrollbar_emit(ctx, id, style, s, content, container, &cbb, bar_layer);

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
    nt_ui_internal_scroll_pop(ctx, false);
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

/* Lightweight scroll for engine-internal clip panes (inspector) with no widget style / bar. Drives
 * the same integrator + drag gesture as game containers off prev-frame Clay dims: instant wheel,
 * 1:1 pointer/touch drag with friction fling on release, hard edge clamp (no rubber/bounce).
 * Call it before the pane's clip element opens and feed the result to its childOffset. */
Clay_Vector2 nt_ui_internal_scroll_pane_offset(nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_scroll_pane_offset: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_internal_scroll_pane_offset: id must be non-zero");
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state(ctx, id, (uint32_t)sizeof *s, NT_UI_SCROLL_TAG);

    /* Prev-frame dims; found==false (frame 1, clip not solved) -> 0 -> bounds 0 (1-frame lag). */
    const Clay_ScrollContainerData scd = Clay_GetScrollContainerData((Clay_ElementId){.id = id});
    const float content[2] = {scd.contentDimensions.width, scd.contentDimensions.height};
    const float container[2] = {scd.scrollContainerDimensions.width, scd.scrollContainerDimensions.height};

    /* Inspector panes share game scroll physics with instant wheel and hard edge clamping. */
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.wheel_ease_speed = 0.0F;
    style.rubber_band_c = 0.0F;
    style.bounce_speed = 0.0F;

    /* The inspector emits after every game scroll closed, so a pane's chain never reaches one. */
    float wheel[2];
    const bool dragging = scroll_gather_input(ctx, id, s, &scd, wheel);
    s->flags |= NT_UI_SCROLL_FLAG_PANE;
    scroll_integrate(s, wheel, ctx->frame_dt, content, container, &style, dragging);
    return (Clay_Vector2){.x = s->pos[0], .y = s->pos[1]};
}

#ifdef NT_TEST_ACCESS
void nt_ui_scroll_test_integrate(nt_ui_scroll_state_t *s, const float wheel[2], float dt, const float content[2], const float container[2], const nt_ui_scroll_style_t *style, bool dragging) {
    NT_ASSERT(s != NULL && wheel != NULL && content != NULL && container != NULL && style != NULL && "nt_ui_scroll_test_integrate: null arg");
    scroll_integrate(s, wheel, dt, content, container, style, dragging);
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
uint8_t nt_ui_scroll_test_last_bar_layer(int axis) {
    NT_ASSERT(axis == 0 || axis == 1);
    return s_last_bar_layer[axis];
}
#endif
