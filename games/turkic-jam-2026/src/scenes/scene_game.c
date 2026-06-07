/* Run scene — thin orchestrator. Drives the sim, calls the view systems, and
 * handles input + flow. No rendering or sim logic lives here (see view.c / sim.c). */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"

#include "input/nt_input.h"
#include "ui/nt_ui.h"

#include "audio_assets.h"
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
static bool s_run_ack = false;     /* run-over: player dismissed the death screen -> reveal the aul */
static int s_drag_card = -1;       /* hand card being dragged onto the field (-1 = none) */
static int s_prev_merges = 0;      /* edge-detect new merges this frame -> screen-shake punch */
static bool s_help_open = false;   /* "?" how-to modal toggled open */
static bool s_ftue_active = false; /* first-run tutorial in progress (pauses the run) */
static int s_ftue_step = 0;        /* 0 intro, 1 watch (walk+fight to 1st pouch), 2 pull, 3 place+merge (2 seeded), 4 done */
static float s_ftue_t = 0.0F;      /* tutorial animation clock (highlight pulse) */
static float s_intro_t = 0.0F;     /* first-run intro clock: dawn reveal + send-the-wayfarer cues */
static bool s_intro_black = false; /* first-run: holding the black-screen open until the player taps */

#if NT_DEVAPI_ENABLED
/* devapi: live run state for bots/tests. */
static int ep_run(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    return snprintf(
        o, (size_t)cap,
        "{\"circle\":%d,\"day\":%d,\"cell\":%d,\"path_cells\":%d,\"phase\":%d,\"hand_count\":%d,\"pouch\":%d,\"in_combat\":%s,\"combat_win\":%s,\"in_event\":%s,\"tamga_cell\":%d,\"stamina\":%d,"
        "\"stamina_max\":%d,\"supplies\":%d,\"alive\":%s,\"won\":%s}",
        s_run.circle, s_run.day, s_run.cell, s_run.path_cells, (int)s_run.phase, s_run.hand_count, s_run.pouch, s_run.in_combat ? "true" : "false", s_run.combat_win ? "true" : "false",
        s_run.in_event ? "true" : "false", s_run.tamga_cell, s_run.stamina, s_run.stamina_max, s_run.supplies, s_run.alive ? "true" : "false", s_run.won ? "true" : "false");
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
    s_intro_black = true;                                   /* open on a black screen; the first tap unlocks web audio + starts the reveal */
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
    const bool cam_locked = s_ftue_active && s_ftue_step == 3; /* keep the merge-gap on screen during the lesson */
    if (!cam_locked) {
        camera_keys(dt);
    }
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
        if (s_drag_card < 0 && !cam_locked) { /* map press: a move past threshold = camera scroll */
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
/* FTUE merge lesson: pre-place 2 matching tiles (war_1) so a single guided drag completes a
 * 3-in-a-row, teaching place + merge in one move. Picks a horizontal buildable triple nearest the
 * grid centre (on-screen without panning); the gap is highlighted for the player. */
static void ftue_seed_merge(void) {
    const int war = tj_config_tile_index("war_1");
    if (war < 0) {
        return;
    }
    const int cols = s_run.grid_cols;
    const int rows = s_run.grid_rows;
    int bx = -1;
    int by = -1;
    int bd = 1 << 30;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x + 2 < cols; x++) {
            const int i0 = (y * cols) + x;
            if (tj_run_cell_buildable(&s_run, x, y) && tj_run_cell_buildable(&s_run, x + 1, y) && tj_run_cell_buildable(&s_run, x + 2, y) && s_run.field_tile[i0] < 0 && s_run.field_tile[i0 + 1] < 0 &&
                s_run.field_tile[i0 + 2] < 0) {
                const int dx = (x + 1) - (cols / 2);
                const int dy = y - (rows / 2);
                const int d = (dx * dx) + (dy * dy);
                if (d < bd) {
                    bd = d;
                    bx = x;
                    by = y;
                }
            }
        }
    }
    if (bx < 0) {
        return;
    }
    s_run.field_tile[(by * cols) + bx] = war;
    s_run.field_tile[(by * cols) + bx + 1] = war;
    tj_view_set_ftue_gap(bx + 2, by); /* the cell the player fills to complete the trio */
}

