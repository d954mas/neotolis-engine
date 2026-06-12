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

    for (int a = 0; a < 2; ++a) {
        const float lo = scroll_min_bound(content[a], container[a]);
        const float hi = 0.0F;

        /* Wheel adds to target (input edge already feeds Clay-sign via begin;
         * here wheel[] arrives in Clay sign). pos eases toward target — no teleport. */
        if (wheel[a] != 0.0F) {
            s->target[a] += wheel[a] * wheel_step;
            s->target[a] = scroll_clampf(s->target[a], lo, hi);
        }

        /* Smooth-wheel + scroll-to: ease pos toward target until it ARRIVES — not
         * only on the wheel frame, or the offset stalls before reaching target. */
        const bool easing = scroll_to || (fabsf(s->target[a] - s->pos[a]) > NT_UI_SCROLL_POS_EPS && !dragging && fabsf(s->vel[a]) <= NT_UI_SCROLL_VEL_EPS);
        if (easing) {
            /* wheel_ease 0 = instant (matches nt_ui_anim speed-0 convention), no teleport otherwise. */
            float k = (wheel_ease <= 0.0F) ? 1.0F : wheel_ease * dt;
            k = scroll_clampf(k, 0.0F, 1.0F);
            s->pos[a] += (s->target[a] - s->pos[a]) * k;
            s->vel[a] = 0.0F; /* eased motion owns pos; momentum stands down */
        } else if (!dragging) {
            /* Momentum/fling: exponential decay, frame-rate independent. target
             * tracks pos so a later wheel/scroll-to starts from the rest point. */
            if (fabsf(s->vel[a]) > NT_UI_SCROLL_VEL_EPS) {
                s->pos[a] += s->vel[a] * dt;
                s->vel[a] *= powf(friction, dt * 60.0F);
                s->target[a] = scroll_clampf(s->pos[a], lo, hi);
            } else {
                s->vel[a] = 0.0F;
            }
        } else {
            /* Active drag owns pos directly; keep target synced for release. */
            s->target[a] = s->pos[a];
        }

        /* Overscroll: while dragging, past-edge distance rubber-bands (compressed).
         * Released + out of bounds: critically-damped pull back to the clamped edge. */
        if (s->pos[a] < lo || s->pos[a] > hi) {
            const float edge = (s->pos[a] < lo) ? lo : hi;
            const float over = s->pos[a] - edge;
            if (dragging && rubber_c > 0.0F) {
                s->pos[a] = edge + nt_ui_rubber_band(over, container[a], rubber_c);
            } else {
                float kb = bounce * dt;
                kb = scroll_clampf(kb, 0.0F, 1.0F);
                s->pos[a] += (edge - s->pos[a]) * kb;
                s->vel[a] = 0.0F; /* bounce absorbs momentum */
                if (fabsf(s->pos[a] - edge) < NT_UI_SCROLL_POS_EPS) {
                    s->pos[a] = edge;
                }
            }
        } else if (!dragging) {
            /* In-bounds: keep target inside so the next wheel/scroll-to starts clamped. */
            s->target[a] = scroll_clampf(s->target[a], lo, hi);
            s->pos[a] = scroll_clampf(s->pos[a], lo, hi);
        }
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

/* Is the framebuffer-px point (px,py) inside the scroll container's layout bbox?
 * Uses Clay's persistent hashmap (PREVIOUS frame's bbox) — scroll_begin runs during
 * declaration, so this frame's bbox is not solved yet (the 1-frame lag we accept).
 * 2D ctx: capture press_pos (UI-space px) and bbox (Clay Y-down px) align. */
static bool point_in_bbox(uint32_t id, float px, float py) {
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    if (!d.found) {
        return false;
    }
    const Clay_BoundingBox bb = d.boundingBox;
    return px >= bb.x && px <= (bb.x + bb.width) && py >= bb.y && py <= (bb.y + bb.height);
}

/* Route one incremental drag delta (drag_x, drag_y in fb px) into our offset/velocity.
 * dt<=0 has no fresh velocity — zero it so a prior fling's momentum can't leak past this
 * drag frame (a stale vel would resume on release). */
static void scroll_route_drag(nt_ui_scroll_state_t *s, const nt_ui_scroll_style_t *style, float drag_x, float drag_y, float dt) {
    if (style->scroll_x) {
        s->pos[0] += drag_x;
        s->vel[0] = (dt > 0.0F) ? (drag_x / dt) : 0.0F;
    }
    if (style->scroll_y) {
        s->pos[1] += drag_y;
        s->vel[1] = (dt > 0.0F) ? (drag_y / dt) : 0.0F;
    }
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

/* Per-pointer drag arbitration (D-59-04, Pitfall 2). One drag-routing path, two entry conditions:
 *   - STEAL: a pointer captured by an INNER widget whose press began in our bbox and dragged past
 *     the threshold hands the gesture over — clearing captures[i].active_id delivers the inner
 *     widget a released_now/clicked=false cancel on its next step.
 *   - FREE-PRESS: a pointer with no capture at all (press on non-interactive content/empty space)
 *     pressed inside our bbox and dragged past the threshold scrolls directly — a finger can start
 *     a fling without landing on a widget. The press anchor rides the state cell (free_press_pos).
 * Either way the per-frame delta routes into pos/vel and the anchor re-anchors so next frame's
 * delta is incremental, not cumulative. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scroll_drag_check(nt_ui_context_t *ctx, uint32_t id, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s) {
    bool any_drag = false;
    bool any_free_press = false;
    const float dt = ctx->frame_dt;
    for (uint32_t i = 0; i < ctx->frame_pointer_count; ++i) {
        nt_ui_capture_t *cap = &ctx->captures[i];
        const bool inner_capture = cap->active_id != 0U && cap->active_id != id && !scroll_capture_excluded(ctx, id, cap->active_id);

        if (inner_capture) {
            /* STEAL path: drag origin + cur pos come from the inner widget's capture. */
            if (!point_in_bbox(id, cap->press_pos[0], cap->press_pos[1])) {
                continue;
            }
            const float ddx = cap->pos[0] - cap->press_pos[0];
            const float ddy = cap->pos[1] - cap->press_pos[1];
            const float axis_move = (style->scroll_x ? fabsf(ddx) : 0.0F) + (style->scroll_y ? fabsf(ddy) : 0.0F);
            if (axis_move < NT_UI_SCROLL_STEAL_THRESHOLD_PX) {
                continue; /* tap (below threshold) leaves the inner capture -> inner clicks */
            }
            cap->active_id = 0U; /* cancel the inner widget */
            scroll_route_drag(s, style, ddx, ddy, dt);
            cap->press_pos[0] = cap->pos[0];
            cap->press_pos[1] = cap->pos[1];
            any_drag = true;
            continue;
        }

        /* FREE-PRESS path: no widget owns this pointer. Track its press origin ourselves. */
        if (cap->active_id != 0U) {
            continue; /* captured by THIS container's bar or an excluded nested scroller */
        }
        const nt_pointer_t *p = &ctx->frame_pointers[i];
        const nt_button_state_t btn = p->buttons[NT_BUTTON_LEFT];
        if (!btn.is_down) {
            continue; /* release ends the free press; momentum/fling owns the rest (DRAGGING clears) */
        }
        const bool press_began = (s->flags & NT_UI_SCROLL_FLAG_FREE_PRESS) == 0U;
        if (press_began) {
            if (!point_in_bbox(id, p->x, p->y)) {
                continue; /* press landed outside the container — not our gesture */
            }
            s->free_press_pos[0] = p->x;
            s->free_press_pos[1] = p->y;
            any_free_press = true;
            continue; /* anchor set this frame; delta accrues from next frame */
        }
        const float ddx = p->x - s->free_press_pos[0];
        const float ddy = p->y - s->free_press_pos[1];
        const float axis_move = (style->scroll_x ? fabsf(ddx) : 0.0F) + (style->scroll_y ? fabsf(ddy) : 0.0F);
        any_free_press = true;
        if (axis_move < NT_UI_SCROLL_STEAL_THRESHOLD_PX) {
            continue; /* still within the tap threshold — not yet a drag */
        }
        scroll_route_drag(s, style, ddx, ddy, dt);
        s->free_press_pos[0] = p->x;
        s->free_press_pos[1] = p->y;
        any_drag = true;
    }
    if (any_drag) {
        s->flags |= NT_UI_SCROLL_FLAG_DRAGGING;
    } else {
        s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_DRAGGING;
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

/* Thumb-drag + track-click for one axis. Reads the bar's prev-frame bbox, steals nothing
 * from the container (different id). Drag delta along the axis maps back into s->pos
 * (Clay sign); a track click (off the thumb) smooth-jumps via nt_ui_scroll_to. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scrollbar_interact(nt_ui_context_t *ctx, uint32_t scroll_id, uint32_t bar_id, int axis, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s, float content, float container,
                               float bar_origin, float track_len, float thumb_off, float thumb_len, bool *out_active) {
    *out_active = false;
    const float over = content - container;
    if (over <= 0.0F || track_len - thumb_len <= 0.0F) {
        (void)nt_ui_step_interaction(ctx, bar_id); /* keep the id registered (consistent hit-test) */
        return;
    }
    const nt_ui_interaction_t in = nt_ui_step_interaction(ctx, bar_id);
    const float pointer_a = (axis == 1) ? in.pos[1] : in.pos[0];
    const float press_a = (axis == 1) ? in.press_pos[1] : in.press_pos[0];

    if (in.pressed_now) {
        const float thumb_lo = bar_origin + thumb_off;
        const bool on_thumb = press_a >= thumb_lo && press_a <= (thumb_lo + thumb_len);
        if (!on_thumb) {
            /* Track click (off the thumb): smooth-jump the clicked fraction. */
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
        /* Held: map the pointer position along the track straight to the offset (1:1 grab
         * is approximated by absolute mapping — thumb follows the pointer). */
        float frac = (pointer_a - bar_origin - (thumb_len * 0.5F)) / (track_len - thumb_len);
        frac = scroll_clampf(frac, 0.0F, 1.0F);
        s->pos[axis] = -(frac * over);
        s->target[axis] = s->pos[axis];
        s->vel[axis] = 0.0F;
        s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_SCROLL_TO; /* drag overrides any pending jump */
        *out_active = true;
    }
    (void)style;
}

