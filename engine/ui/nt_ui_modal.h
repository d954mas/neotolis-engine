#ifndef NT_UI_MODAL_H
#define NT_UI_MODAL_H

/* Modal: panel + full-viewport backdrop floating elements, an input gate and an open/close tween. The game opens
 * and closes it with nt_ui_modal_open / nt_ui_close (nt_ui.h); Esc and a backdrop press close it only per style
 * flags. Conditional begin/end: end only after begin returned true. It owns no scene, lifecycle or assets. */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui.h"       /* nt_ui_layer_t, nt_ui_widget_def_t */
#include "ui/nt_ui_popup.h" /* nt_ui_popup_info_t, nt_ui_popup_close_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_MODAL_DEF;

/* Style flags: the close sources a modal listens to besides nt_ui_close. */
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
    nt_ui_modal_anim_t open;    /* entrance recipe */
    nt_ui_modal_anim_t close;   /* exit recipe; independent (e.g. slide-in / fade-out). symmetric = close == open */
    uint32_t backdrop_color;    /* 0xAABBGGRR; its alpha is the peak opacity at t==1, multiplied by t */
    nt_ui_layer_t layer;        /* draw layer for backdrop + panel element data */
    uint8_t flags;              /* LISTEN_ESC | CLOSE_ON_BACKDROP */
    int16_t backdrop_close_pad; /* clicks within this px margin around the panel do NOT close — avoids accidental close on near-panel taps */
} nt_ui_modal_style_t;
_Static_assert(sizeof(nt_ui_modal_style_t) == 36, "nt_ui_modal_style_t stable ABI (1 float + 2 anim[12] + 1 u32 + 2 u8 + 1 int16)");

/* Valid baseline: symmetric scale-pop entrance + exit. scale_start > 0 (nt_ui_anim asserts scale_* > 0). */
nt_ui_modal_style_t nt_ui_modal_style_defaults(void);

/* Opens the modal `id` on top of the overlay stack, inside the innermost open overlay being declared at the call
 * that is not a popup (0 = none), and closes every popup that is not its ancestor, also the menu or list it is
 * opened from; modals stay (spec: "Input ownership"). It takes input from the frame after its first declaration. */
void nt_ui_modal_open(nt_ui_context_t *ctx, uint32_t id);

/* Returns whether the modal shows this frame: open, or fading out after a close. Only then it declares the
 * backdrop + panel on its overlay stack slot's band; the game declares the body and calls nt_ui_modal_end:
 *
 *   if (clicked_show) nt_ui_modal_open(ctx, ID);
 *   if (nt_ui_modal_begin(ctx, ID, &style, NULL)) {
 *       CLAY({ ...panel... }) { if (ok_clicked) nt_ui_close(ctx, ID); }
 *       nt_ui_modal_end(ctx);
 *   }
 *
 * id non-zero, style non-NULL, info nullable (info->reason: what closed it, once). */
bool nt_ui_modal_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, nt_ui_popup_info_t *info);
void nt_ui_modal_end(nt_ui_context_t *ctx);

#endif /* NT_UI_MODAL_H */