static void ftue_step_tick(float dt) {
    if (!s_ftue_active) {
        return;
    }
    s_ftue_t += dt;
    if (s_ftue_step == 0) {
        if (s_intro_black) {
            if (nt_input_mouse_is_pressed(NT_BUTTON_LEFT)) {
                s_intro_black = false; /* first tap: unlocks web audio (main.c resume) + starts the dawn reveal */
            }
            return; /* hold on the black screen until the tap; consume it so the reveal isn't skipped */
        }
        s_intro_t += dt;
        if (s_run.phase == TJ_PHASE_AUL_READY && s_intro_t < TJ_REVEAL_SECONDS && nt_input_mouse_is_pressed(NT_BUTTON_LEFT)) {
            s_intro_t = TJ_REVEAL_SECONDS; /* a tap skips ahead to the end of the dawn reveal */
        }
        if (s_run.phase == TJ_PHASE_WALK) {
            s_ftue_step = 1; /* walkout done -> watch phase: batyr walks + fights on his own */
        }
    } else if (s_ftue_step == 1 && s_run.pouch > 0) {
        s_ftue_step = 2; /* first pouch earned in combat -> teach the pull */
    } else if (s_ftue_step == 2 && s_run.hand_count > 0) {
        ftue_seed_merge();
        s_ftue_step = 3; /* pull done -> place+merge lesson (2 tiles pre-placed, drag 1 to complete) */
    } else if (s_ftue_step == 3 && s_run.merges_done > 0) {
        tj_view_set_ftue_gap(-1, -1);
        s_ftue_step = 4; /* the guided drag merged -> tutorial complete */
    }
}

/* Intro UI over the world: the on-theme line for the moment (the dawn veil + sand are
 * drawn in the sprite world pass, not here). */
static void draw_intro_overlays(game_ctx_t *g) {
    if (s_run.phase != TJ_PHASE_AUL_READY) {
        tj_view_intro_banner(g, "Путь ведёт его сам.");
    } else if (s_intro_t >= TJ_REVEAL_SECONDS) {
        tj_view_intro_banner(g, "Путь ждёт первого батыра. Отправь его в дорогу.");
    }
}

/* One-shot SFX on game-state rising edges. New-run resets (merges/fx back to 0) self-correct
 * via the end-of-frame prev capture, so they don't mis-fire. */
static void sfx_tick(float dt) {
    static int prev_merges = 0;
    static bool prev_win = false;
    static bool prev_event = false;
    static bool prev_alive = true;
    static float prev_enemy_fx = 0.0F;
    static float hit_cd = 0.0F;
    if (hit_cd > 0.0F) {
        hit_cd -= dt;
    }
    if (s_run.merges_done > prev_merges) {
        tj_audio_play_sfx(TJ_SFX_MERGE);
    }
    if (s_run.combat_win && !prev_win) {
        tj_audio_play_sfx(TJ_SFX_VICTORY);
    }
    if (s_run.in_event && !prev_event) {
        tj_audio_play_sfx(TJ_SFX_EVENT);
    }
    if (!s_run.alive && prev_alive) {
        tj_audio_play_sfx(TJ_SFX_DEATH);
    }
    if (s_run.fx_enemy_t > prev_enemy_fx + 0.01F && hit_cd <= 0.0F) {
        tj_audio_play_sfx(TJ_SFX_HIT); /* hero just landed a blow on the enemy */
        hit_cd = 0.13F;                /* throttle: machine-gun hits blur into a low hum */
    }
    prev_merges = s_run.merges_done;
    prev_win = s_run.combat_win;
    prev_event = s_run.in_event;
    prev_alive = s_run.alive;
    prev_enemy_fx = s_run.fx_enemy_t;
}

