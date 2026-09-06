#ifndef NT_UI_MODAL_H
#define NT_UI_MODAL_H

/* Modal helper: panel + full-viewport backdrop floating elements, an input-gate occluder, and an
 * open/close tween. The game owns the `bool open`; the helper only RAISES close_requested and never
 * closes itself. begin/end are balanced (mirror nt_ui_scroll_begin/end). It owns no scene, lifecycle,
 * or assets, and pauses nothing — only pointer routing is auto-gated by the backdrop occluder. */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui.h" /* nt_ui_layer_t, nt_ui_widget_def_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_MODAL_DEF;

/* Source that raised a close; the game decides what to do. */
typedef enum {
    NT_UI_MODAL_CLOSE_NONE = 0,
    NT_UI_MODAL_CLOSE_ESC,
    NT_UI_MODAL_CLOSE_BACKDROP,
    NT_UI_MODAL_CLOSE_BACK, /* reserved; no platform back event yet */
} nt_ui_modal_close_reason_t;

/* Per-frame result. t is the eased open amount; visible gates body declaration
 * (IM exit-anim: keep declaring the body while t>epsilon, so the close tween plays). */
typedef struct {
    float t;                           /* eased 0..1 open amount */
    nt_ui_modal_close_reason_t reason; /* source that raised the close this frame */
    bool close_requested;              /* reason != NONE */
    bool visible;                      /* t > epsilon — body should be declared */
    bool fully_closed;                 /* !open && t <= epsilon */
} nt_ui_modal_result_t;
_Static_assert(sizeof(nt_ui_modal_result_t) == 12, "nt_ui_modal_result_t stable ABI (1 float + 1 enum[4] + 3 bool + 1 pad)");

/* Style flags — only gate close SOURCES (Esc / backdrop click). A flag lets a source RAISE
 * close_requested; it never closes by itself. */
#define NT_UI_MODAL_LISTEN_ESC ((uint8_t)(1U << 0))
#define NT_UI_MODAL_CLOSE_ON_BACKDROP ((uint8_t)(1U << 1))

/* Animation recipe: a typed t->transform mapping. opacity always rides t (every type fades); the
 * type only picks the spatial part. open and close are independent (e.g. slide-in / fade-out). */
typedef enum {
    NT_UI_MODAL_ANIM_SCALE_POP = 0, /* scale scale_start->1 */
    NT_UI_MODAL_ANIM_FADE,          /* alpha only, no transform */
    NT_UI_MODAL_ANIM_SLIDE,         /* slide `offset` px from/toward `edge` */
} nt_ui_modal_anim_type_t;

typedef enum {
    NT_UI_MODAL_BOTTOM = 0,
    NT_UI_MODAL_TOP,
    NT_UI_MODAL_LEFT,
    NT_UI_MODAL_RIGHT,
} nt_ui_modal_edge_t;

typedef struct {
    uint8_t type;      /* nt_ui_modal_anim_type_t */
    uint8_t edge;      /* nt_ui_modal_edge_t — SLIDE origin (open) / exit (close) edge */
    float offset;      /* SLIDE: start/exit offset px (eased to 0) */
    float scale_start; /* SCALE_POP: start scale, eases to 1.0. Asserted > 0 */
} nt_ui_modal_anim_t;
_Static_assert(sizeof(nt_ui_modal_anim_t) == 12, "nt_ui_modal_anim_t stable ABI (2 u8 + 2 pad + 2 float)");

typedef struct {
    float ease_speed;           /* value_speed for the open/close tween; >= 0 (0 = instant snap) */
    float backdrop_alpha;       /* peak backdrop opacity at t==1 (0..1) */
    nt_ui_modal_anim_t open;    /* entrance recipe */
    nt_ui_modal_anim_t close;   /* exit recipe; independent (e.g. slide-in / fade-out). symmetric = close == open */
    uint32_t backdrop_color;    /* 0xAABBGGRR; alpha multiplied by t*backdrop_alpha */
    nt_ui_layer_t layer;        /* draw layer for backdrop + panel element data */
    uint8_t flags;              /* LISTEN_ESC | CLOSE_ON_BACKDROP */
    int16_t backdrop_close_pad; /* clicks within this px margin around the panel do NOT close — avoids accidental close on near-panel taps */
} nt_ui_modal_style_t;
_Static_assert(sizeof(nt_ui_modal_style_t) == 40, "nt_ui_modal_style_t stable ABI (2 float + 2 anim[12] + 1 u32 + 2 u8 + 1 int16)");

/* Valid baseline: symmetric scale-pop entrance + exit. scale_start > 0 (nt_ui_anim asserts scale_* > 0). */
nt_ui_modal_style_t nt_ui_modal_style_defaults(void);

/* Low-level, UNCONDITIONAL begin/end (like nt_ui_scroll_begin): always begin -> ... -> end. Opens the
 * backdrop + panel floating elements (declared one ctx->modal_zband_stride above the enclosing
 * floating; Clay accumulates the nesting) and eases t toward
 * open?1:0; returns the full result (t / close reason / visible). Panel stays OPEN until nt_ui_modal_end.
 * id non-zero, style non-NULL. Asserts depth < NT_UI_MODAL_MAX_DEPTH BEFORE push (overflow, no fallback).
 * Prefer the scoped nt_ui_modal_visible() unless you need the close reason. */
nt_ui_modal_result_t nt_ui_modal_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool open);
void nt_ui_modal_end(nt_ui_context_t *ctx);

/* High-level scoped form: the game stores ONE bool. Returns "declare the body THIS frame?" — true while
 * open OR still animating closed; this is NOT "is open" (the block keeps running a few frames after you
 * set open=false, so the close tween plays). On close it clears *p_open; once fully closed it balances
 * the stack itself (no nt_ui_modal_end in that branch). Open from anywhere by just setting your bool:
 *
 *   if (clicked_show) open = true;                       // "open" = set your bool (any site)
 *   if (nt_ui_modal_visible(ctx, ID, &style, &open)) {   // declare once per frame, stable place
 *       CLAY({ ...panel... }) { if (ok_clicked) open = false; }
 *       nt_ui_modal_end(ctx);
 *   }
 *
 * One window, many open sites = ONE bool + ONE id; call this from the active container's body so
 * nesting/z follow context. Two windows at once = two ids + two bools. */
bool nt_ui_modal_visible(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool *p_open);

/* Drop modal-owned retained view state for `id` after a transient modal/sheet has closed. Child
 * scroll/input/slider state inside the modal body remains caller-owned and should be cleared by id
 * when it should not survive close/reopen. */
void nt_ui_modal_clear_state(nt_ui_context_t *ctx, uint32_t id);

/* True if a modal was up LAST frame (prev-frame presence — the live depth is 0 after nt_ui_end).
 * The game gates its keyboard/gameplay hotkeys on this; pointer routing is already auto-gated by
 * the backdrop occluder. */
bool nt_ui_modal_active(const nt_ui_context_t *ctx);

#ifdef NT_TEST_ACCESS
void nt_ui_modal_test_last_panel_offset(float *ox, float *oy); /* panel transform offset of the last begin */
#endif

#endif /* NT_UI_MODAL_H */