/* Emits one axis' 2-piece slice9 bar (track + thumb) as floating children of the open
 * scroll container, handles thumb-drag/track-click, and drives the AUTO_HIDE fade with
 * ONE nt_ui_anim call on the bar's derived id. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scrollbar_emit_axis(nt_ui_context_t *ctx, uint32_t scroll_id, int axis, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s, const float content[2], const float container[2]) {
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

    /* Bar origin in the container's local space (floating attaches at the container edge).
     * Read boundingBox only when found — frame 1 (layout unsolved) leaves it garbage. */
    const Clay_ElementData cd = Clay_GetElementData((Clay_ElementId){.id = scroll_id});
    float bar_origin = 0.0F;
    if (cd.found) {
        bar_origin = (axis == 1) ? cd.boundingBox.y : cd.boundingBox.x;
    }

    bool dragging = false;
    scrollbar_interact(ctx, scroll_id, bar_id, axis, style, s, ccontent, clen, bar_origin, track_len, thumb_off, thumb_len, &dragging);

    /* AUTO_HIDE: fade in on drag/hover, out after idle. ALWAYS / AUTO stay opaque. ONE anim. */
    float opacity = 1.0F;
    if (style->bar_visibility == NT_UI_SCROLLBAR_AUTO_HIDE) {
        const nt_ui_interaction_t hov = nt_ui_query_interaction(ctx, bar_id);
        const float fade_target = (dragging || hov.hovered) ? 1.0F : 0.0F;
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
    nt_ui_widget_register(ctx, bar_id, &NT_UI_SCROLLBAR_DEF, NULL);
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
static void scrollbar_emit(nt_ui_context_t *ctx, uint32_t scroll_id, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s, const float content[2], const float container[2]) {
    if (style->scroll_x) {
        scrollbar_emit_axis(ctx, scroll_id, 0, style, s, content, container);
    }
    if (style->scroll_y) {
        scrollbar_emit_axis(ctx, scroll_id, 1, style, s, content, container);
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

    /* Drag arbitration (steal + free-press) updates DRAGGING + routes drag delta before integrate. */
    scroll_drag_check(ctx, id, style, s);

    /* Wheel: only the container the pointer is over receives it (Clay sign = -wheel_dy). */
    float wheel[2] = {0.0F, 0.0F};
    for (uint32_t i = 0; i < ctx->frame_pointer_count; ++i) {
        const nt_pointer_t *p = &ctx->frame_pointers[i];
        if ((p->wheel_dx != 0.0F || p->wheel_dy != 0.0F) && point_in_bbox(id, p->x, p->y)) {
            if (style->scroll_x) {
                wheel[0] += p->wheel_dx;
            }
            if (style->scroll_y) {
                wheel[1] += -p->wheel_dy; /* input edge: flip to Clay's negative-down */
            }
        }
    }

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
    scrollbar_emit(ctx, id, style, s, content, container);

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

    /* Wheel only when the pointer is over THIS pane (prev-frame bbox), Clay negative-down sign. */
    float wheel[2] = {0.0F, 0.0F};
    for (uint32_t i = 0; i < ctx->frame_pointer_count; ++i) {
        const nt_pointer_t *p = &ctx->frame_pointers[i];
        if ((p->wheel_dx != 0.0F || p->wheel_dy != 0.0F) && point_in_bbox(id, p->x, p->y)) {
            if (scroll_x) {
                wheel[0] += p->wheel_dx;
            }
            if (scroll_y) {
                wheel[1] += -p->wheel_dy;
            }
        }
    }

    /* Instant wheel, no overscroll: wheel_ease 0 -> pos snaps to target, rubber/bounce 0. */
    const float tun[NT_UI_SCROLL_T_COUNT] = {0.92F, 0.0F, 0.0F, 0.0F, 40.0F};
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
