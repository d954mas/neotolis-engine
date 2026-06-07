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
#include "save.h"
#include "sim.h"
#include "ui/nt_ui_label.h"
#include "ui_kit.h"
#include "view.h"

#if NT_DEVAPI_ENABLED
#include "devapi/nt_devapi.h"
#endif

static tj_run_t s_run;
static bool s_banked = false;      /* death banked into the aul once per run */
static int s_drag_card = -1;       /* hand card being dragged onto the field (-1 = none) */
static bool s_help_open = false;   /* "?" how-to modal toggled open */
static bool s_ftue_active = false; /* first-run tutorial in progress (pauses the run) */
static int s_ftue_step = 0;        /* 0 intro (reveal->send->walkout), 1 pull, 2 place, 3 merge, 4 done */
static float s_ftue_t = 0.0F;      /* tutorial animation clock (highlight pulse) */
static float s_intro_t = 0.0F;     /* first-run intro clock: dawn reveal + send-the-wayfarer cues */

#if NT_DEVAPI_ENABLED
/* devapi: live run state for bots/tests. */
static int ep_run(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    return snprintf(
        o, (size_t)cap,
        "{\"circle\":%d,\"day\":%d,\"cell\":%d,\"path_cells\":%d,\"phase\":%d,\"hand_count\":%d,\"pouch\":%d,\"in_combat\":%s,\"in_event\":%s,\"tamga_cell\":%d,\"stamina\":%d,\"stamina_max\":%d,"
        "\"supplies\":%d,\"alive\":%s,\"won\":%s}",
        s_run.circle, s_run.day, s_run.cell, s_run.path_cells, (int)s_run.phase, s_run.hand_count, s_run.pouch, s_run.in_combat ? "true" : "false", s_run.in_event ? "true" : "false", s_run.tamga_cell,
        s_run.stamina, s_run.stamina_max, s_run.supplies, s_run.alive ? "true" : "false", s_run.won ? "true" : "false");
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
    return snprintf(o, (size_t)cap, "{\"packs\":%d,\"hand_count\":%d}", s_run.packs, s_run.hand_count);
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
    return snprintf(o, (size_t)cap, "{\"placed\":%s,\"gx\":%d,\"gy\":%d,\"hand_count\":%d}", ok ? "true" : "false", gx, gy, s_run.hand_count);
}

/* devapi: lift a placed building into the (empty) hand. "game.pickup gx=<x> gy=<y>" */
static int ep_pickup(int c, char **v, char *o, int cap, void *u) {
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
    const bool ok = tj_run_pickup_field(&s_run, gx, gy);
    return snprintf(o, (size_t)cap, "{\"picked\":%s,\"hand_count\":%d}", ok ? "true" : "false", s_run.hand_count);
}

/* devapi (debug): force a specific card into hand by tile id. "game.give id=war_1" */
static int ep_give(int c, char **v, char *o, int cap, void *u) {
    (void)u;
    char id[32] = {0};
    for (int i = 0; i < c; i++) {
        if (strncmp(v[i], "id=", 3) == 0) {
            (void)snprintf(id, sizeof id, "%s", v[i] + 3);
        }
    }
    const int t = tj_config_tile_index(id);
    if (t >= 0 && s_run.hand_count < TJ_MAX_HAND) {
        s_run.hand_cards[s_run.hand_count++] = t;
    }
    return snprintf(o, (size_t)cap, "{\"hand_count\":%d}", s_run.hand_count);
}
#endif

/* First-run tutorial gating. Only the very first run ever (persisted as save
 * "ftue_done"). Forces tutorial pulls to a known mergeable tile + tops up the pouch
 * so the lesson always succeeds; the scene pauses the run while the overlay is up. */
static void ftue_begin(void) {
    if (save_get_int("ftue_done", 0) != 0) {
        s_ftue_active = false;
        return;
    }
    s_ftue_active = true;
    s_ftue_step = 0;
    s_ftue_t = 0.0F;
    s_intro_t = 0.0F;
    s_run.phase = TJ_PHASE_AUL_READY;                       /* hero waits at the fire until the player sends him */
    s_run.forced_pull_tile = tj_config_tile_index("war_1"); /* always Точило -> 3-in-a-row merges cleanly */
    s_run.pouch += 4;                                       /* headroom so a misplace can be retried */
}

static void ftue_finish(void) {
    s_ftue_active = false;
    s_run.forced_pull_tile = -1;
    save_set_int("ftue_done", 1);
    save_flush();
}

