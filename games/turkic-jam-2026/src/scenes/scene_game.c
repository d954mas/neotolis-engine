/* Run scene — thin orchestrator. Drives the sim, calls the view systems, and
 * handles input + flow. No rendering or sim logic lives here (see view.c / sim.c). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"

#include "input/nt_input.h"
#include "ui/nt_ui.h"

#include "aul.h"
#include "config.h"
#include "game.h"
#include "i18n.h"
#include "journal.h"
#include "sim.h"
#include "ui/nt_ui_label.h"
#include "ui_kit.h"
#include "view.h"

#if NT_DEVAPI_ENABLED
#include "devapi/nt_devapi.h"
#endif

static tj_run_t s_run;

#if NT_DEVAPI_ENABLED
/* devapi: live run state for bots/tests. */
static int ep_run(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    return snprintf(o, (size_t)cap, "{\"circle\":%d,\"cell\":%d,\"path_cells\":%d,\"hand\":%d,\"stamina\":%d,\"supplies\":%d,\"wisdom\":%d,\"glory\":%d,\"alive\":%s,\"won\":%s}", s_run.circle,
                    s_run.cell, s_run.path_cells, s_run.hand, s_run.stamina, s_run.supplies, s_run.wisdom, s_run.glory, s_run.alive ? "true" : "false", s_run.won ? "true" : "false");
}

/* devapi: recent event-log lines (newest last), for reading the run narrative. */
static int ep_log(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    const int n = tj_journal_count();
    const int shown = n < 12 ? n : 12;
    int len = snprintf(o, (size_t)cap, "[");
    for (int k = shown - 1; k >= 0; k--) {
        const int rem = cap - len;
        if (rem <= 2) {
            break;
        }
        const char *t = tj_journal_get(k, NULL);
        len += snprintf(o + len, (size_t)rem, "%s\"%s\"", (k == shown - 1) ? "" : ",", t ? t : "");
    }
    if (cap - len > 1) {
        len += snprintf(o + len, (size_t)(cap - len), "]");
    }
    return len;
}

/* devapi: place the held card into a roadside slot. "game.place slot=<i>" */
static int ep_place(int c, char **v, char *o, int cap, void *u) {
    (void)u;
    int slot = -1;
    for (int i = 0; i < c; i++) {
        if (strncmp(v[i], "slot=", 5) == 0) {
            slot = (int)strtol(v[i] + 5, NULL, 10);
        }
    }
    bool ok = tj_run_place_roadside(&s_run, slot);
    return snprintf(o, (size_t)cap, "{\"placed\":%s,\"slot\":%d,\"hand\":%d}", ok ? "true" : "false", slot, s_run.hand);
}
#endif

static void on_enter(game_ctx_t *g) {
#if NT_DEVAPI_ENABLED
    static bool s_ep_registered = false;
    if (!s_ep_registered) {
        nt_devapi_register("game.run", ep_run, NULL);
        nt_devapi_register("game.log", ep_log, NULL);
        nt_devapi_register("game.place", ep_place, NULL);
        s_ep_registered = true;
    }
#endif
    if (g->prev == &SCENE_PAUSE) {
        return; /* resume: keep the run in progress */
    }
    tj_run_start(&s_run, 0); /* TODO: heir selection. Desert is auto-rolled per circle in sim. */
}

static void on_update(game_ctx_t *g, float dt) {
    if (nt_input_key_is_pressed(NT_KEY_P)) {
        game_goto(g, &SCENE_PAUSE);
    }

    tj_run_tick(&s_run, dt);

    tj_view_hud(g, &s_run);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 28, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        tj_view_map(g, &s_run);
        tj_view_journal(g, 7);
    }

    if (tj_button(g, "game_give_up", i18n(T_LOSE), 240, 58, TJ_BTN_DANGER)) {
        s_run.alive = false;
    }
    if (s_run.hand >= 0) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "В руке карта — кликни свободный слот, чтобы её поставить", &TJ_STYLE_HINT);
    }

    if (!s_run.alive) {
        tj_aul_add_from_run(s_run.supplies, s_run.wisdom, s_run.glory); /* bank into the aul (meta) */
        g->score = s_run.circle;
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
