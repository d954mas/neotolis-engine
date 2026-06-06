/* Main menu: title, START, language toggle. */

#include "clay.h"

#include "atlas/nt_atlas.h"
#include "input/nt_input.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_label.h"

#include "game.h"
#include "i18n.h"
#include "save.h"
#include "ui_kit.h"

static void cycle_language(void) {
    i18n_cycle();
    save_set_int("lang", (int)i18n_get());
    save_flush();
}

static bool has_region(uint32_t region) { return region != 0U && region != NT_ATLAS_INVALID_REGION; }

static void menu_image(game_ctx_t *g, uint32_t region, float w, float h) {
    if (!has_region(region)) {
        return;
    }
    const nt_ui_image_style_t img = nt_ui_image_style_defaults();
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}},
    };
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
}

static void menu_art_strip(game_ctx_t *g) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(460), CLAY_SIZING_FIXED(92)},
                     .padding = CLAY_PADDING_ALL(8),
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 16,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {42.0F, 34.0F, 25.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
          .border = {.color = {104.0F, 76.0F, 42.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        menu_image(g, g->hero_wayfarer_panel, 60.0F, 78.0F);
        menu_image(g, g->aul_fire_01, 64.0F, 64.0F);
        menu_image(g, g->tile_saxaul, 58.0F, 58.0F);
        menu_image(g, g->card_art_wolf_track_64, 58.0F, 58.0F);
        menu_image(g, g->icon_last_tamga_32, 42.0F, 42.0F);
    }
}

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;

    if (nt_input_key_is_pressed(NT_KEY_L)) {
        cycle_language();
    }

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_TITLE), &TJ_STYLE_TITLE);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_SUBTITLE), &TJ_STYLE_BODY);
    menu_art_strip(g);

    if (tj_button(g, "menu_start", i18n(T_START), 460, 96, TJ_BTN_PRIMARY)) {
        g->score = 0;
        game_goto(g, &SCENE_GAME);
    }

    if (tj_button(g, "menu_settings", i18n(T_SETTINGS), 340, 68, TJ_BTN_SECONDARY)) {
        game_goto(g, &SCENE_SETTINGS);
    }

    if (tj_button(g, "menu_lang", i18n_lang_label(i18n_get()), 340, 68, TJ_BTN_SECONDARY)) {
        cycle_language();
    }

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_HINT), &TJ_STYLE_HINT);
}

const scene_t SCENE_MENU = {
    .name = "menu",
    .on_enter = NULL,
    .on_update = on_update,
    .on_exit = NULL,
};
