#include "ui_kit.h"

#include "clay.h"

#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_label.h"

// #region label styles
const nt_ui_label_style_t TJ_STYLE_TITLE = {
    .font_id = 0,
    .font_size = 72,
    .color = {255.0F, 214.0F, 102.0F, 255.0F}, /* warm gold */
    .align = CLAY_TEXT_ALIGN_CENTER,
};

const nt_ui_label_style_t TJ_STYLE_HEADING = {
    .font_id = 0,
    .font_size = 44,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

const nt_ui_label_style_t TJ_STYLE_BODY = {
    .font_id = 0,
    .font_size = 26,
    .color = {200.0F, 206.0F, 218.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

const nt_ui_label_style_t TJ_STYLE_HINT = {
    .font_id = 0,
    .font_size = 18,
    .color = {150.0F, 158.0F, 172.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

static const nt_ui_label_style_t s_btn_label = {
    .font_id = 0,
    .font_size = 36,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};
// #endregion

bool tj_button(game_ctx_t *g, const char *id_str, const char *text, int w, int h, tj_btn_variant_t variant) {
    uint32_t region = g->btn_blue;
    if (variant == TJ_BTN_SECONDARY) {
        region = g->btn_green;
    } else if (variant == TJ_BTN_DANGER) {
        region = g->btn_red;
    }

    /* Only idle.atlas is set; other states inherit it (atlas.id == 0). */
    const nt_ui_button_style_t style = {
        .idle = {.atlas = g->atlas, .bg_region = region, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
        .hover = {.bg_region = region, .bg_tint = 0xFFFFFFFF, .scale = 1.06F, .opacity = 1.0F},
        .pressed = {.bg_region = region, .bg_tint = 0xFFFFFFFF, .scale = 0.95F, .offset_y = 3.0F, .opacity = 1.0F},
        .disabled = {.bg_region = region, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 0.4F},
        .transition_speed = 12.0F,
        .hit_padding_lrtb = {16, 16, 16, 16},
        .slice9_scale = 1.0F,
    };

    const Clay_ElementDeclaration decl = {
        .layout =
            {
                .sizing = {CLAY_SIZING_FIXED((float)w), CLAY_SIZING_FIXED((float)h)},
                .padding = CLAY_PADDING_ALL(8),
                .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
            },
    };

    const uint32_t id = nt_ui_id(id_str);
    nt_ui_button_begin(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), id, &style, &decl, true);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_btn_label);
    return nt_ui_button_end(g->ui);
}
