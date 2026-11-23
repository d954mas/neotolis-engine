#ifndef NT_UI_MODAL_H
#define NT_UI_MODAL_H

/* Modal helper: panel + full-viewport backdrop floating elements, an input-gate occluder, and an
 * open/close tween. The game owns the `bool open`; Esc and a backdrop click (per flags) clear it. Conditional
 * begin/end: end only after begin returned true. It owns no scene, lifecycle,
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

/* Per-frame info (nullable out of begin): the tween, and the source that closed the modal this frame. */
typedef struct {
    float t;                           /* eased 0..1 open amount */
    nt_ui_modal_close_reason_t reason; /* NONE unless a source cleared *open this frame */
} nt_ui_modal_info_t;
_Static_assert(sizeof(nt_ui_modal_info_t) == 8, "nt_ui_modal_info_t stable ABI (1 float + 1 enum[4])");

/* Style flags — only gate close SOURCES (Esc / backdrop click): a flagged source clears the game's open bool. */
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

/* Returns whether the modal shows this frame: open, or still easing closed after *open was cleared. Only
 * then it opens the backdrop + panel floating elements (a root modal on the frame's next root band, a
 * nested one a band above its parent); the game declares the body and calls nt_ui_modal_end. Esc and a
 * backdrop click (per style flags) clear *open; info->reason says which. The game owns *open:
 *
 *   if (clicked_show) open = true;                            // open from any site: set your bool
 *   if (nt_ui_modal_begin(ctx, ID, &style, &open, NULL)) {    // declare once per frame, stable place
 *       CLAY({ ...panel... }) { if (ok_clicked) open = false; }
 *       nt_ui_modal_end(ctx);
 *   }
 *
 * One window, many open sites = ONE bool + ONE id; call this from the active container's body so
 * nesting/z follow context. id non-zero, style and open non-NULL, info nullable. Asserts depth <
 * NT_UI_MODAL_MAX_DEPTH. */
bool nt_ui_modal_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool *open, nt_ui_modal_info_t *info);
void nt_ui_modal_end(nt_ui_context_t *ctx);

/* Drop modal-owned retained view state for `id` after a transient modal/sheet has closed. Child
 * scroll/input/slider state inside the modal body remains caller-owned and should be cleared by id
 * when it should not survive close/reopen. */
void nt_ui_modal_clear_state(nt_ui_context_t *ctx, uint32_t id);

#ifdef NT_TEST_ACCESS
void nt_ui_modal_test_last_panel_offset(float *ox, float *oy); /* panel transform offset of the last begin */
#endif

#endif /* NT_UI_MODAL_H */
