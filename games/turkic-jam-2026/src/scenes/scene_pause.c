/* Pause: full-screen scene swap (keeps score in game_ctx). P or Resume returns. */

#include "clay.h"

#include "input/nt_input.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_label.h"

#include "game.h"
#include "i18n.h"
#include "ui_kit.h"

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;

    if (nt_input_key_is_pressed(NT_KEY_P)) {
        game_goto(g, &SCENE_GAME);
    }

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_PAUSED), &TJ_STYLE_TITLE);

    if (tj_button(g, "pause_resume", i18n(T_RESUME), 360, 100, TJ_BTN_PRIMARY)) {
        game_goto(g, &SCENE_GAME);
    }
    if (tj_button(g, "pause_menu", i18n(T_MENU), 300, 80, TJ_BTN_DANGER)) {
        game_goto(g, &SCENE_MENU);
    }
}

const scene_t SCENE_PAUSE = {
    .name = "pause",
    .on_enter = NULL,
    .on_update = on_update,
    .on_exit = NULL,
};
