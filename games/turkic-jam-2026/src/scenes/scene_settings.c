/* Settings: language selection (current language highlighted) + reset progress. */

#include "clay.h"

#include "ui/nt_ui.h"
#include "ui/nt_ui_label.h"

#include "game.h"
#include "i18n.h"
#include "save.h"
#include "ui_kit.h"

static void set_lang(i18n_lang_t lang) {
    i18n_set(lang);
    save_set_int("lang", (int)lang);
    save_flush();
}

static tj_btn_variant_t lang_variant(i18n_lang_t lang) { return i18n_get() == lang ? TJ_BTN_PRIMARY : TJ_BTN_SECONDARY; }

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_SETTINGS), &TJ_STYLE_HEADING);

    if (tj_button(g, "set_en", i18n_lang_label(LANG_EN), 320, 70, lang_variant(LANG_EN))) {
        set_lang(LANG_EN);
    }
    if (tj_button(g, "set_ru", i18n_lang_label(LANG_RU), 320, 70, lang_variant(LANG_RU))) {
        set_lang(LANG_RU);
    }
    if (tj_button(g, "set_tr", i18n_lang_label(LANG_TR), 320, 70, lang_variant(LANG_TR))) {
        set_lang(LANG_TR);
    }
    if (tj_button(g, "set_reset", i18n(T_RESET), 320, 70, TJ_BTN_DANGER)) {
        save_set_int("best", 0);
        save_flush();
        g->best = 0;
    }
    if (tj_button(g, "set_back", i18n(T_MENU), 280, 70, TJ_BTN_SECONDARY)) {
        game_goto(g, &SCENE_MENU);
    }
}

const scene_t SCENE_SETTINGS = {
    .name = "settings",
    .on_enter = NULL,
    .on_update = on_update,
    .on_exit = NULL,
};
