/* Gameplay placeholder: a tappable score (with screen-shake juice) and a
 * persisted best. Stands in for the real game; proves scene + save + i18n +
 * juice. Score lives in game_ctx so Pause can swap scenes without losing it. */

#include <stdio.h>

#include "clay.h"

#include "input/nt_input.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_label.h"

#include "game.h"
#include "i18n.h"
#include "save.h"
#include "ui_kit.h"

static void on_enter(game_ctx_t *g) {
    /* score is reset by the entry point (menu START / game-over RETRY) so that
     * returning from Pause keeps the run going. */
    g->best = save_get_int("best", 0);
}

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;

    if (nt_input_key_is_pressed(NT_KEY_P)) {
        game_goto(g, &SCENE_PAUSE);
    }

    /* Static so the pointer stays valid until nt_ui_end consumes it. */
    static char score_buf[96];
    (void)snprintf(score_buf, sizeof score_buf, "%s: %d   (best %d)", i18n(T_SCORE), g->score, g->best);

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_GAME_TITLE), &TJ_STYLE_HEADING);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), score_buf, &TJ_STYLE_BODY);

    if (tj_button(g, "game_tap", i18n(T_TAP), 360, 110, TJ_BTN_PRIMARY)) {
        g->score++;
        tj_shake_add(&g->shake, 0.45F);
        if (g->score > g->best) {
            g->best = g->score;
            save_set_int("best", g->best);
            save_flush();
        }
    }

    if (tj_button(g, "game_lose", i18n(T_LOSE), 280, 80, TJ_BTN_DANGER)) {
        game_goto(g, &SCENE_GAMEOVER);
    }

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "P = pause", &TJ_STYLE_HINT);
}

const scene_t SCENE_GAME = {
    .name = "game",
    .on_enter = on_enter,
    .on_update = on_update,
    .on_exit = NULL,
};
