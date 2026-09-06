#ifndef NT_UI_SCROLL_H
#define NT_UI_SCROLL_H

/* Scroll container with custom engine-side physics (momentum/fling, overscroll
 * rubber-band + bounce, smooth wheel, animated scroll-to). The engine computes
 * the offset and feeds Clay a ready clip.childOffset each frame — Clay's built-in
 * scroll is bypassed. Scroll state rides the nt_ui_state pool keyed by
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
 * UI units — the space passed to nt_ui_begin (games typically feed nt_ui_scale's
 * logical space, making this resolution-independent) — steals capture from the inner
 * widget and scrolls. Mobile standard ~8-10 px. The latch frame routes ZERO delta
 * (the inner widget already applied it;
 * re-routing would double-move once); scroll tracks 1:1 from the next frame. The
 * cancelled inner widget keeps any value it wrote before the steal (Model D — value
 * lives in the game), so commit-on-release is the safe pattern for sliders. */
#ifndef NT_UI_SCROLL_STEAL_THRESHOLD_PX
#define NT_UI_SCROLL_STEAL_THRESHOLD_PX 8.0F
#endif

/* Scroll-to active: ease pos toward target, clear on arrival. */
#define NT_UI_SCROLL_FLAG_SCROLL_TO ((uint8_t)(1U << 0))
/* A pointer is dragging this container (set by capture-steal); gates rubber-band. */
#define NT_UI_SCROLL_FLAG_DRAGGING ((uint8_t)(1U << 1))
/* A free pointer (no widget capture) is mid-press inside the bbox; free_press_pos is its anchor. */
#define NT_UI_SCROLL_FLAG_FREE_PRESS ((uint8_t)(1U << 2))
/* Drag crossed the threshold and latched: dead-zone consumed, so later frames route the
 * full per-frame delta 1:1 (only the latching frame subtracts the threshold). Per-CONTAINER,
 * not per-pointer — single-touch by design. */
#define NT_UI_SCROLL_FLAG_DRAG_LATCHED ((uint8_t)(1U << 3))

/* Per-container retained state in the nt_ui_state pool. pos/vel/target in Clay's
 * negative-down sign convention. ~52B; under NT_UI_STATE_PAYLOAD_MAX. */
typedef struct {
    float pos[2];            /* DISPLAY offset fed to clip.childOffset (raw clamped/rubber-banded) */
    float raw[2];            /* un-rubber-banded scroll pos: accumulates finger 1:1 even past the edge (drag anchor) */
    float vel[2];            /* momentum velocity (px/s), sampled from drag for the release fling */
    float target[2];         /* smooth-wheel + scroll-to target */
    float free_press_pos[2]; /* free-press drag anchor (re-anchored each frame; FREE_PRESS gates it) */
    float idle;              /* seconds since last scroll activity (AUTO_HIDE linger; dt-accumulated, no wall clock) */
    float thumb_grab;        /* offset of the press point within the thumb (track-axis px); set on thumb-press */
    uint8_t flags;
} nt_ui_scroll_state_t;

/* Scrollbar visibility (defined here so the style stays one struct). Bars always float
 * as an overlay; a reserved-gutter layout would need real engine support and is not modelled. */
typedef enum { NT_UI_SCROLLBAR_ALWAYS = 0, NT_UI_SCROLLBAR_AUTO, NT_UI_SCROLLBAR_AUTO_HIDE } nt_ui_scrollbar_visibility_t;

typedef struct {
    bool scroll_x, scroll_y; /* which axes scroll */
    float friction;          /* momentum decay (per-60fps base) */
    float wheel_ease_speed;  /* smooth-wheel ease toward target; 0 = instant */
    float rubber_band_c;     /* overscroll coefficient (0 = no rubber-band; default 0.55) */
    float bounce_speed;      /* bounce-back spring ease */
    float wheel_step_px;     /* UI units (nt_ui_begin space) per wheel notch; name kept for ABI */
    /* scrollbar visual (declared here for one-struct ABI). */
    nt_ui_scrollbar_visibility_t bar_visibility;
    float bar_thickness;
    float bar_thumb_min_px;
    float bar_fade_speed; /* AUTO_HIDE fade via nt_ui_anim value_t */
    float bar_hide_delay; /* AUTO_HIDE: seconds of idle before the bar fades out (iOS/macOS overlay linger ~1.2s) */
    /* Track-click semantics: clicking the track OFF the thumb smooth-jumps to the clicked spot
     * (macOS "jump to the spot that's clicked" option), not page-by-page — chosen for game UI. */
    nt_atlas_region_ref_t track_ref, thumb_ref;
    uint32_t track_tint, thumb_tint;
} nt_ui_scroll_style_t;
/* 88 not 84: the 16B atlas refs force 8B struct alignment, so dropping bar_placement just moved
 * 4B of padding ahead of track_ref — the size is unchanged. */
_Static_assert(sizeof(nt_ui_scroll_style_t) == 88, "nt_ui_scroll_style_t stable ABI (2 bool + 5 float + 1 enum + 4 float[8B aligned] + 2x16B ref + 2 u32)");

/* A valid baseline: mobile-game-like tunables, y-only scroll, ALWAYS-visible overlay bar. */
nt_ui_scroll_style_t nt_ui_scroll_style_defaults(void);

/* Opens a Clay clip element whose childOffset comes from OUR physics (not Clay).
 * Engine OWNS .id / .clip / .userData on the decl; the caller's decl supplies sizing/
 * padding only. Scroll state rides the nt_ui_state pool keyed by id. Must be balanced
 * with nt_ui_scroll_end. */
void nt_ui_scroll_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const nt_ui_scroll_style_t *style, const Clay_ElementDeclaration *decl);
void nt_ui_scroll_end(nt_ui_context_t *ctx);

/* Set the target offset (Clay sign: negative = down/right) and ease there.
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
/* Scrollbar readbacks: which axes' bars emitted (bit0=x, bit1=y) + the last per-axis
 * bar geometry (thumb len/off, track len, eased fade opacity) + the derived id. */
uint8_t nt_ui_scroll_test_last_bar_emitted_axes(void);
void nt_ui_scroll_test_last_bar_geometry(int axis, float *thumb_len, float *thumb_off, float *track_len, float *opacity);
uint32_t nt_ui_scroll_test_bar_id(uint32_t scroll_id, int axis);
/* The bar floats at its container's own band (delta 0) and is declared last, so it paints over the
 * container's content — but a floating declared after it in the same band paints over the bar.
 *
 * The draw layer the last-emitted bar used (per axis); must match the container's content layer so the
 * bar isn't buried under higher-layer content. */
uint8_t nt_ui_scroll_test_last_bar_layer(int axis);
/* Count of scroll_begin gathers that consumed a wheel since the last reset; lets a multi-container
 * test detect a wheel reaching more than one container in a single frame (the broadcast bug). */
uint32_t nt_ui_scroll_test_wheel_recipients(void);
void nt_ui_scroll_test_wheel_recipients_reset(void);
#endif

#endif /* NT_UI_SCROLL_H */
