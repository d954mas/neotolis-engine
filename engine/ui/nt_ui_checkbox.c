#include "ui/nt_ui_checkbox.h"

const nt_ui_widget_def_t NT_UI_CHECKBOX_DEF = {
    .name = "nt_checkbox",
    .pill_color = 0xFF70B0D0U,
    ._reserved = 0U,
};

/* Skeleton: real cb_core + nt_ui_checkbox body lands in Task 3 (GREEN). */
bool nt_ui_checkbox(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const char *label, bool *value, const nt_ui_checkbox_style_t *style, const Clay_ElementDeclaration *decl,
                    bool enabled) {
    (void)ctx;
    (void)data;
    (void)id;
    (void)label;
    (void)value;
    (void)style;
    (void)decl;
    (void)enabled;
    return false;
}