static void on_update(game_ctx_t *g, float dt) {
    if (g->request_restart) { /* full reset from settings: drop straight into a fresh run */
        g->request_restart = false;
        s_drag_card = -1;
        s_run_ack = false;
        start_new_run(g);
    }
    const bool paused = g->settings_open; /* settings modal freezes the run */
    g->anim_t += dt;                      /* free-running clock for glows/pulses (runs even while paused) */
    if (!paused && nt_input_key_is_pressed(NT_KEY_P)) {
        game_goto(g, &SCENE_PAUSE);
    }

    const bool in_intro = s_ftue_active && s_ftue_step == 0; /* dawn reveal -> send -> walkout */
    const bool intro_walkout = in_intro && (s_run.phase == TJ_PHASE_AUL_EXIT || s_run.phase == TJ_PHASE_ROAD_ENTRY);
    const bool ftue_watch = s_ftue_active && s_ftue_step == 1; /* batyr walks + fights on his own until the first pouch */

    /* Tick rules: the settings modal freezes everything; otherwise normal play ticks, the intro
     * walkout + the FTUE "watch" phase tick (batyr moves/fights), and the pull/place/merge lessons
     * stay paused (no time pressure while learning). */
    if (!paused && (!s_ftue_active || intro_walkout || ftue_watch)) {
        tj_run_tick(&s_run, dt);
    }
    if (s_run.fx_cell_t > 0.0F) {
        s_run.fx_cell_t -= dt; /* place/merge pop decays every frame, even while the run is paused */
    }
    if (!paused && !in_intro && s_run.alive && !s_run.won) {
        handle_map_input(g, dt); /* camera + cards locked during intro / settings / run-over (no dragging while dead) */
    }
    if (!paused) {
        ftue_step_tick(dt);
        tj_view_battle_tick(g, &s_run, dt); /* combat-stage particles/shake + hit/victory bursts */
        sfx_tick(dt);                       /* one-shot SFX on state edges */
    }
    /* Merge punch: shake scales with how many fuses landed this frame (cascades hit harder). */
    if (s_run.merges_done != s_prev_merges) {
        if (s_run.merges_done > s_prev_merges) {
            const float amt = 0.26F * (float)(s_run.merges_done - s_prev_merges);
            tj_shake_add(&g->shake, (amt > 0.7F) ? 0.7F : amt);
        }
        s_prev_merges = s_run.merges_done; /* also resets on a fresh run (merges_done back to 0) */
    }

    /* Hand the intro state to the sprite world pass (it draws the black screen + sand +
     * dawn veil; Clay carries only the UI). */
    g->intro_active = in_intro;
    g->intro_black = in_intro && s_intro_black;
    g->intro_t = s_intro_t;
    g->intro_anim_t = s_ftue_t;
    g->drag_tile = (s_drag_card >= 0 && s_drag_card < s_run.hand_count) ? s_run.hand_cards[s_drag_card] : -1; /* held card -> merge telegraph */

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
                if (!s_intro_black && s_run.phase == TJ_PHASE_AUL_READY && tj_view_launch_panel(g, &s_run, s_intro_t)) {
                    tj_run_send_wayfarer(&s_run); /* "Отправить путника" -> hero walks out */
                }
            } else if (s_run.alive && !s_run.won) {
                tj_view_hero_panel(g, &s_run);
            } else if (!s_run_ack) {
                if (tj_view_death_panel(g, &s_run)) {
                    s_run_ack = true; /* verdict acknowledged -> reveal the aul + upgrades */
                }
            } else if (tj_view_aul_panel(g, &s_run)) {
                start_new_run(g); /* "Отправить путника" -> send the next heir */
            }
        }
        if (!in_intro) {
            if (s_run.alive && !s_run.won) {
                tj_view_card_hand(g, &s_run, s_drag_card, s_ftue_active); /* no hand on the run-over screen */
            }
            if (!paused && tj_view_help_button(g)) {
                s_help_open = !s_help_open; /* "?" toggles the how-to modal */
            }
            if (tj_view_settings_button(g)) {
                g->settings_open = true; /* gear opens the settings modal (pauses the run) */
            }
            if (s_help_open && tj_view_help_modal(g)) {
                s_help_open = false; /* "Понятно" closes it */
            }
        }
        if (in_intro && s_intro_black) {
            tj_view_intro_black(g, s_ftue_t); /* black-screen open; tap continues (handled in ftue_step_tick) */
        } else if (in_intro) {
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
        if (!in_intro && s_run.alive && !s_run.won) {
            tj_view_field_badges(g, &s_run);  /* level numbers + max crowns over the field */
            tj_view_field_tooltip(g, &s_run); /* hover a building -> name + level + effect */
        }
        if (s_drag_card >= 0) {
            tj_view_drag_overlay(g, &s_run, s_drag_card); /* dragged card + targeting arrow + merge "+N" */
        }
        /* Run-over presentation lives in the right panel now (tj_view_aul_panel): no
         * full-screen veil over the map — the panel carries the verdict + sand line. */
    }

    /* Run over (death or victory): bank once into the aul, then stay in-scene — the
     * right panel becomes the aul (results + upgrades + "Новый забег"). No gameover scene. */
    if ((!s_run.alive || s_run.won) && !s_banked) {
        if (!s_run.alive) {
            tj_tamga_spawn(s_run.cell, s_run.circle); /* leave the Last Tamga where the heir fell */
        }
        s_run.supplies += tj_run_field_supplies(&s_run);                /* Жильё legacy payout: cashes out once now, not per circle */
        tj_aul_add_from_run(s_run.supplies, s_run.wisdom, s_run.glory); /* bank into the aul (meta) */
        s_run.supplies = 0;                                             /* haul is now in the aul; HUD shows g_aul.supplies + run->supplies, no double-count */
        g->score = s_run.circle;
        if (g->score > g->best) {
            g->best = g->score;
        }
        s_banked = true;
        s_run_ack = false; /* start the run-over on the death screen; the aul appears after "В аул" */
    }
    if (!paused && (!s_run.alive || s_run.won)) {
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
