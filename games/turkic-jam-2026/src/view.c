#include "view.h"

#include <math.h>
#include <stdio.h>

#include "clay.h"

#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_label.h"

#include "config.h"
#include "journal.h"
#include "ui_kit.h"

// #region styles
static const nt_ui_label_style_t s_aul_label = {.font_id = 0, .font_size = 16, .color = {40.0F, 30.0F, 18.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};

/* One style per journal kind (TJ_LOG_*). */
static const nt_ui_label_style_t s_log_styles[4] = {
    [TJ_LOG_PLAIN] = {.font_id = 0, .font_size = 18, .color = {150.0F, 158.0F, 172.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
    [TJ_LOG_GOOD] = {.font_id = 0, .font_size = 18, .color = {150.0F, 210.0F, 150.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
    [TJ_LOG_BAD] = {.font_id = 0, .font_size = 18, .color = {232.0F, 138.0F, 120.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
    [TJ_LOG_BIG] = {.font_id = 0, .font_size = 20, .color = {255.0F, 210.0F, 120.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
};

static const nt_ui_label_style_t s_chip = {.font_id = 0, .font_size = 19, .color = {214.0F, 204.0F, 184.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_circle_big = {.font_id = 0, .font_size = 30, .color = {255.0F, 214.0F, 120.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_panel_title = {.font_id = 0, .font_size = 20, .color = {196.0F, 168.0F, 124.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT};
static const nt_ui_label_style_t s_stat = {.font_id = 0, .font_size = 20, .color = {210.0F, 216.0F, 228.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_dim = {.font_id = 0, .font_size = 17, .color = {130.0F, 138.0F, 152.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_card_name = {.font_id = 0, .font_size = 19, .color = {245.0F, 236.0F, 214.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};

#define TJ_PANEL_BG {16.0F, 19.0F, 30.0F, 255.0F}
#define TJ_BAR_BG {20.0F, 24.0F, 38.0F, 255.0F}
#define TJ_CHIP_BG {34.0F, 40.0F, 58.0F, 255.0F}
// #endregion

// #region map geometry (winding trail loop around the central aul, Loop Hero style)
#define MAP_SIZE 360.0F

static Clay_Color cell_color(int tile_idx) {
    if (tile_idx < 0 || tile_idx >= g_config.tile_count) {
        return (Clay_Color){62.0F, 64.0F, 76.0F, 255.0F};
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

/* Square tile marker, floating-offset from the map centre. */
#define MAP_TILE(sz, col, ox, oy, z)                                                                                                                                                                   \
    {                                                                                                                                                                                                  \
        .layout = {.sizing = {CLAY_SIZING_FIXED(sz), CLAY_SIZING_FIXED(sz)}}, .backgroundColor = (col), .cornerRadius = CLAY_CORNER_RADIUS(3.0F), .floating = {                                        \
            .attachTo = CLAY_ATTACH_TO_PARENT,                                                                                                                                                         \
            .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},                                                                                   \
            .offset = {(ox), (oy)},                                                                                                                                                                    \
            .zIndex = (z),                                                                                                                                                                             \
        }                                                                                                                                                                                              \
    }

/* Floating rect (w x h, corner radius cr), offset from the map centre. */
#define MAP_RECT(w, h, col, cr, ox, oy, z)                                                                                                                                                             \
    {                                                                                                                                                                                                  \
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}}, .backgroundColor = (col), .cornerRadius = CLAY_CORNER_RADIUS(cr), .floating = {                                            \
            .attachTo = CLAY_ATTACH_TO_PARENT,                                                                                                                                                         \
            .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},                                                                                   \
            .offset = {(ox), (oy)},                                                                                                                                                                    \
            .zIndex = (z),                                                                                                                                                                             \
        }                                                                                                                                                                                              \
    }

/* Cell coord -> pixel offset from the map centre (grid centred in the area). */
static float grid_x(int gx, int cols, float pitch) { return ((float)gx - ((float)(cols - 1) * 0.5F)) * pitch; }
static float grid_y(int gy, int rows, float pitch) { return ((float)gy - ((float)(rows - 1) * 0.5F)) * pitch; }
// #endregion

static void hud_chip(game_ctx_t *g, const char *text) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_GROW(0)}, .padding = {14, 14, 0, 0}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_CHIP_BG,
          .cornerRadius = CLAY_CORNER_RADIUS(9.0F)}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_chip);
    }
}

static void hud_spacer(void) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(0)}}}) {}
}

void tj_view_top_hud(game_ctx_t *g, const tj_run_t *run) {
    static char circle[40];
    static char sup[28];
    static char wis[28];
    static char glo[28];
    static char sta[28];
    static char day[24];
    (void)snprintf(circle, sizeof circle, "КРУГ %d/%d", run->circle, g_config.laps_to_win);
    (void)snprintf(sup, sizeof sup, "Запасы %d", run->supplies);
    (void)snprintf(wis, sizeof wis, "Мудрость %d", run->wisdom);
    (void)snprintf(glo, sizeof glo, "Слава %d", run->glory);
    (void)snprintf(sta, sizeof sta, "Силы %d", run->stamina);
    (void)snprintf(day, sizeof day, "День %d", run->day);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64)},
                     .padding = {16, 16, 10, 10},
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 10,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_BAR_BG}) {
        hud_chip(g, sup);
        hud_chip(g, wis);
        hud_chip(g, glo);
        hud_spacer();
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), circle, &s_circle_big);
        hud_spacer();
        hud_chip(g, day);
        hud_chip(g, sta);
        hud_chip(g, "x1");
    }
}

static void draw_aul(game_ctx_t *g, const tj_run_t *run, float pitch) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const float acx = (float)run->aul_x0 + ((float)(run->aul_w - 1) * 0.5F);
    const float acy = (float)run->aul_y0 + ((float)(run->aul_h - 1) * 0.5F);
    const float ox = (acx - ((float)(cols - 1) * 0.5F)) * pitch;
    const float oy = (acy - ((float)(rows - 1) * 0.5F)) * pitch;
    const float w = (float)run->aul_w * pitch * 0.92F;
    const float h = (float)run->aul_h * pitch * 0.92F;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {176.0F, 146.0F, 106.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(6.0F),
          .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {ox, oy}}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "Аул", &s_aul_label);
    }
}

