#ifndef NT_UI_POPUP_INTERNAL_H
#define NT_UI_POPUP_INTERNAL_H

/* Module-internal extended popup-core entry. The modal (nt_ui_modal.c) re-expresses itself on top of
 * this: it supplies a visible dim backdrop color, an Esc-close opt-in, and a close-pad, and reads back
 * the close source so it can map it to its own close-reason enum. The public nt_ui_popup_begin is a
 * thin wrapper over nt_ui_popup_begin_internal with no backdrop / no Esc / zero pad. NOT a public API. */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui.h" /* nt_ui_widget_def_t, nt_ui_transform_t */
#include "ui/nt_ui_popup.h"

typedef struct nt_ui_context nt_ui_context_t;

/* Close source raised by the core this frame (mapped by the caller to its own enum). */
typedef enum {
    NT_UI_POPUP_CLOSE_SRC_NONE = 0,
    NT_UI_POPUP_CLOSE_SRC_DISMISS, /* outside-click on the catcher (beyond the close-pad) */
    NT_UI_POPUP_CLOSE_SRC_ESC,     /* Escape on the top popup (esc_close requested) */
} nt_ui_popup_close_src_t;

/* Extended config layered onto the base popup-core. All zero = the public behavior. */
typedef struct {
    const nt_ui_transform_t *open_xf;  /* override the eased panel transform start (open phase); NULL = style */
    const nt_ui_transform_t *close_xf; /* override (close phase); NULL = style */
    const nt_ui_widget_def_t *def;     /* widget_register def; NULL = NT_UI_POPUP_DEF */
    uint32_t backdrop_color;           /* 0xAABBGGRR; 0 = no visible backdrop (transparent catcher) */
    float backdrop_alpha;              /* peak backdrop opacity at t==1 (0..1); multiplied into the color alpha */
    int16_t close_pad;                 /* px margin around the panel; an outside-click within it does NOT dismiss */
    bool esc_close;                    /* Escape on the top popup raises NT_UI_POPUP_CLOSE_SRC_ESC */
    bool dismiss_on_click;             /* an outside-click beyond the panel+pad raises NT_UI_POPUP_CLOSE_SRC_DISMISS */
    bool force_catcher;                /* declare the catcher even with no backdrop + no dismiss (inert input gate) */
} nt_ui_popup_ext_t;

/* The shared core: z-band push + tween + present-only catcher/backdrop + close-scan + panel decl.
 * out_src (nullable) receives the close source. ext (nullable) layers backdrop/esc/pad/def on top. */
nt_ui_popup_result_t nt_ui_popup_begin_internal(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, bool open, const nt_ui_popup_ext_t *ext,
                                                nt_ui_popup_close_src_t *out_src);

#endif /* NT_UI_POPUP_INTERNAL_H */
