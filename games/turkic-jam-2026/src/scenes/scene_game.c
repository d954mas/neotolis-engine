/* Run scene — thin orchestrator. Drives the sim, calls the view systems, and
 * handles input + flow. No rendering or sim logic lives here (see view.c / sim.c). */

#include <math.h>
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
    return snprintf(o, (size_t)cap,
                    "{\"circle\":%d,\"day\":%d,\"cell\":%d,\"path_cells\":%d,\"phase\":%d,\"packs\":%d,\"pack_open\":%s,\"hand\":%d,\"tamga_cell\":%d,\"stamina\":%d,\"supplies\":%d,\"wisdom\":%d,"
                    "\"glory\":%d,\"alive\":%s,\"won\":%s}",
                    s_run.circle, s_run.day, s_run.cell, s_run.path_cells, (int)s_run.phase, s_run.packs, s_run.pack_open ? "true" : "false", s_run.hand, s_run.tamga_cell, s_run.stamina,
                    s_run.supplies, s_run.wisdom, s_run.glory, s_run.alive ? "true" : "false", s_run.won ? "true" : "false");
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

/* devapi: dump the generated loop geometry (cells + slots) for tests/bots. */
static int ep_path(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    int len = snprintf(o, (size_t)cap, "{\"cols\":%d,\"rows\":%d,\"aul\":[%d,%d,%d,%d],\"cells\":[", s_run.grid_cols, s_run.grid_rows, s_run.aul_x0, s_run.aul_y0, s_run.aul_w, s_run.aul_h);
    for (int i = 0; i < s_run.path_cells && len < cap - 24; i++) {
        len += snprintf(o + len, (size_t)(cap - len), "%s[%d,%d]", i ? "," : "", s_run.path_gx[i], s_run.path_gy[i]);
    }
    len += snprintf(o + len, (size_t)(cap - len), "],\"slots\":[");
    for (int i = 0; i < s_run.build_count && len < cap - 24; i++) {
        len += snprintf(o + len, (size_t)(cap - len), "%s[%d,%d]", i ? "," : "", s_run.build_gx[i], s_run.build_gy[i]);
    }
    len += snprintf(o + len, (size_t)(cap - len), "],\"road_ev\":[");
    for (int i = 0; i < s_run.path_cells && len < cap - 12; i++) {
        len += snprintf(o + len, (size_t)(cap - len), "%s%d", i ? "," : "", s_run.tile_at[i]);
    }
    len += snprintf(o + len, (size_t)(cap - len), "],\"field\":[");
    bool ffirst = true;
    for (int i = 0; i < s_run.grid_cols * s_run.grid_rows && len < cap - 24; i++) {
        if (s_run.field_tile[i] < 0) {
            continue;
        }
        len += snprintf(o + len, (size_t)(cap - len), "%s[%d,%d,%d]", ffirst ? "" : ",", i % s_run.grid_cols, i / s_run.grid_cols, s_run.field_tile[i]);
        ffirst = false;
    }
    len += snprintf(o + len, (size_t)(cap - len), "],\"global\":[");
    for (int i = 0; i < s_run.global_count && len < cap - 24; i++) {
        len += snprintf(o + len, (size_t)(cap - len), "%s[%d,%d,%d]", i ? "," : "", s_run.global_gx[i], s_run.global_gy[i], s_run.global_tile[i]);
    }
    len += snprintf(o + len, (size_t)(cap - len), "]}");
    return len;
}

/* devapi: open the front reward pack (so a card can be chosen). */
static int ep_openpack(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    tj_run_open_pack(&s_run);
    return snprintf(o, (size_t)cap, "{\"pack_open\":%s,\"packs\":%d}", s_run.pack_open ? "true" : "false", s_run.packs);
}

/* devapi: pick a card from the opened pack. "game.choose idx=<0..2>" */
static int ep_choose(int c, char **v, char *o, int cap, void *u) {
    (void)u;
    int idx = -1;
    for (int i = 0; i < c; i++) {
        if (strncmp(v[i], "idx=", 4) == 0) {
            idx = (int)strtol(v[i] + 4, NULL, 10);
        }
    }
    tj_run_choose_card(&s_run, idx);
    return snprintf(o, (size_t)cap, "{\"packs\":%d,\"hand\":%d}", s_run.packs, s_run.hand);
}

/* devapi: end the run now (for testing death -> aul -> next heir). */
static int ep_kill(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    s_run.alive = false;
    return snprintf(o, (size_t)cap, "{\"alive\":false}");
}

/* devapi: place the held card into a field cell. "game.place gx=<x> gy=<y>" */
static int ep_place(int c, char **v, char *o, int cap, void *u) {
    (void)u;
    int gx = -1;
    int gy = -1;
    for (int i = 0; i < c; i++) {
        if (strncmp(v[i], "gx=", 3) == 0) {
            gx = (int)strtol(v[i] + 3, NULL, 10);
        } else if (strncmp(v[i], "gy=", 3) == 0) {
            gy = (int)strtol(v[i] + 3, NULL, 10);
        }
    }
    const bool ok = tj_run_place_field(&s_run, gx, gy);
    return snprintf(o, (size_t)cap, "{\"placed\":%s,\"gx\":%d,\"gy\":%d,\"hand\":%d}", ok ? "true" : "false", gx, gy, s_run.hand);
}
#endif

