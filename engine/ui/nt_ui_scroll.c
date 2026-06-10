#include "ui/nt_ui_scroll.h"

#include <math.h>

#include "core/nt_assert.h"

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
            float k = wheel_ease * dt;
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

#ifdef NT_TEST_ACCESS
void nt_ui_scroll_test_integrate(nt_ui_scroll_state_t *s, const float wheel[2], float dt, const float content[2], const float container[2], const float tunables[5]) {
    NT_ASSERT(s != NULL && wheel != NULL && content != NULL && container != NULL && tunables != NULL && "nt_ui_scroll_test_integrate: null arg");
    scroll_integrate(s, wheel, dt, content, container, tunables);
}
float nt_ui_scroll_test_rubber_band(float d, float dim) { return nt_ui_rubber_band(d, dim, 0.55F); }
#endif
