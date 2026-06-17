#include "ui/nt_ui_tooltip.h"

/* RED scaffold — real implementation lands in the GREEN step. */

const nt_ui_widget_def_t NT_UI_TOOLTIP_DEF = {
    .name = "nt_tooltip",
    .pill_color = 0xFF80C0FFU,
    ._reserved = 0U,
};

nt_ui_tooltip_style_t nt_ui_tooltip_style_defaults(void) {
    return (nt_ui_tooltip_style_t){0};
}

bool nt_ui_tooltip(nt_ui_context_t *ctx, uint32_t target_id, const char *content, const nt_ui_tooltip_style_t *style) {
    (void)ctx;
    (void)target_id;
    (void)content;
    (void)style;
    return false;
}

#ifdef NT_TEST_ACCESS
uint32_t nt_ui_tooltip_test_timer_id(uint32_t target_id) {
    (void)target_id;
    return 0U;
}
float nt_ui_tooltip_test_hover_secs(nt_ui_context_t *ctx, uint32_t target_id) {
    (void)ctx;
    (void)target_id;
    return 0.0F;
}
#endif