static void on_enter(game_ctx_t *g) {
#if NT_DEVAPI_ENABLED
    static bool s_ep_registered = false;
    if (!s_ep_registered) {
        nt_devapi_register("game.run", ep_run, NULL);
        nt_devapi_register("game.log", ep_log, NULL);
        nt_devapi_register("game.path", ep_path, NULL);
        nt_devapi_register("game.place", ep_place, NULL);
        nt_devapi_register("game.openpack", ep_openpack, NULL);
        nt_devapi_register("game.choose", ep_choose, NULL);
        nt_devapi_register("game.kill", ep_kill, NULL);
        s_ep_registered = true;
    }
#endif
    g->run = &s_run; /* expose the run to the world sprite pass (tj_view_world in main.c) */
    if (g->prev == &SCENE_PAUSE) {
        return; /* resume: keep the run in progress */
    }
    tj_run_start(&s_run, g->chosen_heir); /* archetype chosen on the heir-select screen */
}

static void on_exit(game_ctx_t *g) { g->run = NULL; }

/* Keyboard camera pan (arrows / WASD). */
static void camera_keys(float dt) {
    const float pan = 360.0F * dt;
    if (nt_input_key_is_down(NT_KEY_ARROW_LEFT) || nt_input_key_is_down(NT_KEY_A)) {
        tj_view_world_pan(pan, 0.0F);
    }
    if (nt_input_key_is_down(NT_KEY_ARROW_RIGHT) || nt_input_key_is_down(NT_KEY_D)) {
        tj_view_world_pan(-pan, 0.0F);
    }
    if (nt_input_key_is_down(NT_KEY_ARROW_UP) || nt_input_key_is_down(NT_KEY_W)) {
        tj_view_world_pan(0.0F, pan);
    }
    if (nt_input_key_is_down(NT_KEY_ARROW_DOWN) || nt_input_key_is_down(NT_KEY_S)) {
        tj_view_world_pan(0.0F, -pan);
    }
}

/* Mouse on the map: a DRAG (press + move) scrolls the camera; a CLICK (press +
 * release without moving) places the held card on release. The reward modal owns
 * input while open, so the map is inert then. */
static void handle_map_input(game_ctx_t *g, float dt) {
    static bool press_active = false;
    static bool dragged = false;
    static float press_x = 0.0F;
    static float press_y = 0.0F;
    static float last_x = 0.0F;
    static float last_y = 0.0F;
    if (s_run.pack_open) {
        press_active = false;
        return;
    }
    camera_keys(dt);
    const float mx = g->ptr_x;
    const float my = g->ptr_y;
    if (nt_input_mouse_is_pressed(NT_BUTTON_LEFT)) {
        press_active = true;
        dragged = false;
        press_x = mx;
        press_y = my;
        last_x = mx;
        last_y = my;
        return;
    }
    if (press_active && nt_input_mouse_is_down(NT_BUTTON_LEFT)) {
        if (!dragged && (fabsf(mx - press_x) + fabsf(my - press_y)) > 8.0F) {
            dragged = true; /* moved past the threshold -> it's a scroll, not a tap */
        }
        if (dragged) {
            tj_view_world_pan(mx - last_x, my - last_y); /* grab-the-map scroll */
        }
        last_x = mx;
        last_y = my;
        return;
    }
    if (nt_input_mouse_is_released(NT_BUTTON_LEFT) && press_active) {
        press_active = false;
        int gx = -1;
        int gy = -1;
        if (!dragged && s_run.hand >= 0 && tj_view_world_cell_at(mx, my, &gx, &gy) && !tj_run_place_field(&s_run, gx, gy)) {
            tj_journal_push(TJ_LOG_BAD, "Здесь нельзя строить — выбери свободную клетку поля.");
        }
    }
}

static void on_update(game_ctx_t *g, float dt) {
    if (nt_input_key_is_pressed(NT_KEY_P)) {
        game_goto(g, &SCENE_PAUSE);
    }

    tj_run_tick(&s_run, dt);
    handle_map_input(g, dt);

    /* Full-screen frame: top HUD, then [log | map | hero], then card hand. */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        tj_view_top_hud(g, &s_run);
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
            tj_view_log(g, 10);
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) { tj_view_map(g, &s_run); }
            tj_view_hero_panel(g, &s_run);
        }
        tj_view_card_hand(g, &s_run);
        tj_view_pack_overlay(g, &s_run); /* reward modal: full-screen, above the clipped map */
    }

    if (!s_run.alive) {
        tj_tamga_spawn(s_run.cell, s_run.circle);                       /* leave the Last Tamga where the heir fell */
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
    .on_exit = on_exit,
    .fullscreen = true,
};
