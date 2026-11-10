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
#include "ui/nt_ui_state.h"

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_SCROLL_DEF;
extern const nt_ui_widget_def_t NT_UI_SCROLLBAR_DEF;

/* Scroll gestures: a press records the scroll chain of the front-most widget under it (its
 * declaration scope). Once the pointer moves the drag slop (NT_UI_DRAG_SLOP_DP, 8 dp) along an
 * axis, the strongest crossing among the pressed widget's own drag axes (nt_ui_step_desc_t.drag_axes)
 * and the chain's scrollable axes (inner first) takes the gesture; a scroll that wins cancels the
 * widget (no click). The cancelled widget keeps any value it wrote before (Model D), so a draggable
 * holds value changes while nt_ui_interaction_t.gesture_pending is set. */

/* Scroll-to active: ease pos toward target, clear on arrival. */
#define NT_UI_SCROLL_FLAG_SCROLL_TO ((uint8_t)(1U << 0))
/* Bit 1 is engine-internal (nt_ui_internal.h). */

/* This frame declared the x / y bar: nt_ui_scroll_end adds its hit record after the content's, so a
 * row under the bar never outranks it. */
#define NT_UI_SCROLL_FLAG_BAR_X ((uint8_t)(1U << 2))
#define NT_UI_SCROLL_FLAG_BAR_Y ((uint8_t)(1U << 3))

/* State-pool tag of a scroll cell (game scrolls and inspector panes). */
#define NT_UI_SCROLL_TAG NT_UI_STATE_TAG('s', 'c', 'r', 'l')

/* Per-container retained state in the nt_ui_state pool. pos/vel/target in Clay's
 * negative-down sign convention. 48B; under NT_UI_STATE_PAYLOAD_MAX. */
typedef struct {
    float pos[2];     /* DISPLAY offset fed to clip.childOffset (raw clamped/rubber-banded) */
    float raw[2];     /* un-rubber-banded scroll pos: accumulates finger 1:1 even past the edge (drag anchor) */
    float vel[2];     /* momentum velocity (px/s), sampled from drag for the release fling */
    float target[2];  /* smooth-wheel + scroll-to target */
    float idle;       /* seconds since last scroll activity (AUTO_HIDE linger; dt-accumulated, no wall clock) */
    float thumb_grab; /* offset of the press point within the thumb (track-axis px); set on thumb-press */
    uint32_t parent;  /* enclosing scroll at the last declaration (0 = none or a popup barrier) */
    uint8_t flags;
    uint8_t bar_axis; /* 1 + axis while the owner drags this scroll by a thumb; 0 = content drag */
    uint16_t bar_pad; /* this frame's bar hit padding (style bar_hit_pad) */
} nt_ui_scroll_state_t;

/* Scrollbar visibility (defined here so the style stays one struct). Bars always float
 * as an overlay; a reserved-gutter layout would need real engine support and is not modelled. */
typedef enum { NT_UI_SCROLLBAR_ALWAYS = 0, NT_UI_SCROLLBAR_AUTO, NT_UI_SCROLLBAR_AUTO_HIDE } nt_ui_scrollbar_visibility_t;

typedef struct {
    bool scroll_x, scroll_y; /* which axes scroll */
    float friction;          /* momentum decay (per-60fps base) */
    float wheel_ease_speed;  /* smooth-wheel ease toward target; 0 = instant */
    float rubber_band_c;     /* overscroll coefficient (0 = hard edge for drag and fling; default 0.55) */
    float bounce_speed;      /* bounce-back spring ease; > 0 when rubber_band_c > 0 */
    float wheel_step_px;     /* UI units (nt_ui_begin space) per wheel notch */
    /* scrollbar visual */
    nt_ui_scrollbar_visibility_t bar_visibility;
    float bar_thickness;
    float bar_hit_pad; /* extra hit width from the visual bar toward the content (touch target); 0 = the bar only */
    float bar_thumb_min_px;
    float bar_fade_speed; /* AUTO_HIDE fade via nt_ui_anim value_t */
    float bar_hide_delay; /* AUTO_HIDE: seconds of idle before the bar fades out (iOS/macOS overlay linger ~1.2s) */
    /* Track-click semantics: clicking the track OFF the thumb smooth-jumps to the clicked spot
     * (macOS "jump to the spot that's clicked" option), not page-by-page — chosen for game UI. */
    nt_atlas_region_ref_t track_ref, thumb_ref;
    uint32_t track_tint, thumb_tint;
} nt_ui_scroll_style_t;

/* A valid baseline: mobile-game-like tunables, y-only scroll, ALWAYS-visible overlay bar. */
nt_ui_scroll_style_t nt_ui_scroll_style_defaults(void);

/* Opens a Clay clip element whose childOffset comes from OUR physics (not Clay).
 * Engine OWNS .id / .clip / .userData on the decl; the caller's decl supplies sizing/
 * padding only. Scroll state rides the nt_ui_state pool keyed by id. Must be balanced
 * with nt_ui_scroll_end. */
void nt_ui_scroll_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const nt_ui_scroll_style_t *style, const Clay_ElementDeclaration *decl);
/* Closes the container. The scrollbars are floating children declared by nt_ui_scroll_begin at the
 * container's own band (delta 0); their hit records are added here, after the content's, so a press on
 * a bar never goes to a row under it. */
void nt_ui_scroll_end(nt_ui_context_t *ctx);

/* Set the target offset (Clay sign: negative = down/right) and ease there.
 * No-op if the container's state cell does not yet exist (call after first declaration). */
void nt_ui_scroll_to(nt_ui_context_t *ctx, uint32_t id, float x, float y);

#ifdef NT_TEST_ACCESS
/* Physics-only probe: drives the static integrator directly so unit tests can
 * assert momentum decay / clamp / rubber-band / scroll-to without a Clay frame.
 * Reads physics settings from style for the duration of the call. */
void nt_ui_scroll_test_integrate(nt_ui_scroll_state_t *s, const float wheel[2], float dt, const float content[2], const float container[2], const nt_ui_scroll_style_t *style, bool dragging);
float nt_ui_scroll_test_rubber_band(float d, float dim);
/* Reads back the last clip.childOffset fed to Clay by scroll_begin (Clay sign). */
void nt_ui_scroll_test_last_child_offset(float *out_x, float *out_y);
uint32_t nt_ui_scroll_test_last_scroll_id(void);
/* Scrollbar readbacks: which axes' bars emitted (bit0=x, bit1=y) + the last per-axis
 * bar geometry (thumb len/off, track len, eased fade opacity) + the derived id. */
uint8_t nt_ui_scroll_test_last_bar_emitted_axes(void);
void nt_ui_scroll_test_last_bar_geometry(int axis, float *thumb_len, float *thumb_off, float *track_len, float *opacity);
uint32_t nt_ui_scroll_test_bar_id(uint32_t scroll_id, int axis);
/* The draw layer the last-emitted bar used (per axis); must match the container's content layer so the
 * bar isn't buried under higher-layer content. */
uint8_t nt_ui_scroll_test_last_bar_layer(int axis);
#endif

#endif /* NT_UI_SCROLL_H */
