#ifndef NT_UI_LABEL_H
#define NT_UI_LABEL_H

/* Stateless text widget. text is copied into per-frame scratch — callers may pass transient buffers. */

#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_LABEL_DEF;

typedef struct {
    uint16_t font_id;         /* asserted < NT_UI_MAX_FONTS */
    float font_size;          /* px; asserted > 0 */
    Clay_Color color;         /* 0..255 (Clay convention) */
    uint16_t line_height;     /* 0 = auto from font metrics */
    uint16_t letter_tracking; /* maps to Clay letterSpacing */
    uint8_t wrap_mode;        /* Clay_TextElementConfigWrapMode; 0 = WORDS */
    uint8_t align;            /* Clay_TextAlignment; 0 = LEFT */
} nt_ui_label_style_t;
_Static_assert(sizeof(nt_ui_label_style_t) <= 32, "nt_ui_label_style_t fits in 32 B");

/* data may be NULL (= no layer, no user_data); built with NT_UI_DATA_LAYER / _FULL. */
void nt_ui_label(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style);

/* Keeps style static-const; pair with nt_ui_fit_*. */
void nt_ui_label_sized(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style, float font_size_override);

#endif /* NT_UI_LABEL_H */
