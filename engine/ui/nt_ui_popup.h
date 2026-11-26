#ifndef NT_UI_POPUP_H
#define NT_UI_POPUP_H

/* Popup-core: the shared floating-overlay primitive (tooltip, popup, modal, menu level). One panel floating
 * element + a catcher for an overlay that takes input + an open/close value_t tween + its overlay stack slot
 * (open order = paint order) + trigger-anchoring with per-side edge-flip. The open state is the stack's:
 * nt_ui_popup_open, nt_ui_close, nt_ui_is_open. Conditional begin/end: end only after begin returned true. */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui.h" /* nt_ui_layer_t, nt_ui_widget_def_t, nt_ui_transform_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_POPUP_DEF;

/* Placement side of the popup relative to its trigger anchor. The core flips the preferred side to
 * the opposite when the projected popup rect would overflow the matching screen border (edge-flip). */
typedef enum {
    NT_UI_POPUP_BELOW = 0, /* popup top edge attaches to the anchor bottom edge (default for a trigger) */
    NT_UI_POPUP_ABOVE,     /* flipped from BELOW near the bottom border */
    NT_UI_POPUP_RIGHT,     /* popup left edge attaches to the anchor right edge (submenu default) */
    NT_UI_POPUP_LEFT,      /* flipped from RIGHT near the right border */
    NT_UI_POPUP_CENTER,    /* centered on the screen (modal): no anchor, no edge-flip */
} nt_ui_popup_side_t;

/* Trigger anchor rect (UI-space, prev-frame). For a CENTER popup the rect is ignored. The core reads
 * the popup's own prev-frame size via nt_ui_get_bbox to decide whether prefer_side overflows a border;
 * found==false on the first frame falls back to prefer_side (cosmetic 1-frame lag, A3). */
typedef struct {
    float x, y, w, h;    /* trigger rect in UI space */
    uint8_t prefer_side; /* nt_ui_popup_side_t — desired side; the core may flip to the opposite */
    uint8_t _pad[3];
} nt_ui_popup_anchor_t;
_Static_assert(sizeof(nt_ui_popup_anchor_t) == 20, "nt_ui_popup_anchor_t stable ABI (4 float + 1 u8 + 3 pad)");

/* What closed an overlay, reported once by its begin. */
typedef enum {
    NT_UI_POPUP_CLOSE_NONE = 0,
    NT_UI_POPUP_CLOSE_ESC,
    NT_UI_POPUP_CLOSE_OUTSIDE, /* a press outside the panel: on its catcher (a modal's backdrop) or on no record */
    NT_UI_POPUP_CLOSE_OTHER,   /* nt_ui_close of it or of the overlay it was opened inside, an overlay opened outside it, its menu's activation, or no declaration */
} nt_ui_popup_close_t;

/* Per-frame info of a popup or a modal (nullable out of begin): the chosen side, the close. */
typedef struct {
    uint8_t side;   /* nt_ui_popup_side_t actually chosen (post edge-flip) */
    uint8_t reason; /* nt_ui_popup_close_t raised this frame */
} nt_ui_popup_info_t;

typedef struct {
    float ease_speed;                 /* value_speed for the open/close tween; >= 0 (0 = instant snap) */
    nt_ui_transform_t open_xf_start;  /* panel transform at t=0 while opening (eases to identity at t=1) */
    nt_ui_transform_t close_xf_start; /* panel transform at t=0 while closing (independent of open) */
    nt_ui_layer_t layer;              /* draw layer for catcher + panel element data */
    uint8_t _pad[3];
} nt_ui_popup_style_t;
_Static_assert(sizeof(nt_ui_popup_style_t) == 80, "nt_ui_popup_style_t stable ABI (1 float + 2 transform[36] + 1 u8 + 3 pad)");

/* Valid baseline: identity transforms (no spatial animation). Transforms are identity (scale 1) so a {0}-init
 * never trips nt_ui_anim's scale_*>0 assert. */
nt_ui_popup_style_t nt_ui_popup_style_defaults(void);

/* Opens the popup `id` on top of the overlay stack, inside the innermost open overlay being declared at the call (0 = none), and
 * closes every popup that is not its ancestor; opening an open one does nothing, a reopen during the fade starts
 * it again on top. It takes input from the frame after its first declaration (spec: "Input ownership"). */
void nt_ui_popup_open(nt_ui_context_t *ctx, uint32_t id);

/* Returns whether the popup shows this frame: open, or fading out after a close. Only then it declares the
 * catcher and the panel on its stack slot's band, eased toward open?1:0, placed at the anchor with edge-flip
 * (or centered for a CENTER anchor); the game declares the body and calls nt_ui_popup_end. It closes on Esc and
 * on a press outside it. A popup that is not open declares nothing:
 *
 *   if (clicked_trigger) nt_ui_popup_open(ctx, ID);
 *   if (nt_ui_popup_begin(ctx, ID, &style, &anchor, NULL)) {
 *       CLAY({ ...popup body... }) { if (select) nt_ui_close(ctx, ID); }
 *       nt_ui_popup_end(ctx);
 *   }
 *
 * id non-zero, style and anchor non-NULL; info nullable (info->reason: what closed it, once). */
bool nt_ui_popup_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, nt_ui_popup_info_t *info);
void nt_ui_popup_end(nt_ui_context_t *ctx);

#endif /* NT_UI_POPUP_H */
