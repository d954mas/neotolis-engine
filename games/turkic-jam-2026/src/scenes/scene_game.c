/* The run + the map. Aul in the centre, a ring of path cells around it (each a
 * tile slot, coloured by its tile kind), and the hero marker walking the ring.
 * Hero auto-walks; tiles fire; Силы 0 = death. Numbers from config. */

#include <math.h>
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

#if NT_DEVAPI_ENABLED
#include "devapi/nt_devapi.h"
#endif

static tj_run_t s_run;

// #region map geometry / colors
#define MAP_SIZE 380.0F
#define MAP_RADIUS 150.0F
#define CELL_SIZE 34.0F
#define AUL_SIZE 64.0F
#define HERO_SIZE 22.0F

static Clay_Color cell_color(int tile_idx) {
    if (tile_idx < 0 || tile_idx >= g_config.tile_count) {
        return (Clay_Color){62.0F, 64.0F, 76.0F, 255.0F}; /* empty slot */
    }
    switch (g_config.tiles[tile_idx].kind) {
    case TJ_TILE_SAFE:
        return (Clay_Color){90.0F, 175.0F, 110.0F, 255.0F};
    case TJ_TILE_SUPPORT:
        return (Clay_Color){88.0F, 140.0F, 205.0F, 255.0F};
    case TJ_TILE_CHECK:
        return (Clay_Color){205.0F, 110.0F, 88.0F, 255.0F};
    default:
        return (Clay_Color){120.0F, 120.0F, 130.0F, 255.0F};
    }
}

/* Floating marker anchored to the map centre, offset (x,y) px. */
#define MAP_MARKER(w, h, col, radius, ox, oy, z)                                                                                                                                                       \
    {                                                                                                                                                                                                  \
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}}, .backgroundColor = (col), .cornerRadius = CLAY_CORNER_RADIUS(radius), .floating = {                                        \
            .attachTo = CLAY_ATTACH_TO_PARENT,                                                                                                                                                         \
            .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},                                                                                   \
            .offset = {(ox), (oy)},                                                                                                                                                                    \
            .zIndex = (z),                                                                                                                                                                             \
        }                                                                                                                                                                                              \
    }

static void draw_map(game_ctx_t *g) {
    const int n = g_config.path_cells > 0 ? g_config.path_cells : 1;
    const float step = 6.2831853F / (float)n;
    const float start = -1.5707963F; /* first cell at the top */

    CLAY({.id = CLAY_ID("map"), .layout = {.sizing = {CLAY_SIZING_FIXED(MAP_SIZE), CLAY_SIZING_FIXED(MAP_SIZE)}}}) {
        /* aul in the centre */
        CLAY(MAP_MARKER(AUL_SIZE, AUL_SIZE, ((Clay_Color){180.0F, 150.0F, 110.0F, 255.0F}), AUL_SIZE * 0.32F, 0.0F, 0.0F, 0)) {}

        /* ring of cells (tile slots) */
        for (int i = 0; i < n && i < TJ_MAX_PATH; i++) {
            const float a = start + (step * (float)i);
            const float ox = MAP_RADIUS * cosf(a);
            const float oy = MAP_RADIUS * sinf(a);
            CLAY(MAP_MARKER(CELL_SIZE, CELL_SIZE, cell_color(s_run.tile_at[i]), CELL_SIZE * 0.5F, ox, oy, 0)) {}
        }

        /* hero marker, interpolated between cells */
        float per = g_config.move_seconds_per_cell;
        float frac = (per > 0.0F) ? (s_run.move_t / per) : 0.0F;
        if (frac < 0.0F) {
            frac = 0.0F;
        }
        if (frac > 1.0F) {
            frac = 1.0F;
        }
        const float ah = start + (step * ((float)s_run.cell + frac));
        const float hx = MAP_RADIUS * cosf(ah);
        const float hy = MAP_RADIUS * sinf(ah);
        CLAY(MAP_MARKER(HERO_SIZE, HERO_SIZE, ((Clay_Color){255.0F, 210.0F, 90.0F, 255.0F}), HERO_SIZE * 0.5F, hx, hy, 2)) {}
    }
    (void)g;
}
// #endregion

static void seed_desert(void) {
    int n = g_config.tile_count;
    for (int c = 0; c < g_config.path_cells && c < TJ_MAX_PATH; c++) {
        tj_run_place_tile(&s_run, c, n > 0 ? (c % n) : -1);
    }
}

#if NT_DEVAPI_ENABLED
/* devapi: live run state for bots/tests. */
static int ep_run(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    return snprintf(o, (size_t)cap, "{\"circle\":%d,\"cell\":%d,\"stamina\":%d,\"supplies\":%d,\"wisdom\":%d,\"glory\":%d,\"alive\":%s,\"won\":%s}", s_run.circle, s_run.cell, s_run.stamina,
                    s_run.supplies, s_run.wisdom, s_run.glory, s_run.alive ? "true" : "false", s_run.won ? "true" : "false");
}
#endif

static void on_enter(game_ctx_t *g) {
#if NT_DEVAPI_ENABLED
    static bool s_ep_registered = false;
    if (!s_ep_registered) {
        nt_devapi_register("game.run", ep_run, NULL);
        s_ep_registered = true;
    }
#endif
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

    static char l_circle[64];
    static char l_res[96];
    (void)snprintf(l_circle, sizeof l_circle, "Круг %d/%d", s_run.circle, g_config.laps_to_win);
    (void)snprintf(l_res, sizeof l_res, "Силы %d    Запасы %d    Мудрость %d    Слава %d", s_run.stamina, s_run.supplies, s_run.wisdom, s_run.glory);

    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), l_circle, &TJ_STYLE_HEADING);
    draw_map(g);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), l_res, &TJ_STYLE_BODY);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), s_run.last_event, &TJ_STYLE_HINT);

    if (tj_button(g, "game_give_up", i18n(T_LOSE), 260, 64, TJ_BTN_DANGER)) {
        s_run.alive = false;
    }

    if (!s_run.alive) {
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
