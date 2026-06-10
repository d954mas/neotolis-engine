#include "ui/nt_ui_scroll.h"

#include <math.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_state.h"

const nt_ui_widget_def_t NT_UI_SCROLL_DEF = {
    .name = "nt_scroll",
    .pill_color = 0xFF40C0B0U,
    ._reserved = 0U,
};

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

/* Capture-steal (D-59-04, Pitfall 2): a pointer captured by an INNER widget whose drag
 * exceeds the threshold along a scrolling axis hands the gesture to the scroll container.
 * Clearing captures[i].active_id makes the inner widget's next step see released_now /
 * clicked=false (a cancel), and routes the drag delta into our velocity/pos. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scroll_steal_check(nt_ui_context_t *ctx, uint32_t id, const nt_ui_scroll_style_t *style, nt_ui_scroll_state_t *s) {
    bool any_drag = false;
    const float dt = ctx->frame_dt;
    for (uint32_t i = 0; i < ctx->frame_pointer_count; ++i) {
        nt_ui_capture_t *cap = &ctx->captures[i];
        const bool inner_capture = cap->active_id != 0U && cap->active_id != id;
        if (!inner_capture) {
            continue;
        }
        /* Only steal if the press began inside this container. */
        if (!point_in_bbox(id, cap->press_pos[0], cap->press_pos[1])) {
            continue;
        }
        const float ddx = cap->pos[0] - cap->press_pos[0];
        const float ddy = cap->pos[1] - cap->press_pos[1];
        const float axis_move = (style->scroll_x ? fabsf(ddx) : 0.0F) + (style->scroll_y ? fabsf(ddy) : 0.0F);
        if (axis_move < NT_UI_SCROLL_STEAL_THRESHOLD_PX) {
            continue; /* tap (below threshold) leaves the inner capture -> inner clicks */
        }
        /* Steal: cancel the inner widget, route the per-frame drag delta into our offset. */
        cap->active_id = 0U;
        any_drag = true;
        if (style->scroll_x) {
            s->pos[0] += ddx;
            if (dt > 0.0F) {
                s->vel[0] = ddx / dt;
            }
        }
        if (style->scroll_y) {
            s->pos[1] += ddy;
            if (dt > 0.0F) {
                s->vel[1] = ddy / dt;
            }
        }
        /* Re-anchor the press so next frame's delta is incremental, not cumulative. */
        cap->press_pos[0] = cap->pos[0];
        cap->press_pos[1] = cap->pos[1];
    }
    if (any_drag) {
        s->flags |= NT_UI_SCROLL_FLAG_DRAGGING;
    } else {
        s->flags &= (uint8_t)~NT_UI_SCROLL_FLAG_DRAGGING;
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

    nt_ui_scroll_state_t *s = nt_ui_state(ctx, id, (uint32_t)sizeof *s);
    NT_ASSERT(s != NULL && "nt_ui_scroll_begin: state pool returned NULL");

    /* Clamp bounds from Clay's post-layout measurement (found==false on frame 1 ->
     * dims 0 -> bounds default to 0, accepted 1-frame lag). */
    const Clay_ScrollContainerData scd = Clay_GetScrollContainerData((Clay_ElementId){.id = id});
    const float content[2] = {scd.contentDimensions.width, scd.contentDimensions.height};
    const float container[2] = {scd.scrollContainerDimensions.width, scd.scrollContainerDimensions.height};

    /* Capture-steal updates DRAGGING + routes drag delta before the integrate. */
    scroll_steal_check(ctx, id, style, s);

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

#ifdef NT_TEST_ACCESS
    s_last_child_offset[0] = s->pos[0];
    s_last_child_offset[1] = s->pos[1];
    s_last_scroll_id = id;
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
#endif
