/* Game over: final score + best, retry or back to menu. */

#include <stdio.h>

#include "clay.h"

#include "ui/nt_ui.h"
#include "ui/nt_ui_label.h"

#include "aul.h"
#include "game.h"
#include "i18n.h"
#include "ui_kit.h"

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;

    static char buf[96];
    static char aul[176];
    static char tamga[160];
    (void)snprintf(buf, sizeof buf, "Наследник пал на круге %d", g->score);
    (void)snprintf(aul, sizeof aul, "Аул: Запасы %d   Мудрость %d   Слава %d   (павших: %d)", g_aul.supplies, g_aul.wisdom, g_aul.glory, g_aul.deaths);

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_GAMEOVER), &TJ_STYLE_TITLE);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), buf, &TJ_STYLE_BODY);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), aul, &TJ_STYLE_BODY);
    if (g_aul.tamga_pending) {
        (void)snprintf(tamga, sizeof tamga, "Последняя Тамга осталась на клетке %d (М+%d, С+%d). Наследник подберёт её.", g_aul.tamga_cell, g_aul.tamga_wisdom, g_aul.tamga_glory);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), tamga, &TJ_STYLE_BODY);
    }

    if (tj_button(g, "go_retry", i18n(T_RETRY), 360, 100, TJ_BTN_PRIMARY)) {
        g->score = 0;
        game_goto(g, &SCENE_GAME);
    }
    if (tj_button(g, "go_menu", i18n(T_MENU), 300, 80, TJ_BTN_SECONDARY)) {
        game_goto(g, &SCENE_MENU);
    }
}

const scene_t SCENE_GAMEOVER = {
    .name = "gameover",
    .on_enter = NULL,
    .on_update = on_update,
    .on_exit = NULL,
};