/* Road as a continuous trail: a connector bridges each pair of consecutive
 * cells, a node square fills every cell (and the turns) -> one winding path. */
static void draw_road(const tj_run_t *run, float pitch) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const float w = pitch * 0.42F; /* trail thickness */
    const Clay_Color trail = {150.0F, 128.0F, 96.0F, 255.0F};
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        const int j = (i + 1) % run->path_cells;
        const float xi = grid_x(run->path_gx[i], cols, pitch);
        const float yi = grid_y(run->path_gy[i], rows, pitch);
        const float xj = grid_x(run->path_gx[j], cols, pitch);
        const float yj = grid_y(run->path_gy[j], rows, pitch);
        const bool horiz = (run->path_gy[i] == run->path_gy[j]);
        CLAY(MAP_RECT(horiz ? pitch : w, horiz ? w : pitch, trail, 0.0F, (xi + xj) * 0.5F, (yi + yj) * 0.5F, 1)) {}
    }
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        CLAY(MAP_RECT(w, w, trail, 0.0F, grid_x(run->path_gx[i], cols, pitch), grid_y(run->path_gy[i], rows, pitch), 1)) {}
    }
}

/* Road events (scope on_enter): a kind-coloured marker sitting on the trail. */
static void draw_road_events(const tj_run_t *run, float pitch) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const float m = pitch * 0.5F;
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        if (run->tile_at[i] >= 0) {
            CLAY(MAP_RECT(m, m, cell_color(run->tile_at[i]), 4.0F, grid_x(run->path_gx[i], cols, pitch), grid_y(run->path_gy[i], rows, pitch), 2)) {}
        }
    }
}

/* Global desert objects (scope global): functional landmarks out in the field. */
static void draw_global(const tj_run_t *run, float pitch, float tile) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    for (int i = 0; i < run->global_count && i < TJ_MAX_GLOBAL; i++) {
        const float x = grid_x(run->global_gx[i], cols, pitch);
        const float y = grid_y(run->global_gy[i], rows, pitch);
        CLAY(MAP_RECT(tile, tile, cell_color(run->global_tile[i]), 8.0F, x, y, 1)) {}
    }
}

/* The ancestor's Last Tamga: a bright gold mark on a road cell, collected on pass. */
static void draw_tamga(const tj_run_t *run, float pitch) {
    if (run->tamga_cell < 0 || run->tamga_cell >= run->path_cells) {
        return;
    }
    const float x = grid_x(run->path_gx[run->tamga_cell], run->grid_cols, pitch);
    const float y = grid_y(run->path_gy[run->tamga_cell], run->grid_rows, pitch);
    const float m = pitch * 0.56F;
    CLAY(MAP_RECT(m, m, ((Clay_Color){255.0F, 226.0F, 130.0F, 255.0F}), m * 0.5F, x, y, 2)) {}
}