static bool field_has_any_tile(void) {
    const int n = s_run.grid_cols * s_run.grid_rows;
    for (int i = 0; i < n && i < TJ_ZONE_CELLS; i++) {
        if (s_run.field_tile[i] >= 0) {
            return true;
        }
    }
    return false;
}

static void on_enter(game_ctx_t *g) {
#if NT_DEVAPI_ENABLED
    static bool s_ep_registered = false;
    if (!s_ep_registered) {
        nt_devapi_register("game.run", ep_run, NULL);
        nt_devapi_register("game.log", ep_log, NULL);
        nt_devapi_register("game.path", ep_path, NULL);
        nt_devapi_register("game.place", ep_place, NULL);
        nt_devapi_register("game.pickup", ep_pickup, NULL);
        nt_devapi_register("game.give", ep_give, NULL);
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
    tj_run_start(&s_run, g->chosen_heir); /* one champion of the clan */
    s_banked = false;
    ftue_begin(); /* first-run tutorial (no-op once "ftue_done" is saved) */
}

static void on_exit(game_ctx_t *g) { g->run = NULL; }

/* Start the next run in place (no gameover scene): re-roll the champion, clear flags. */
static void start_new_run(game_ctx_t *g) {
    g->score = 0;
    s_banked = false;
    tj_run_start(&s_run, g->chosen_heir);
    ftue_begin(); /* no-op after the first run (save flag) */
}

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

/* Input. Press on a hand card -> drag it (StS-style); release over a buildable cell
 * places it, else it snaps back. Press on the map -> drag scrolls the camera; a tap
 * on a placed building lifts it into the fan (to move/merge). */
static void handle_map_input(game_ctx_t *g, float dt) {
    static bool press_active = false;
    static bool dragged = false;
    static float press_x = 0.0F;
    static float press_y = 0.0F;
    static float last_x = 0.0F;
    static float last_y = 0.0F;
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
        s_drag_card = tj_view_hand_index_at(g, &s_run, mx, my); /* >=0 if grabbing a card from the fan */
        return;
    }
    if (press_active && nt_input_mouse_is_down(NT_BUTTON_LEFT)) {
        if (s_drag_card < 0) { /* map press: a move past threshold = camera scroll */
            if (!dragged && (fabsf(mx - press_x) + fabsf(my - press_y)) > 8.0F) {
                dragged = true;
            }
            if (dragged) {
                tj_view_world_pan(mx - last_x, my - last_y);
            }
        }
        last_x = mx;
        last_y = my;
        return;
    }
    if (nt_input_mouse_is_released(NT_BUTTON_LEFT) && press_active) {
        press_active = false;
        int gx = -1;
        int gy = -1;
        const bool on_cell = tj_view_world_cell_at(mx, my, &gx, &gy);
        if (s_drag_card >= 0) {
            if (on_cell && !tj_run_place_card(&s_run, s_drag_card, gx, gy)) {
                tj_journal_push(TJ_LOG_BAD, "Сюда нельзя — тяни на зелёную клетку за дорогой.");
            }
            s_drag_card = -1; /* dropped: placed, or snapped back to the fan */
        } else if (!dragged && on_cell) {
            tj_run_pickup_field(&s_run, gx, gy); /* lift a placed building to move it */
        }
    }
}

/* First-run tutorial clocks + state-driven step gates (action steps advance on the
 * real action, not a timer). Intro: dawn reveal -> send -> walkout, then the pull lesson. */
static void ftue_step_tick(float dt) {
    if (!s_ftue_active) {
        return;
    }
    s_ftue_t += dt;
    if (s_ftue_step == 0) {
        s_intro_t += dt;
        if (s_run.phase == TJ_PHASE_AUL_READY && s_intro_t < TJ_REVEAL_SECONDS && nt_input_mouse_is_pressed(NT_BUTTON_LEFT)) {
            s_intro_t = TJ_REVEAL_SECONDS; /* a tap skips ahead to the end of the dawn reveal */
        }
        if (s_run.phase == TJ_PHASE_WALK) {
            s_ftue_step = 1; /* walkout done -> begin the pull lesson */
        }
    } else if (s_ftue_step == 1 && s_run.hand_count > 0) {
        s_ftue_step = 2;
    } else if (s_ftue_step == 2 && field_has_any_tile()) {
        s_ftue_step = 3;
    } else if (s_ftue_step == 3 && s_run.merges_done > 0) {
        s_ftue_step = 4;
    }
}

