#ifndef NT_UI_POPUP_INTERNAL_H
#define NT_UI_POPUP_INTERNAL_H

/* Module-internal extended popup-core entry. The modal (nt_ui_modal.c) and the menu re-express
 * themselves on top of this: a visible dim backdrop color, the Esc and outside-press closes, a close-pad, a
 * widget def. The public nt_ui_popup_begin is nt_ui_popup_begin_internal with the popup's ext. NOT a public API. */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui.h" /* nt_ui_widget_def_t, nt_ui_transform_t */
#include "ui/nt_ui_popup.h"

typedef struct nt_ui_context nt_ui_context_t;

/* Extended config layered onto the base popup-core. All zero = no backdrop and no own close. */
typedef struct {
    const nt_ui_widget_def_t *def; /* widget_register def; NULL = NT_UI_POPUP_DEF */
    uint32_t backdrop_color;       /* 0xAABBGGRR; 0 = no visible backdrop (transparent catcher) */
    float backdrop_alpha;          /* peak backdrop opacity at t==1 (0..1); multiplied into the color alpha */
    int16_t close_pad;             /* px margin around the panel; an outside press within it does NOT close */
    bool esc_close;                /* the top closes on the Escape it owns: NT_UI_POPUP_CLOSE_ESC */
    bool outside_close;            /* the top closes on a press beyond the panel+pad: NT_UI_POPUP_CLOSE_OUTSIDE */
} nt_ui_popup_ext_t;

/* The shared core for an overlay on the stack: tween + stack slot band + catcher/backdrop + panel decl, only for
 * a shown one; it records the own close nt_ui_begin runs next frame. */
bool nt_ui_popup_begin_internal(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, const nt_ui_popup_ext_t *ext, nt_ui_popup_info_t *info);

/* A tooltip: the same panel on the band above every stack slot, shown while `open` or fading out; no stack entry,
 * no catcher, no input. Ends with nt_ui_popup_end. */
bool nt_ui_internal_tooltip_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, bool open, nt_ui_popup_info_t *info);

#endif /* NT_UI_POPUP_INTERNAL_H */
