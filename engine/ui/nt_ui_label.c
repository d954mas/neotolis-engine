#include "ui/nt_ui_label.h"

#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "ui/nt_ui_internal.h"

/* Phase 56 ext (descriptor refactor): static descriptor consumed by the
 * inspector. Blue pill (0xFFC88C5A -- R=90,G=140,B=200; preserved from the
 * pre-refactor cdv_widget_color switch). */
const nt_ui_widget_def_t NT_UI_LABEL_DEF = {
    .name = "nt_label",
    .pill_color = 0xFFC88C5AU,
    ._reserved = 0U,
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_label(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style) {
    NT_ASSERT(ctx != NULL && "nt_ui_label: ctx must be non-NULL");
    NT_ASSERT(style != NULL && "nt_ui_label: style must be non-NULL");
    NT_ASSERT(text != NULL && "nt_ui_label: text must be non-NULL (use \"\" for empty)");
    NT_ASSERT(style->font_id < NT_UI_MAX_FONTS && "nt_ui_label: font_id out of registry range");
    NT_ASSERT(nt_font_valid(ctx->fonts[style->font_id]) && "nt_ui_label: font slot empty; call nt_ui_set_font first");
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_label: font_size must be > 0");

    const uint16_t clay_font_size = (uint16_t)(style->font_size + 0.5F);
    Clay_String s = {.length = (int32_t)strlen(text), .chars = text};
    CLAY_TEXT(s, CLAY_TEXT_CONFIG({
                     .userData = (void *)data,
                     .textColor = style->color,
                     .fontId = style->font_id,
                     .fontSize = clay_font_size,
                     .letterSpacing = style->letter_tracking,
                     .lineHeight = style->line_height,
                     .wrapMode = (Clay_TextElementConfigWrapMode)style->wrap_mode,
                     .textAlignment = (Clay_TextAlignment)style->align,
                 }));

    /* Phase 56 ext (CHUNK B fix): tag the just-emitted CLAY_TEXT leaf so
     * nt_ui_inspector shows "nt_label" next to it in the element tree.
     * current_open_element_id() returns the PARENT's id here -- Clay's
     * Clay__OpenTextElement appends the text element but does NOT push it
     * onto openLayoutElementStack (clay.h:1991-2023). Use
     * last_emitted_element_id() to read layoutElements[length-1] = the leaf
     * we just emitted. id 0 (Clay's maxElementsExceeded early-out) is
     * silently dropped by widget_register. */
    nt_ui_widget_register(ctx, nt_ui_internal_last_emitted_element_id(), &NT_UI_LABEL_DEF, NULL);
}

void nt_ui_label_sized(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style, float font_size_override) {
    NT_ASSERT(ctx != NULL && "nt_ui_label_sized: ctx must be non-NULL");
    NT_ASSERT(style != NULL && "nt_ui_label_sized: style must be non-NULL");
    NT_ASSERT(font_size_override > 0.0F && "nt_ui_label_sized: font_size_override must be > 0");
    nt_ui_label_style_t local = *style;
    local.font_size = font_size_override;
    nt_ui_label(ctx, data, text, &local);
}
