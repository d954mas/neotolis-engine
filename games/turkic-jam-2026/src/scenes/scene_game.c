/* Gameplay placeholder: a tappable score with a persisted best, plus BACK.
 * Stands in for whatever the jam game becomes; proves scene + save + i18n. */

#include <stdio.h>

#include "clay.h"

#include "ui/nt_ui.h"
#include "ui/nt_ui_label.h"

#include "game.h"
#include "i18n.h"
#include "save.h"
#include "ui_kit.h"

static int s_score;
static int s_best;

static void on_enter(game_ctx_t *g) {
    (void)g;
    s_score = 0;
    s_best = save_get_int("best", 0);
}

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;

    /* Static so the pointer stays valid until nt_ui_end consumes it. */
    static char score_buf[96];
    (void)snprintf(score_buf, sizeof score_buf, "%s: %d   (best %d)", i18n(T_SCORE), s_score, s_best);

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_GAME_TITLE), &TJ_STYLE_HEADING);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), score_buf, &TJ_STYLE_BODY);

    if (tj_button(g, "game_tap", i18n(T_TAP), 360, 110, TJ_BTN_PRIMARY)) {
        s_score++;
        if (s_score > s_best) {
            s_best = s_score;
            save_set_int("best", s_best);
            save_flush();
        }
    }

    if (tj_button(g, "game_back", i18n(T_BACK), 280, 80, TJ_BTN_DANGER)) {
        game_goto(g, &SCENE_MENU);
    }
}

const scene_t SCENE_GAME = {
    .name = "game",
    .on_enter = on_enter,
    .on_update = on_update,
    .on_exit = NULL,
};
