/* The run: hero auto-walks the ring, tiles fire, Силы drop, death -> game over.
 * Numbers come from config; desert is auto-seeded for now (player tile placement
 * via cards comes later). Pause swaps scene but keeps the run (see g->prev). */

#include <stdio.h>

#include "clay.h"

#include "input/nt_input.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_label.h"

#include "config.h"
#include "game.h"
#include "i18n.h"
#include "sim.h"
#include "ui_kit.h"

static tj_run_t s_run;

static void seed_desert(void) {
    int n = g_config.tile_count;
    for (int c = 0; c < g_config.path_cells && c < TJ_MAX_PATH; c++) {
        tj_run_place_tile(&s_run, c, n > 0 ? (c % n) : -1);
    }
}

static void on_enter(game_ctx_t *g) {
    if (g->prev == &SCENE_PAUSE) {
        return; /* resume: keep the run in progress */
    }
    tj_run_start(&s_run, 0); /* TODO: heir selection */
    seed_desert();           /* TODO: player places tiles via cards */
}

static void on_update(game_ctx_t *g, float dt) {
    if (nt_input_key_is_pressed(NT_KEY_P)) {
        game_goto(g, &SCENE_PAUSE);
    }

    tj_run_tick(&s_run, dt);

    static char l_circle[96];
    static char l_hero[96];
    static char l_res[96];
    (void)snprintf(l_circle, sizeof l_circle, "Круг %d/%d    клетка %d/%d", s_run.circle, g_config.laps_to_win, s_run.cell + 1, g_config.path_cells);
    (void)snprintf(l_hero, sizeof l_hero, "Силы %d    Body %d  Mind %d  Spirit %d", s_run.stamina, s_run.body, s_run.mind, s_run.spirit);
    (void)snprintf(l_res, sizeof l_res, "Запасы %d    Мудрость %d    Слава %d", s_run.supplies, s_run.wisdom, s_run.glory);

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), l_circle, &TJ_STYLE_HEADING);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), l_hero, &TJ_STYLE_BODY);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), l_res, &TJ_STYLE_BODY);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), s_run.last_event, &TJ_STYLE_BODY);

    if (tj_button(g, "game_give_up", i18n(T_LOSE), 280, 70, TJ_BTN_DANGER)) {
        s_run.alive = false;
    }
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "P = пауза", &TJ_STYLE_HINT);

    if (!s_run.alive) {
        g->score = s_run.circle; /* circle reached, shown on game over */
        if (g->score > g->best) {
            g->best = g->score;
        }
        game_goto(g, &SCENE_GAMEOVER);
    } else if (s_run.won) {
        game_goto(g, &SCENE_MENU);
    }
}

const scene_t SCENE_GAME = {
    .name = "game",
    .on_enter = on_enter,
    .on_update = on_update,
    .on_exit = NULL,
};
