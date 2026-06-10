#ifndef NT_UI_SCROLL_H
#define NT_UI_SCROLL_H

/* Scroll container with custom engine-side physics (momentum/fling, overscroll
 * rubber-band + bounce, smooth wheel, animated scroll-to). The engine computes
 * the offset and feeds Clay a ready clip.childOffset each frame — Clay's built-in
 * scroll is bypassed (D-59-01). Scroll state rides the nt_ui_state pool keyed by
 * the scroll id; offset is in Clay's NEGATIVE-down sign convention (childOffset is
 * negative going down/right; clamp [-(content-container), 0]). */

#include <stdbool.h>
#include <stdint.h>

#include "stddef.h"

typedef struct nt_ui_context nt_ui_context_t;

/* Capture-steal threshold: a drag inside a scroll container exceeding this many
 * framebuffer px steals capture from the inner widget (cancel its click) and
 * scrolls (D-59-04). Mobile standard ~8-10 px. */
#ifndef NT_UI_SCROLL_STEAL_THRESHOLD_PX
#define NT_UI_SCROLL_STEAL_THRESHOLD_PX 8.0F
#endif

/* Scroll-to active: ease pos toward target, clear on arrival. */
#define NT_UI_SCROLL_FLAG_SCROLL_TO ((uint8_t)(1U << 0))
/* A pointer is dragging this container (set by capture-steal); gates rubber-band. */
#define NT_UI_SCROLL_FLAG_DRAGGING ((uint8_t)(1U << 1))

/* Per-container retained state in the nt_ui_state pool. pos/vel/target in Clay's
 * negative-down sign convention. ~28B; under NT_UI_STATE_PAYLOAD_MAX. */
typedef struct {
    float pos[2];    /* current offset fed to clip.childOffset */
    float vel[2];    /* momentum velocity (px/s) */
    float target[2]; /* smooth-wheel + scroll-to target */
    uint8_t flags;
} nt_ui_scroll_state_t;

#ifdef NT_TEST_ACCESS
/* Physics-only probe: drives the static integrator directly so unit tests can
 * assert momentum decay / clamp / rubber-band / scroll-to without a Clay frame.
 * tunables: {friction, wheel_ease_speed, rubber_band_c, bounce_speed, wheel_step_px}. */
void nt_ui_scroll_test_integrate(nt_ui_scroll_state_t *s, const float wheel[2], float dt, const float content[2], const float container[2], const float tunables[5]);
float nt_ui_scroll_test_rubber_band(float d, float dim);
#endif

#endif /* NT_UI_SCROLL_H */
