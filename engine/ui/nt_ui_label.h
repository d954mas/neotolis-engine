#ifndef NT_UI_LABEL_H
#define NT_UI_LABEL_H

/* Stateless text widget. Style is passed by const-pointer; engine asserts
 * style != NULL, font_id < NT_UI_MAX_FONTS, font slot bound, font_size > 0.
 * text must live until nt_ui_end completes (Clay stores .chars by pointer). */

#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

/* EXPERIMENTAL (Phase 56 ext): API surface may change in v1.9. Used by
 * ui_buttons_demo and inspector internals. Game code adopting this should
 * pin the engine version.
 *
 * Inspector descriptor: pill name "nt_label" + color shown in the element
 * tree. Engine widget defs use the "nt_" prefix to disambiguate from Clay's
 * own config-type pills ("Text" appears on the same row from Clay's port).
 * nt_ui_label auto-registers the descriptor against the just-emitted CLAY_TEXT
 * leaf's id so a row tagged "nt_label" appears alongside the text content. */
extern const nt_ui_widget_def_t NT_UI_LABEL_DEF;

typedef struct {
    uint16_t font_id;         /* index into ctx->fonts[]; asserted < NT_UI_MAX_FONTS */
    float font_size;          /* px; asserted > 0. Passed to Clay as (uint16_t) for layout, float for rendering. */
    Clay_Color color;         /* 4 floats, 0..255 (Clay convention) */
    uint16_t line_height;     /* px; 0 = auto from font metrics */
    uint16_t letter_tracking; /* px; maps to Clay letterSpacing */
    uint8_t wrap_mode;        /* Clay_TextElementConfigWrapMode value; 0 = CLAY_TEXT_WRAP_WORDS default */
    uint8_t align;            /* Clay_TextAlignment value; 0 = CLAY_TEXT_ALIGN_LEFT default */
} nt_ui_label_style_t;
_Static_assert(sizeof(nt_ui_label_style_t) <= 32, "nt_ui_label_style_t fits in 32 B");

/* Asserts at entry: ctx, style, text non-NULL; font_id < NT_UI_MAX_FONTS;
 * nt_font_valid(ctx->fonts[font_id]); font_size > 0.
 * data may be NULL (= no layer, no user_data); build with NT_UI_DATA_LAYER /
 * NT_UI_DATA_FULL macros. data is passed through to TEXT render command's
 * userData -- walker reads .layer for batch sort. */
void nt_ui_label(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style);

/* nt_ui_label with font_size override (keeps style static-const; pair with nt_ui_fit_*). */
void nt_ui_label_sized(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style, float font_size_override);

#endif /* NT_UI_LABEL_H */
