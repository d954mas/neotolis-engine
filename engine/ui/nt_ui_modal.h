#ifndef NT_UI_MODAL_H
#define NT_UI_MODAL_H

/* Modal helper: a within-frame composition of Clay floating elements (panel +
 * full-viewport backdrop), nt_ui_block_pointer (input-gate occluder), and the
 * nt_ui_anim value_t ease (open/close tween). Signal-not-action: the helper never
 * closes itself, it only raises close_requested (D-60-06). It owns NO scene,
 * lifecycle, or assets (D-60-01/02) and pauses nothing (D-60-05) — only pointer
 * routing is auto-gated via the backdrop occluder (D-60-03). */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui.h" /* nt_ui_layer_t, nt_ui_widget_def_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_MODAL_DEF;

/* A source may RAISE a close (the game decides what to do); the helper never closes.
 * BACK is reserved — no Android-back platform event exists today (D-60-08). */
typedef enum {
    NT_UI_MODAL_CLOSE_NONE = 0,
    NT_UI_MODAL_CLOSE_ESC,
    NT_UI_MODAL_CLOSE_BACKDROP,
    NT_UI_MODAL_CLOSE_BACK, /* reserved; never raised this phase */
} nt_ui_modal_close_reason_t;

/* Per-frame result. t is the eased open amount; visible gates body declaration
 * (IM exit-animation rule D-60-11: keep declaring while t>epsilon). */
typedef struct {
    float t;                           /* eased 0..1 open amount */
    nt_ui_modal_close_reason_t reason; /* source that raised the close this frame */
    bool close_requested;              /* reason != NONE */
    bool visible;                      /* t > epsilon — body should be declared */
    bool fully_closed;                 /* !open && t <= epsilon */
} nt_ui_modal_result_t;
_Static_assert(sizeof(nt_ui_modal_result_t) == 12, "nt_ui_modal_result_t stable ABI (1 float + 1 enum[4] + 3 bool + 1 pad)");

/* Style flags. The transition selector is 2 bits (fade / scale-pop / slide). A flag
 * only lets a source RAISE close_requested — it never closes (D-60-07). */
#define NT_UI_MODAL_LISTEN_ESC ((uint8_t)(1U << 0))
#define NT_UI_MODAL_CLOSE_ON_BACKDROP ((uint8_t)(1U << 1))
/* Transition selector occupies bits 2-3; mask with NT_UI_MODAL_TRANSITION_MASK. */
#define NT_UI_MODAL_TRANSITION_SCALE_POP ((uint8_t)(0U << 2)) /* default: scale 0.92->1 + alpha */
#define NT_UI_MODAL_TRANSITION_FADE ((uint8_t)(1U << 2))      /* alpha only, scale fixed at 1 */
#define NT_UI_MODAL_TRANSITION_SLIDE ((uint8_t)(2U << 2))     /* slide-from-edge + alpha */
#define NT_UI_MODAL_TRANSITION_MASK ((uint8_t)(3U << 2))

typedef struct {
    float ease_speed;        /* value_speed for the open/close tween; > 0 (0 = instant snap) */
    float scale_start;       /* scale-pop start (default 0.92); eases to 1.0. Asserted > 0 */
    float backdrop_alpha;    /* peak backdrop opacity at t==1 (0..1) */
    float slide_offset;      /* SLIDE transition: start offset px (additive, eased to 0) */
    uint32_t backdrop_color; /* 0xAABBGGRR; alpha multiplied by t*backdrop_alpha */
    nt_ui_layer_t layer;     /* draw layer for backdrop + panel element data */
    uint8_t flags;           /* LISTEN_ESC | CLOSE_ON_BACKDROP | transition selector */
    uint8_t _pad[2];
} nt_ui_modal_style_t;
_Static_assert(sizeof(nt_ui_modal_style_t) == 24, "nt_ui_modal_style_t stable ABI (4 float + 1 u32 + 1 u8 + 1 u8 + 2 pad)");

/* A valid baseline: scale-pop + alpha, sane ease speed and scale-start > 0. Dodges the
 * {0} trap (scale_start must be > 0 — nt_ui_anim asserts scale_* > 0). */
nt_ui_modal_style_t nt_ui_modal_style_defaults(void);

/* Low-level (control): opens the backdrop + panel floating elements at z-band 1000*(depth+1),
 * pushes id onto the active-modal stack, eases t toward open?1:0. The panel stays OPEN until
 * nt_ui_modal_end (balanced, mirror nt_ui_scroll_begin/end). Returns the per-frame result; the
 * game owns the open bool and decides what to do with close_requested. id non-zero, style non-NULL.
 * Asserts depth < NT_UI_MODAL_MAX_DEPTH BEFORE push (MODAL-05 overflow, no silent fallback). */
nt_ui_modal_result_t nt_ui_modal_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool open);
void nt_ui_modal_end(nt_ui_context_t *ctx);

/* High-level (simple): the game stores ONE bool. Returns true while the body must be declared
 * (open OR animating-closed); clears *p_open on close_requested; returns false (and balances the
 * stack itself) once fully closed (D-60-09). Usage:
 *   if (nt_ui_modal(ctx, ID, &style, &open)) { ...body...; nt_ui_modal_end(ctx); } */
bool nt_ui_modal(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool *p_open);

/* True while any modal is up. The game gates its own keyboard/gameplay hotkeys on this
 * (D-60-04); pointer routing is already auto-gated by the backdrop occluder (D-60-03). */
bool nt_ui_modal_active(const nt_ui_context_t *ctx);

#ifdef NT_TEST_ACCESS
uint16_t nt_ui_modal_test_last_zband(void);                            /* panel z of the last begin */
uint16_t nt_ui_modal_test_last_backdrop_zband(void);                   /* backdrop z of the last begin */
uint8_t nt_ui_modal_test_stack_depth(const nt_ui_context_t *ctx);      /* live active_modal_depth */
nt_ui_modal_close_reason_t nt_ui_modal_test_last_close_reason(void);   /* reason of the last begin */
float nt_ui_modal_test_tween(const nt_ui_context_t *ctx, uint32_t id); /* eased t in the anim slot */
#endif

#endif /* NT_UI_MODAL_H */