/* Build slots (the outward empty cell beside each road cell). Filled = marker;
 * empty = clickable button that places the held card. */
static void draw_slots(game_ctx_t *g, tj_run_t *run, float pitch, float tile) {
    static uint32_t base = 0U;
    if (base == 0U) {
        base = nt_ui_id("tj_slot");
    }
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        if (run->slot_gx[i] == TJ_NO_SLOT) {
            continue; /* no free cell beside this road cell */
        }
        const int sgx = run->slot_gx[i];
        const int sgy = run->slot_gy[i];
        if (run->field_tile[(sgy * cols) + sgx] >= 0) {
            continue; /* already built here (drawn by draw_field) */
        }
        const float sx = grid_x(sgx, cols, pitch);
        const float sy = grid_y(sgy, rows, pitch);
        /* Highlight buildable cells green while a card is held (valid placement). */
        const uint32_t idle_tint = (run->hand >= 0) ? 0xCC3AA85EU : 0x553A302CU;
        const nt_ui_button_style_t st = {
            .idle = {.atlas = g->atlas, .bg_region = g->white_region, .bg_tint = idle_tint, .scale = 1.0F, .opacity = 1.0F},
            .hover = {.bg_region = g->white_region, .bg_tint = 0xFF7CE08CU, .scale = 1.12F, .opacity = 1.0F},
            .pressed = {.bg_region = g->white_region, .bg_tint = 0xFF7CE08CU, .scale = 0.95F, .opacity = 1.0F},
            .disabled = {.bg_region = g->white_region, .bg_tint = 0x442A2422U, .scale = 1.0F, .opacity = 0.5F},
            .transition_speed = 14.0F,
            .hit_padding_lrtb = {4, 4, 4, 4},
            .slice9_scale = 1.0F,
        };
        const Clay_ElementDeclaration decl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(tile), CLAY_SIZING_FIXED(tile)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {sx, sy}, .zIndex = 5},
        };
        if (nt_ui_button(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), base + (uint32_t)i, &st, &decl, run->hand >= 0)) {
            tj_run_place_field(run, sgx, sgy); /* persistent build; road re-routes around it */
        }
    }
}

/* Click anywhere on the map that is not a buildable slot (while holding a card)
 * -> short "can't build here" feedback in the log (no modal). */
static void draw_build_catcher(game_ctx_t *g, tj_run_t *run) {
    if (run->hand < 0) {
        return;
    }
    static uint32_t cid = 0U;
    if (cid == 0U) {
        cid = nt_ui_id("tj_nobuild");
    }
    const nt_ui_button_style_t st = {
        .idle = {.atlas = g->atlas, .bg_region = g->white_region, .bg_tint = 0x00000000U, .scale = 1.0F, .opacity = 0.0F},
        .hover = {.bg_region = g->white_region, .bg_tint = 0x00000000U, .scale = 1.0F, .opacity = 0.0F},
        .pressed = {.bg_region = g->white_region, .bg_tint = 0x00000000U, .scale = 1.0F, .opacity = 0.0F},
        .disabled = {.bg_region = g->white_region, .bg_tint = 0x00000000U, .scale = 1.0F, .opacity = 0.0F},
        .transition_speed = 1.0F,
        .slice9_scale = 1.0F,
    };
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(MAP_SIZE), CLAY_SIZING_FIXED(MAP_SIZE)}},
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {0.0F, 0.0F}, .zIndex = 4},
    };
    if (nt_ui_button(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), cid, &st, &decl, true)) {
        tj_journal_push(TJ_LOG_BAD, "Здесь не строят. Ставь в подсвеченные клетки у дороги.");
    }
}

/* Persistent player builds in the desert (survive the per-circle road reshuffle). */
static void draw_field(const tj_run_t *run, float pitch, float tile) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const int n = cols * rows;
    for (int i = 0; i < n && i < TJ_ZONE_CELLS; i++) {
        if (run->field_tile[i] < 0) {
            continue;
        }
        const int gx = i % cols;
        const int gy = i / cols;
        CLAY(MAP_RECT(tile, tile, cell_color(run->field_tile[i]), 6.0F, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch), 1)) {}
    }
}

