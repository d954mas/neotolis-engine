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

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t, nt_ui_widget_def_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_SCROLL_DEF;
extern const nt_ui_widget_def_t NT_UI_SCROLLBAR_DEF;

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

/* Scrollbar visibility / placement (visual consumed by Plan 04; defined here so the
 * style is one struct across the phase). */
typedef enum { NT_UI_SCROLLBAR_ALWAYS = 0, NT_UI_SCROLLBAR_AUTO, NT_UI_SCROLLBAR_AUTO_HIDE } nt_ui_scrollbar_visibility_t;
typedef enum { NT_UI_SCROLLBAR_OVERLAY = 0, NT_UI_SCROLLBAR_GUTTER } nt_ui_scrollbar_placement_t;

typedef struct {
    bool scroll_x, scroll_y; /* which axes scroll */
    float friction;          /* momentum decay (per-60fps base); D-59-02 tunable */
    float wheel_ease_speed;  /* smooth-wheel ease toward target; 0 = instant */
    float rubber_band_c;     /* overscroll coefficient (0 = no rubber-band; default 0.55) */
    float bounce_speed;      /* bounce-back spring ease */
    float wheel_step_px;     /* px per wheel unit */
    /* scrollbar visual (consumed by Plan 04; declared here for one-struct ABI). */
    nt_ui_scrollbar_visibility_t bar_visibility;
    nt_ui_scrollbar_placement_t bar_placement;
    float bar_thickness;
    float bar_thumb_min_px;
    float bar_fade_speed; /* AUTO_HIDE fade via nt_ui_anim value_t */
    nt_atlas_region_ref_t track_ref, thumb_ref;
    uint32_t track_tint, thumb_tint;
} nt_ui_scroll_style_t;
_Static_assert(sizeof(nt_ui_scroll_style_t) == 88, "nt_ui_scroll_style_t stable ABI (2 bool + 5 float + 2 enum + 3 float + 2x16B ref + 2 u32)");

/* A valid baseline: mobile-game-like tunables, y-only scroll, ALWAYS/OVERLAY bar. */
nt_ui_scroll_style_t nt_ui_scroll_style_defaults(void);

/* Opens a Clay clip element whose childOffset comes from OUR physics (not Clay).
 * Engine OWNS .id / .clip / .userData on the decl; the caller's decl supplies sizing/
 * padding only. Scroll state rides the nt_ui_state pool keyed by id. Must be balanced
 * with nt_ui_scroll_end. */
void nt_ui_scroll_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const nt_ui_scroll_style_t *style, const Clay_ElementDeclaration *decl);
void nt_ui_scroll_end(nt_ui_context_t *ctx);

/* Set the target offset (Clay sign: negative = down/right) and ease there (D-59-02d).
 * No-op if the container's state cell does not yet exist (call after first declaration). */
void nt_ui_scroll_to(nt_ui_context_t *ctx, uint32_t id, float x, float y);

#ifdef NT_TEST_ACCESS
/* Physics-only probe: drives the static integrator directly so unit tests can
 * assert momentum decay / clamp / rubber-band / scroll-to without a Clay frame.
 * tunables: {friction, wheel_ease_speed, rubber_band_c, bounce_speed, wheel_step_px}. */
void nt_ui_scroll_test_integrate(nt_ui_scroll_state_t *s, const float wheel[2], float dt, const float content[2], const float container[2], const float tunables[5]);
float nt_ui_scroll_test_rubber_band(float d, float dim);
/* Reads back the last clip.childOffset fed to Clay by scroll_begin (Clay sign). */
void nt_ui_scroll_test_last_child_offset(float *out_x, float *out_y);
uint32_t nt_ui_scroll_test_last_scroll_id(void);
/* Scrollbar readbacks (Plan 04): which axes' bars emitted (bit0=x, bit1=y) + the last
 * per-axis bar geometry (thumb len/off, track len, eased fade opacity) + the derived id. */
uint8_t nt_ui_scroll_test_last_bar_emitted_axes(void);
void nt_ui_scroll_test_last_bar_geometry(int axis, float *thumb_len, float *thumb_off, float *track_len, float *opacity);
uint32_t nt_ui_scroll_test_bar_id(uint32_t scroll_id, int axis);
#endif

#endif /* NT_UI_SCROLL_H */
