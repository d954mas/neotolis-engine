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
    const i18n_lang_t lang = i18n_get();
    if (lang == LANG_RU) {
        (void)snprintf(buf, sizeof buf, "Наследник пал на круге %d", g->score);
        (void)snprintf(aul, sizeof aul, "Аул: Запасы %d   Мудрость %d   Слава %d   (павших: %d)", g_aul.supplies, g_aul.wisdom, g_aul.glory, g_aul.deaths);
    } else if (lang == LANG_TR) {
        (void)snprintf(buf, sizeof buf, "Varis %d. döngüde düştü", g->score);
        (void)snprintf(aul, sizeof aul, "Oba: Erzak %d   Bilgelik %d   Ün %d   (düşen: %d)", g_aul.supplies, g_aul.wisdom, g_aul.glory, g_aul.deaths);
    } else {
        (void)snprintf(buf, sizeof buf, "The heir fell on circle %d", g->score);
        (void)snprintf(aul, sizeof aul, "Aul: Supplies %d   Wisdom %d   Glory %d   (fallen: %d)", g_aul.supplies, g_aul.wisdom, g_aul.glory, g_aul.deaths);
    }

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_GAMEOVER), &TJ_STYLE_TITLE);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), buf, &TJ_STYLE_BODY);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), aul, &TJ_STYLE_BODY);
    if (g_aul.tamga_pending) {
        if (lang == LANG_RU) {
            (void)snprintf(tamga, sizeof tamga, "Последняя Тамга осталась на клетке %d (М+%d, С+%d). Наследник подберёт её.", g_aul.tamga_cell, g_aul.tamga_wisdom, g_aul.tamga_glory);
        } else if (lang == LANG_TR) {
            (void)snprintf(tamga, sizeof tamga, "Son Tamga %d. hücrede kaldı (B+%d, Ü+%d). Sonraki varis onu alacak.", g_aul.tamga_cell, g_aul.tamga_wisdom, g_aul.tamga_glory);
        } else {
            (void)snprintf(tamga, sizeof tamga, "The Last Tamga remains on cell %d (W+%d, G+%d). The next heir will carry it.", g_aul.tamga_cell, g_aul.tamga_wisdom, g_aul.tamga_glory);
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), tamga, &TJ_STYLE_BODY);
    }

    if (tj_button(g, "go_retry", i18n(T_RETRY), 360, 100, TJ_BTN_PRIMARY)) {
        g->score = 0;
        game_goto(g, &SCENE_HEIR_SELECT); /* new heir -> pick an archetype again */
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