/* Hero position while walking the loop: lerp between the current and next cell. */
static void hero_walk_pos(const tj_run_t *run, float pitch, float *hx, float *hy) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const int n = (run->path_cells > 0) ? run->path_cells : 1;
    const float per = g_config.move_seconds_per_cell;
    float frac = (per > 0.0F) ? (run->move_t / per) : 0.0F;
    if (frac < 0.0F) {
        frac = 0.0F;
    } else if (frac > 1.0F) {
        frac = 1.0F;
    }
    int i0 = run->cell % n;
    if (i0 < 0) {
        i0 = 0;
    }
    const int i1 = (i0 + 1) % n;
    const float x0 = grid_x(run->path_gx[i0], cols, pitch);
    const float y0 = grid_y(run->path_gy[i0], rows, pitch);
    *hx = x0 + ((grid_x(run->path_gx[i1], cols, pitch) - x0) * frac);
    *hy = y0 + ((grid_y(run->path_gy[i1], rows, pitch) - y0) * frac);
}

/* Hero position during the FTUE intro: lerp from the aul centre to road cell 0. */
static void hero_exit_pos(const tj_run_t *run, float pitch, float *hx, float *hy) {
    const float dur = (g_config.aul_exit_seconds > 0.0F) ? g_config.aul_exit_seconds : 2.0F;
    float f = run->intro_t / dur;
    if (f < 0.0F) {
        f = 0.0F;
    } else if (f > 1.0F) {
        f = 1.0F;
    }
    const float acx = (float)run->aul_x0 + ((float)(run->aul_w - 1) * 0.5F);
    const float acy = (float)run->aul_y0 + ((float)(run->aul_h - 1) * 0.5F);
    const float ax = (acx - ((float)(run->grid_cols - 1) * 0.5F)) * pitch;
    const float ay = (acy - ((float)(run->grid_rows - 1) * 0.5F)) * pitch;
    const float c0x = grid_x(run->path_gx[0], run->grid_cols, pitch);
    const float c0y = grid_y(run->path_gy[0], run->grid_rows, pitch);
    *hx = ax + ((c0x - ax) * f);
    *hy = ay + ((c0y - ay) * f);
}

static void draw_hero(const tj_run_t *run, float pitch) {
    float hx;
    float hy;
    if (run->phase == TJ_PHASE_AUL_EXIT) {
        hero_exit_pos(run, pitch, &hx, &hy);
    } else {
        hero_walk_pos(run, pitch, &hx, &hy);
    }
    const float hs = pitch * 0.40F;
    CLAY(MAP_RECT(hs, hs, ((Clay_Color){255.0F, 212.0F, 96.0F, 255.0F}), hs * 0.5F, hx, hy, 3)) {}
}

/* Sandstorm veil over the map: opaque at the circle change, fades to reveal the
 * new (different) path -> the reshuffle reads as a desert storm, not a hard snap. */
static void draw_storm(const tj_run_t *run) {
    if (run->storm_t <= 0.0F) {
        return;
    }
    const float dur = (g_config.storm_seconds > 0.0F) ? g_config.storm_seconds : 1.3F;
    float a = run->storm_t / dur;
    if (a > 1.0F) {
        a = 1.0F;
    }
    const Clay_Color sand = {196.0F, 172.0F, 122.0F, a * 235.0F};
    CLAY(MAP_RECT(MAP_SIZE, MAP_SIZE, sand, 0.0F, 0.0F, 0.0F, 10)) {}
}

static void pack_row(game_ctx_t *g, tj_run_t *run) {
    static const char *ids[3] = {"tj_pick0", "tj_pick1", "tj_pick2"};
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 10}}) {
        for (int i = 0; i < 3; i++) {
            const int ti = run->pack_offer[0][i];
            const char *name = (ti >= 0 && ti < g_config.tile_count) ? g_config.tiles[ti].name : "-";
            if (tj_button(g, ids[i], name, 148, 78, TJ_BTN_SECONDARY)) {
                tj_run_choose_card(run, i);
            }
        }
    }
}

