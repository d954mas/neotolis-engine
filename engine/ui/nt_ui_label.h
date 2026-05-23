#ifndef NT_UI_LABEL_H
#define NT_UI_LABEL_H

/* Stateless text widget. Style is passed by const-pointer; engine asserts
 * style != NULL, font_id < NT_UI_MAX_FONTS, font slot bound, font_size > 0.
 * text must live until nt_ui_end completes (Clay stores .chars by pointer). */

#include <stdint.h>

#include "clay.h"

/* Forward decl avoids include cycle with ui/nt_ui.h umbrella re-export
 * (C11+ permits identical typedef redeclaration; misc-header-include-cycle
 * would fire if we included ui/nt_ui.h here). */
typedef struct nt_ui_context nt_ui_context_t;

typedef struct {
    uint16_t font_id;         /* index into ctx->fonts[]; asserted < NT_UI_MAX_FONTS */
    uint16_t font_size;       /* px; asserted > 0 */
    Clay_Color color;         /* 4 floats, 0..255 (Clay convention) */
    uint16_t line_height;     /* px; 0 = auto from font metrics */
    uint16_t letter_tracking; /* px; maps to Clay letterSpacing (uint16_t — D-53-07 Drift-1 corrected) */
    uint8_t wrap_mode;        /* Clay_TextElementConfigWrapMode value; 0 = CLAY_TEXT_WRAP_WORDS default */
    uint8_t align;            /* Clay_TextAlignment value; 0 = CLAY_TEXT_ALIGN_LEFT default */
} nt_ui_label_style_t;
/* Layout: 2+2+16+2+2+1+1 = 26 B → padded to 32 B (alignof Clay_Color = 4). */
_Static_assert(sizeof(nt_ui_label_style_t) <= 32, "nt_ui_label_style_t fits in 32 B");

/* Asserts at entry: ctx, style, text non-NULL; font_id < NT_UI_MAX_FONTS;
 * nt_font_valid(ctx->fonts[font_id]); font_size > 0. */
void nt_ui_label(nt_ui_context_t *ctx, const char *text, const nt_ui_label_style_t *style);

#endif /* NT_UI_LABEL_H */
