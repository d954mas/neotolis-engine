#ifndef NT_UI_POPUP_H
#define NT_UI_POPUP_H

/* Popup-core: the shared floating-overlay primitive. One panel floating element + a
 * transparent light-dismiss catcher (block_pointer occluder) + an open/close value_t tween + a
 * one-stride-per-level z-band + trigger-anchoring with per-side edge-flip. The game owns the `bool open`;
 * the core clears it on an outside press or Esc (light-dismiss) and never opens it.
 * Conditional begin/end like the menu: end only after begin returned true. The modal is re-expressed on top of this
 * core (modal = popup-core centered + dim backdrop). Dropdown / tooltip / context-menu reuse it. */

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

/* Per-frame info, for a game that draws with the tween or the chosen side (nullable out of begin). */
typedef struct {
    float t;      /* eased 0..1 open amount */
    uint8_t side; /* nt_ui_popup_side_t actually chosen (post edge-flip) */
    uint8_t _pad[3];
} nt_ui_popup_info_t;
_Static_assert(sizeof(nt_ui_popup_info_t) == 8, "nt_ui_popup_info_t stable ABI (1 float + 1 u8 + 3 pad)");

/* Catcher mode: a popup that wants light-dismiss declares a transparent full-viewport catcher under
 * its panel; a tooltip (NO dismiss) declares none. */
#define NT_UI_POPUP_LIGHT_DISMISS ((uint8_t)(1U << 0)) /* transparent catcher -> an outside press or Esc closes */

typedef struct {
    float ease_speed;                 /* value_speed for the open/close tween; >= 0 (0 = instant snap) */
    nt_ui_transform_t open_xf_start;  /* panel transform at t=0 while opening (eases to identity at t=1) */
    nt_ui_transform_t close_xf_start; /* panel transform at t=0 while closing (independent of open) */
    nt_ui_layer_t layer;              /* draw layer for catcher + panel element data */
    uint8_t flags;                    /* NT_UI_POPUP_LIGHT_DISMISS */
    uint8_t _pad[2];
} nt_ui_popup_style_t;
_Static_assert(sizeof(nt_ui_popup_style_t) == 80, "nt_ui_popup_style_t stable ABI (1 float + 2 transform[36] + 1 u8 + 1 u8 + 2 pad)");

/* Valid baseline: instant-identity tween (no spatial animation), light-dismiss on. Transforms are
 * identity (scale 1) so a {0}-init never trips nt_ui_anim's scale_*>0 assert. */
nt_ui_popup_style_t nt_ui_popup_style_defaults(void);

/* Returns whether the popup shows this frame: open, or still easing closed after the game cleared *open.
 * Only then it opens the catcher (light-dismiss) + panel floating elements (a root popup on the frame's
 * next root band, a nested one a band above its parent) eased toward open?1:0, placed at the anchor with
 * edge-flip (or centered for a CENTER anchor); the game declares the body and calls nt_ui_popup_end. A
 * hidden popup declares nothing. An outside press or Esc clears *open. The game owns *open:
 *
 *   if (clicked_trigger) open = true;
 *   if (nt_ui_popup_begin(ctx, ID, &style, &anchor, &open, NULL)) {
 *       CLAY({ ...popup body... }) { if (select) open = false; }
 *       nt_ui_popup_end(ctx);
 *   }
 *
 * id non-zero, style, anchor and open non-NULL; info nullable. Asserts depth < NT_UI_MODAL_MAX_DEPTH. */
bool nt_ui_popup_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, bool *open, nt_ui_popup_info_t *info);
void nt_ui_popup_end(nt_ui_context_t *ctx);

#ifdef NT_TEST_ACCESS
bool nt_ui_popup_test_last_catcher_present(void);    /* did the last begin declare a catcher? */
uint32_t nt_ui_popup_test_entrance_seed_count(void); /* cumulative entrance t=0 re-seeds (once per open-edge) */
void nt_ui_popup_test_entrance_seed_reset(void);     /* zero the entrance-seed counter */
#endif

#endif /* NT_UI_POPUP_H */
