#ifndef TJ_UI_KIT_H
#define TJ_UI_KIT_H

/* Reusable widget styling on top of nt_ui. Centralizes the "big and bright"
 * look so scenes stay declarative. */

#include <stdbool.h>

#include "ui/nt_ui_label.h"

#include "game.h"

typedef enum {
    TJ_BTN_PRIMARY = 0,
    TJ_BTN_SECONDARY,
    TJ_BTN_DANGER,
} tj_btn_variant_t;

extern const nt_ui_label_style_t TJ_STYLE_TITLE;
extern const nt_ui_label_style_t TJ_STYLE_HEADING;
extern const nt_ui_label_style_t TJ_STYLE_BODY;
extern const nt_ui_label_style_t TJ_STYLE_HINT;

/* Big slice9 button with eased hover/press. Returns true on click.
 * Call inside a CLAY parent; id_str must be stable across frames. */
bool tj_button(game_ctx_t *g, const char *id_str, const char *text, int w, int h, tj_btn_variant_t variant);

/* Horizontal volume-style slider. Drag anywhere on the track; returns the new
 * value in [0,1]. id_str must be stable across frames. */
float tj_slider(game_ctx_t *g, const char *id_str, float value);

/* Press-and-hold button: a red bar fills over `hold_seconds` while held;
 * releasing early resets. Returns true on the single frame the hold completes.
 * `accum` is caller-owned elapsed-hold state (one float, persisted by caller). */
bool tj_hold_button(game_ctx_t *g, const char *id_str, const char *text, float dt, float hold_seconds, float *accum);

#endif /* TJ_UI_KIT_H */
