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

    /* Aul meta: spend banked supplies on permanent upgrades before the next run. */
    static char u0[56];
    static char u1[56];
    static char u2[56];
    static char u3[56];
    (void)snprintf(u0, sizeof u0, "+Сила  ур.%d  (%d)", g_aul.up_force, tj_aul_upgrade_cost(0));
    (void)snprintf(u1, sizeof u1, "+Скорость  ур.%d  (%d)", g_aul.up_speed, tj_aul_upgrade_cost(1));
    (void)snprintf(u2, sizeof u2, "+Выносливость  ур.%d  (%d)", g_aul.up_vigor, tj_aul_upgrade_cost(2));
    (void)snprintf(u3, sizeof u3, "+Наследие  ур.%d  (%d)", g_aul.up_keep, tj_aul_upgrade_cost(3));
    if (tj_button(g, "up_f", u0, 360, 54, TJ_BTN_SECONDARY)) {
        tj_aul_upgrade(0);
    }
    if (tj_button(g, "up_s", u1, 360, 54, TJ_BTN_SECONDARY)) {
        tj_aul_upgrade(1);
    }
    if (tj_button(g, "up_v", u2, 360, 54, TJ_BTN_SECONDARY)) {
        tj_aul_upgrade(2);
    }
    if (tj_button(g, "up_k", u3, 360, 54, TJ_BTN_SECONDARY)) {
        tj_aul_upgrade(3);
    }

    if (tj_button(g, "go_retry", i18n(T_RETRY), 360, 100, TJ_BTN_PRIMARY)) {
        g->score = 0;
        game_goto(g, &SCENE_GAME); /* one champion of the clan -> straight back into a run */
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
