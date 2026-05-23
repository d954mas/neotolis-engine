#include "ui/nt_ui_label.h"

#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "ui/nt_ui_internal.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_label(nt_ui_context_t *ctx, const char *text, const nt_ui_label_style_t *style) {
    NT_ASSERT(ctx != NULL && "nt_ui_label: ctx must be non-NULL");
    NT_ASSERT(style != NULL && "nt_ui_label: style must be non-NULL");
    NT_ASSERT(text != NULL && "nt_ui_label: text must be non-NULL (use \"\" for empty)");
    NT_ASSERT(style->font_id < NT_UI_MAX_FONTS && "nt_ui_label: font_id out of registry range");
    NT_ASSERT(nt_font_valid(ctx->fonts[style->font_id]) && "nt_ui_label: font slot empty; call nt_ui_set_font first");
    NT_ASSERT(style->font_size > 0 && "nt_ui_label: font_size must be > 0");

    /* Plan 03 wires CLAY_TEXT here; placeholder unused param. */
    (void)text;
}
