/* Main menu: title, START, language toggle. */

#include "clay.h"

#include "input/nt_input.h"
#include "ui/nt_ui.h"
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

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;

    if (nt_input_key_is_pressed(NT_KEY_L)) {
        cycle_language();
    }

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_TITLE), &TJ_STYLE_TITLE);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_SUBTITLE), &TJ_STYLE_BODY);

    if (tj_button(g, "menu_start", i18n(T_START), 460, 120, TJ_BTN_PRIMARY)) {
        g->score = 0;
        game_goto(g, &SCENE_GAME);
    }

    if (tj_button(g, "menu_settings", i18n(T_SETTINGS), 340, 80, TJ_BTN_SECONDARY)) {
        game_goto(g, &SCENE_SETTINGS);
    }

    if (tj_button(g, "menu_lang", i18n_lang_label(i18n_get()), 340, 80, TJ_BTN_SECONDARY)) {
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
