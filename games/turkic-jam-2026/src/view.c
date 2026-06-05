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

// #region map geometry (square tiles on a rectangular grid loop, Loop Hero style)
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

/* Rectangle dims whose perimeter == n (n even); kept roughly square. */
static void grid_dims(int n, int *cols, int *rows) {
    const int half = (n / 2) + 2; /* cols + rows */
    int c = (half + 1) / 2;
    int r = half - c;
    if (c < 3) {
        c = 3;
    }
    if (r < 3) {
        r = 3;
    }
    *cols = c;
    *rows = r;
}

/* Cell index i (clockwise from the top-left corner) -> grid coords. */
static void cell_grid(int i, int cols, int rows, int *gx, int *gy) {
    const int top = cols;
    const int right = rows - 1;
    const int bottom = cols - 1;
    if (i < top) {
        *gx = i;
        *gy = 0;
    } else if (i < top + right) {
        *gx = cols - 1;
        *gy = (i - top) + 1;
    } else if (i < top + right + bottom) {
        *gx = (cols - 2) - (i - top - right);
        *gy = rows - 1;
    } else {
        *gx = 0;
        *gy = (rows - 2) - (i - top - right - bottom);
    }
}

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

static void draw_aul(game_ctx_t *g, int cols, int rows, float pitch) {
    const float w = ((float)(cols - 2) * pitch) + (pitch * 0.4F);
    const float h = ((float)(rows - 2) * pitch) + (pitch * 0.4F);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {176.0F, 146.0F, 106.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(6.0F),
          .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {0.0F, 0.0F}}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "Аул", &s_aul_label);
    }
}

static void draw_road(const tj_run_t *run, int n, int cols, int rows, float pitch, float tile) {
    for (int i = 0; i < n && i < TJ_MAX_PATH; i++) {
        int gx;
        int gy;
        cell_grid(i, cols, rows, &gx, &gy);
        const Clay_Color col = (run->tile_at[i] >= 0) ? cell_color(run->tile_at[i]) : (Clay_Color){92.0F, 86.0F, 78.0F, 255.0F};
        CLAY(MAP_TILE(tile, col, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch), 1)) {}
    }
}

/* Outer build slots (one per road cell, just outside the loop). Filled = marker;
 * empty = clickable button that places the held card. */
static void draw_slots(game_ctx_t *g, tj_run_t *run, int n, int cols, int rows, float pitch, float tile) {
    static uint32_t base = 0U;
    if (base == 0U) {
        base = nt_ui_id("tj_slot");
    }
    const float cx = (float)(cols - 1) * 0.5F;
    const float cy = (float)(rows - 1) * 0.5F;
    for (int i = 0; i < n && i < TJ_MAX_PATH; i++) {
        int gx;
        int gy;
        cell_grid(i, cols, rows, &gx, &gy);
        const int dx = ((float)gx > cx) ? 1 : (((float)gx < cx) ? -1 : 0);
        const int dy = ((float)gy > cy) ? 1 : (((float)gy < cy) ? -1 : 0);
        const float sx = grid_x(gx + dx, cols, pitch);
        const float sy = grid_y(gy + dy, rows, pitch);
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

static void draw_hero(const tj_run_t *run, int n, int cols, int rows, float pitch, float tile) {
    float per = g_config.move_seconds_per_cell;
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
    int x0;
    int y0;
    int x1;
    int y1;
    cell_grid(i0, cols, rows, &x0, &y0);
    cell_grid(i1, cols, rows, &x1, &y1);
    const float hx = grid_x(x0, cols, pitch) + ((grid_x(x1, cols, pitch) - grid_x(x0, cols, pitch)) * frac);
    const float hy = grid_y(y0, rows, pitch) + ((grid_y(y1, rows, pitch) - grid_y(y0, rows, pitch)) * frac);
    CLAY(MAP_TILE(tile * 0.62F, ((Clay_Color){255.0F, 212.0F, 96.0F, 255.0F}), hx, hy, 2)) {}
}

void tj_view_map(game_ctx_t *g, tj_run_t *run) {
    int n = run->path_cells;
    if (n < 4) {
        n = 4;
    }
    if (n % 2 != 0) {
        n -= 1;
    }
    int cols;
    int rows;
    grid_dims(n, &cols, &rows);
    const int maxdim = (cols > rows) ? cols : rows;
    const float pitch = MAP_SIZE / (float)(maxdim + 3);
    const float tile = pitch * 0.86F;

    CLAY({.id = CLAY_ID("map"), .layout = {.sizing = {CLAY_SIZING_FIXED(MAP_SIZE), CLAY_SIZING_FIXED(MAP_SIZE)}}}) {
        draw_aul(g, cols, rows, pitch);
        draw_road(run, n, cols, rows, pitch, tile);
        draw_slots(g, run, n, cols, rows, pitch, tile);
        draw_hero(run, n, cols, rows, pitch, tile);
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