/* Reward choice floats over the aul (does NOT pause the run -> no broken idle). */
static void draw_pack_choice(game_ctx_t *g, tj_run_t *run) {
    if (!run->pack_open || run->packs <= 0) {
        return;
    }
    CLAY({.floating =
              {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {0.0F, -24.0F}, .zIndex = 25},
          .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(14),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 10,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {20.0F, 24.0F, 38.0F, 238.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(14.0F),
          .border = {.color = {196.0F, 168.0F, 124.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "Дар пути - выбери карту", &s_panel_title);
        pack_row(g, run);
    }
}

void tj_view_map(game_ctx_t *g, tj_run_t *run) {
    if (run->grid_cols < 2 || run->grid_rows < 2 || run->path_cells < 1) {
        return; /* loop not generated yet */
    }
    const int maxdim = (run->grid_cols > run->grid_rows) ? run->grid_cols : run->grid_rows;
    const float pitch = MAP_SIZE / (float)(maxdim + 1);
    const float tile = pitch * 0.66F; /* placed objects beside the trail */

    CLAY({.id = CLAY_ID("map"), .layout = {.sizing = {CLAY_SIZING_FIXED(MAP_SIZE), CLAY_SIZING_FIXED(MAP_SIZE)}}}) {
        draw_aul(g, run, pitch);
        draw_road(run, pitch);
        draw_road_events(run, pitch);
        draw_global(run, pitch, tile);
        draw_field(run, pitch, tile);
        draw_tamga(run, pitch);
        draw_build_catcher(g, run);
        draw_slots(g, run, pitch, tile);
        draw_hero(run, pitch);
        draw_storm(run);
        draw_pack_choice(g, run);
    }
}

void tj_view_log(game_ctx_t *g, int max_lines) {
    const int avail = tj_journal_count();
    const int shown = avail < max_lines ? avail : max_lines;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(14), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}, .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "Летопись", &s_panel_title);
        for (int k = shown - 1; k >= 0; k--) {
            tj_log_kind_t kind = TJ_LOG_PLAIN;
            const char *line = tj_journal_get(k, &kind);
            if (line) {
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), line, &s_log_styles[kind]);
            }
        }
    }
}

static const char *tj_hero_name(const tj_run_t *run) {
    if (run->heir_index >= 0 && run->heir_index < g_config.heir_count) {
        return g_config.heirs[run->heir_index].name;
    }
    return "Наследник";
}

void tj_view_hero_panel(game_ctx_t *g, const tj_run_t *run) {
    static char body[20];
    static char mind[20];
    static char spirit[20];
    static char sta[24];
    static char cellinfo[40];
    (void)snprintf(body, sizeof body, "Тело %d", run->body);
    (void)snprintf(mind, sizeof mind, "Ум %d", run->mind);
    (void)snprintf(spirit, sizeof spirit, "Дух %d", run->spirit);
    (void)snprintf(sta, sizeof sta, "Силы %d", run->stamina);
    (void)snprintf(cellinfo, sizeof cellinfo, "Клетка %d / %d", run->cell + 1, run->path_cells);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 12,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), tj_hero_name(run), &s_panel_title);
        /* Hero doll placeholder (real silhouette/equipment slots later). */
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(150)}}, .backgroundColor = {54.0F, 60.0F, 80.0F, 255.0F}, .cornerRadius = CLAY_CORNER_RADIUS(14.0F)}) {}
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), body, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), mind, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), spirit, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), sta, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), cellinfo, &s_dim);
    }
}

static void hand_card(game_ctx_t *g, const char *name, bool active) {
    const Clay_Color bg = active ? (Clay_Color){46.0F, 54.0F, 40.0F, 255.0F} : (Clay_Color){26.0F, 30.0F, 44.0F, 255.0F};
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(132), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(10), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = bg,
          .cornerRadius = CLAY_CORNER_RADIUS(10.0F),
          .border = {.color = active ? (Clay_Color){150.0F, 210.0F, 150.0F, 255.0F} : (Clay_Color){44.0F, 50.0F, 66.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), name, active ? &s_card_name : &s_dim);
    }
}

/* Bottom hand: the held card + empty slots. Reward choice floats over the aul. */
void tj_view_card_hand(game_ctx_t *g, tj_run_t *run) {
    const bool has = (run->hand >= 0 && run->hand < g_config.tile_count);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(118)},
                     .padding = CLAY_PADDING_ALL(12),
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 12,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_BAR_BG}) {
        hand_card(g, has ? g_config.tiles[run->hand].name : "-", has);
        for (int i = 1; i < 5; i++) {
            hand_card(g, "пусто", false);
        }
        if (has) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "  кликни свободный слот у дороги, чтобы поставить", &s_dim);
        }
    }
}
