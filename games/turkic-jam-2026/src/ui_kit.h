#ifndef TJ_UI_KIT_H
#define TJ_UI_KIT_H

/* Reusable widget styling on top of nt_ui. Centralizes the "big and bright"
 * look so scenes stay declarative. */

#include <stdbool.h>

#include "ui/nt_ui_label.h"

#include "game.h"

typedef enum {
    TJ_BTN_PRIMARY = 0, /* blue */
    TJ_BTN_SECONDARY,   /* green */
    TJ_BTN_DANGER,      /* red */
} tj_btn_variant_t;

extern const nt_ui_label_style_t TJ_STYLE_TITLE;
extern const nt_ui_label_style_t TJ_STYLE_HEADING;
extern const nt_ui_label_style_t TJ_STYLE_BODY;
extern const nt_ui_label_style_t TJ_STYLE_HINT;

/* Big slice9 button with eased hover/press. Returns true on click.
 * Call inside a CLAY parent; id_str must be stable across frames. */
bool tj_button(game_ctx_t *g, const char *id_str, const char *text, int w, int h, tj_btn_variant_t variant);

#endif /* TJ_UI_KIT_H */