/* Intro overlays over the world: the dawn veil + the on-theme line for the moment. */
static void draw_intro_overlays(game_ctx_t *g) {
    tj_view_reveal_veil(g, s_intro_t);
    if (s_run.phase != TJ_PHASE_AUL_READY) {
        tj_view_intro_banner(g, "Путь ведёт его сам.");
    } else if (s_intro_t >= TJ_REVEAL_SECONDS) {
        tj_view_intro_banner(g, "Путь ждёт первого путника. Отправь его в дорогу.");
    }
}

static void on_update(game_ctx_t *g, float dt) {
    if (nt_input_key_is_pressed(NT_KEY_P)) {
        game_goto(g, &SCENE_PAUSE);
    }

    const bool in_intro = s_ftue_active && s_ftue_step == 0; /* dawn reveal -> send -> walkout */
    const bool intro_walkout = in_intro && (s_run.phase == TJ_PHASE_AUL_EXIT || s_run.phase == TJ_PHASE_ROAD_ENTRY);

    /* Tick rules: normal play ticks; the intro walkout ticks (hero leaves the aul); the
     * wait-at-fire and the merge lessons stay paused (no time pressure while learning). */
    if (!s_ftue_active || intro_walkout) {
        tj_run_tick(&s_run, dt);
    }
    if (s_run.fx_cell_t > 0.0F) {
        s_run.fx_cell_t -= dt; /* place/merge pop decays every frame, even while the run is paused */
    }
    if (!in_intro) {
        handle_map_input(g, dt); /* camera + cards are locked during the intro */
    }

    ftue_step_tick(dt);

    /* Full-screen frame: top HUD, then [log | map | hero], then card hand. During the
     * first-run intro everything but the world + launch panel is hidden (progressive
     * disclosure: UI opens up step by step as the player needs it). */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        if (!in_intro) {
            tj_view_top_hud(g, &s_run);
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
            if (!in_intro) {
                tj_view_log(g, 10);
            }
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) { tj_view_map(g, &s_run); }
            if (in_intro) {
                if (s_run.phase == TJ_PHASE_AUL_READY && tj_view_launch_panel(g, &s_run, s_intro_t)) {
                    tj_run_send_wayfarer(&s_run); /* "Отправить путника" -> hero walks out */
                }
            } else if (s_run.alive && !s_run.won) {
                tj_view_hero_panel(g, &s_run);
            } else if (tj_view_aul_panel(g, &s_run)) {
                start_new_run(g); /* "Новый забег" pressed in the aul panel */
            }
        }
        if (!in_intro) {
            tj_view_card_hand(g, &s_run, s_drag_card, s_ftue_active);
            if (tj_view_help_button(g)) {
                s_help_open = !s_help_open; /* "?" toggles the how-to modal */
            }
            if (s_help_open && tj_view_help_modal(g)) {
                s_help_open = false; /* "Понятно" closes it */
            }
        }
        if (in_intro) {
            draw_intro_overlays(g);
        } else if (s_ftue_active) {
            const int fr = tj_view_ftue_overlay(g, &s_run, s_ftue_step, s_ftue_t);
            if (fr != 0) {
                ftue_finish(); /* "Играть" on the last card, or "Пропустить" */
            }
        }
        if (s_run.alive && !s_run.won) {
            tj_view_action_overlay(g, &s_run); /* combat / dice window — not over the run-over screen */
        }
        if (s_drag_card >= 0) {
            tj_view_drag_overlay(g, &s_run, s_drag_card); /* dragged card + targeting arrow */
        }
        if (!s_run.alive || s_run.won) {
            tj_view_death_overlay(g, &s_run); /* run-over veil + text (passthrough, above map) */
        }
    }

    /* Run over (death or victory): bank once into the aul, then stay in-scene — the
     * right panel becomes the aul (results + upgrades + "Новый забег"). No gameover scene. */
    if ((!s_run.alive || s_run.won) && !s_banked) {
        if (!s_run.alive) {
            tj_tamga_spawn(s_run.cell, s_run.circle); /* leave the Last Tamga where the heir fell */
        }
        tj_aul_add_from_run(s_run.supplies, s_run.wisdom, s_run.glory); /* bank into the aul (meta) */
        g->score = s_run.circle;
        if (g->score > g->best) {
            g->best = g->score;
        }
        s_banked = true;
    }
    if (!s_run.alive || s_run.won) {
        s_run.death_t += dt; /* drive the run-over veil animation */
    }
}

const scene_t SCENE_GAME = {
    .name = "game",
    .on_enter = on_enter,
    .on_update = on_update,
    .on_exit = on_exit,
    .fullscreen = true,
};
