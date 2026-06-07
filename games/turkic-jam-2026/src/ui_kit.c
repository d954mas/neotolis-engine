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
    .color = {232.0F, 214.0F, 176.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

const nt_ui_label_style_t TJ_STYLE_BODY = {
    .font_id = 0,
    .font_size = 26,
    .color = {218.0F, 202.0F, 174.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

const nt_ui_label_style_t TJ_STYLE_HINT = {
    .font_id = 0,
    .font_size = 18,
    .color = {176.0F, 160.0F, 135.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

static const nt_ui_label_style_t s_btn_label = {
    .font_id = 0,
    .font_size = 28,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};
// #endregion

bool tj_button(game_ctx_t *g, const char *id_str, const char *text, int w, int h, tj_btn_variant_t variant) {
    uint32_t tint = 0xFFFFFFFF;
    if (variant == TJ_BTN_SECONDARY) {
        tint = 0xFF3A5A8AU;
    } else if (variant == TJ_BTN_DANGER) {
        tint = 0xFF283A9CU;
    }

    /* Only idle.atlas is set; other states inherit it (atlas.id == 0). */
    const nt_ui_button_style_t style = {
        .idle = {.atlas = g->atlas, .bg_region = g->ui_button_dark_64, .bg_tint = tint, .scale = 1.0F, .opacity = 1.0F},
        .hover = {.bg_region = g->ui_button_dark_64, .bg_tint = tint, .scale = 1.06F, .opacity = 1.0F},
        .pressed = {.bg_region = g->ui_button_dark_64, .bg_tint = tint, .scale = 0.95F, .offset_y = 3.0F, .opacity = 1.0F},
        .disabled = {.bg_region = g->ui_button_dark_64, .bg_tint = tint, .scale = 1.0F, .opacity = 0.4F},
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

float tj_slider(game_ctx_t *g, const char *id_str, float value) {
    if (value < 0.0F) {
        value = 0.0F;
    } else if (value > 1.0F) {
        value = 1.0F;
    }

    const float track_w = 440.0F;
    const float track_h = 34.0F;
    const uint32_t id = nt_ui_id(id_str);

    /* Drag: map this-frame pointer x into the track's previous-frame bbox. */
    const nt_ui_interaction_t in = nt_ui_step_interaction(g->ui, id);
    const nt_ui_bbox_t bb = nt_ui_get_bbox(g->ui, id);
    if (in.pressed && bb.found && bb.width > 1.0F) {
        float t = (in.pos[0] - bb.x) / bb.width;
        if (t < 0.0F) {
            t = 0.0F;
        } else if (t > 1.0F) {
            t = 1.0F;
        }
        value = t;
    }

    float fill_w = value * track_w;
    if (fill_w < track_h) {
        fill_w = track_h; /* keep the rounded fill visible near zero */
    }

    CLAY({.id = (Clay_ElementId){.id = id},
          .layout = {.sizing = {CLAY_SIZING_FIXED(track_w), CLAY_SIZING_FIXED(track_h)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {44.0F, 36.0F, 27.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(track_h * 0.5F),
          .border = {.color = {104.0F, 76.0F, 42.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(fill_w), CLAY_SIZING_FIXED(track_h)}}, .backgroundColor = {214.0F, 170.0F, 80.0F, 255.0F}, .cornerRadius = CLAY_CORNER_RADIUS(track_h * 0.5F)}) {}
    }
    return value;
}

bool tj_hold_button(game_ctx_t *g, const char *id_str, const char *text, float dt, float hold_seconds, float *accum) {
    const uint32_t id = nt_ui_id(id_str);
    const nt_ui_interaction_t in = nt_ui_step_interaction(g->ui, id);

    bool done = false;
    if (in.pressed) {
        *accum += dt;
        if (*accum >= hold_seconds) {
            done = true;
            *accum = 0.0F;
        }
    } else {
        *accum = 0.0F; /* released before full -> cancel */
    }

    float prog = (hold_seconds > 0.0F) ? (*accum / hold_seconds) : 0.0F;
    if (prog > 1.0F) {
        prog = 1.0F;
    }

    const float w = 440.0F;
    const float h = 64.0F;
    const float fill_w = prog * w;

    CLAY({.id = (Clay_ElementId){.id = id},
          .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {72.0F, 28.0F, 28.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(12.0F),
          .border = {.color = {150.0F, 50.0F, 44.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        /* Floating so the fill grows under the centered label without shifting it.
           Layer sort keeps the TEXT label above this layer-0 fill. */
        if (fill_w > 1.0F) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(fill_w), CLAY_SIZING_FIXED(h)}},
                  .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_CENTER}, .zIndex = 1},
                  .backgroundColor = {200.0F, 60.0F, 48.0F, 255.0F},
                  .cornerRadius = CLAY_CORNER_RADIUS(12.0F)}) {}
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_btn_label);
    }
    return done;
}
