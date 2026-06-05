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

void tj_view_hud(game_ctx_t *g, const tj_run_t *run) {
    static char l_circle[64];
    static char l_res[128];
    const char *hand = (run->hand >= 0 && run->hand < g_config.tile_count) ? g_config.tiles[run->hand].name : "нет";
    (void)snprintf(l_circle, sizeof l_circle, "Круг %d/%d", run->circle, g_config.laps_to_win);
    (void)snprintf(l_res, sizeof l_res, "Силы %d   Запасы %d   Мудрость %d   Слава %d     Карта: %s", run->stamina, run->supplies, run->wisdom, run->glory, hand);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), l_circle, &TJ_STYLE_HEADING);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), l_res, &TJ_STYLE_BODY);
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
        const float sx = grid_x(run->slot_gx[i], cols, pitch);
        const float sy = grid_y(run->slot_gy[i], rows, pitch);
        if (run->roadside[i] >= 0) {
            CLAY(MAP_TILE(tile, cell_color(run->roadside[i]), sx, sy, 0)) {}
            continue;
        }
        const nt_ui_button_style_t st = {
            .idle = {.atlas = g->atlas, .bg_region = g->white_region, .bg_tint = 0xFF3A302CU, .scale = 1.0F, .opacity = 1.0F},
            .hover = {.bg_region = g->white_region, .bg_tint = 0xFF5AA0FFU, .scale = 1.12F, .opacity = 1.0F},
            .pressed = {.bg_region = g->white_region, .bg_tint = 0xFF5AA0FFU, .scale = 0.95F, .opacity = 1.0F},
            .disabled = {.bg_region = g->white_region, .bg_tint = 0xFF2A2422U, .scale = 1.0F, .opacity = 0.6F},
            .transition_speed = 14.0F,
            .hit_padding_lrtb = {4, 4, 4, 4},
            .slice9_scale = 1.0F,
        };
        const Clay_ElementDeclaration decl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(tile), CLAY_SIZING_FIXED(tile)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {sx, sy}},
        };
        if (nt_ui_button(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), base + (uint32_t)i, &st, &decl, run->hand >= 0)) {
            tj_run_place_roadside(run, i);
        }
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
        draw_slots(g, run, pitch, tile);
        draw_hero(run, pitch);
    }
}

void tj_view_journal(game_ctx_t *g, int max_lines) {
    const int avail = tj_journal_count();
    int shown = avail < max_lines ? avail : max_lines;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(620), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2}}) {
        for (int k = shown - 1; k >= 0; k--) {
            tj_log_kind_t kind = TJ_LOG_PLAIN;
            const char *line = tj_journal_get(k, &kind);
            if (line) {
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), line, &s_log_styles[kind]);
            }
        }
    }
}
