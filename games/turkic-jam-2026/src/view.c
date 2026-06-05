#include "view.h"

#include <math.h>
#include <stdio.h>

#include "clay.h"

#include "ui/nt_ui.h"
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

// #region map geometry
#define MAP_SIZE 360.0F
#define MAP_RADIUS 116.0F   /* the road ring */
#define OUTER_RADIUS 154.0F /* roadside build slots */
#define CELL_SIZE 26.0F
#define SLOT_SIZE 24.0F
#define AUL_SIZE 58.0F
#define HERO_SIZE 18.0F

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

/* Floating marker anchored to the map centre, offset (x,y) px. Debug art — a
 * coloured rounded rect. Swap this for sprites/animations later. */
#define MAP_MARKER(w, h, col, radius, ox, oy, z)                                                                                                                                                       \
    {                                                                                                                                                                                                  \
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}}, .backgroundColor = (col), .cornerRadius = CLAY_CORNER_RADIUS(radius), .floating = {                                        \
            .attachTo = CLAY_ATTACH_TO_PARENT,                                                                                                                                                         \
            .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},                                                                                   \
            .offset = {(ox), (oy)},                                                                                                                                                                    \
            .zIndex = (z),                                                                                                                                                                             \
        }                                                                                                                                                                                              \
    }
// #endregion

void tj_view_hud(game_ctx_t *g, const tj_run_t *run) {
    static char l_circle[64];
    static char l_res[96];
    (void)snprintf(l_circle, sizeof l_circle, "Круг %d/%d", run->circle, g_config.laps_to_win);
    const char *hand = (run->hand >= 0 && run->hand < g_config.tile_count) ? g_config.tiles[run->hand].name : "нет";
    (void)snprintf(l_res, sizeof l_res, "Силы %d   Запасы %d   Мудрость %d   Слава %d     Карта: %s", run->stamina, run->supplies, run->wisdom, run->glory, hand);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), l_circle, &TJ_STYLE_HEADING);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), l_res, &TJ_STYLE_BODY);
}

static void draw_ring(const tj_run_t *run, int n, float step, float start) {
    for (int i = 0; i < n && i < TJ_MAX_PATH; i++) {
        const float a = start + (step * (float)i);
        const Clay_Color road_col = (run->tile_at[i] >= 0) ? cell_color(run->tile_at[i]) : (Clay_Color){95.0F, 90.0F, 82.0F, 255.0F};
        CLAY(MAP_MARKER(CELL_SIZE, CELL_SIZE, road_col, CELL_SIZE * 0.5F, MAP_RADIUS * cosf(a), MAP_RADIUS * sinf(a), 1)) {}
        const Clay_Color slot_col = (run->roadside[i] >= 0) ? cell_color(run->roadside[i]) : (Clay_Color){46.0F, 48.0F, 60.0F, 255.0F};
        CLAY(MAP_MARKER(SLOT_SIZE, SLOT_SIZE, slot_col, SLOT_SIZE * 0.25F, OUTER_RADIUS * cosf(a), OUTER_RADIUS * sinf(a), 0)) {}
    }
}

static void draw_hero(const tj_run_t *run, float step, float start) {
    float per = g_config.move_seconds_per_cell;
    float frac = (per > 0.0F) ? (run->move_t / per) : 0.0F;
    if (frac < 0.0F) {
        frac = 0.0F;
    } else if (frac > 1.0F) {
        frac = 1.0F;
    }
    const float ah = start + (step * ((float)run->cell + frac));
    CLAY(MAP_MARKER(HERO_SIZE, HERO_SIZE, ((Clay_Color){255.0F, 210.0F, 90.0F, 255.0F}), HERO_SIZE * 0.5F, MAP_RADIUS * cosf(ah), MAP_RADIUS * sinf(ah), 2)) {}
}

void tj_view_map(game_ctx_t *g, const tj_run_t *run) {
    const int n = run->path_cells > 0 ? run->path_cells : 1;
    const float step = 6.2831853F / (float)n;
    const float start = -1.5707963F; /* first cell at the top */

    CLAY({.id = CLAY_ID("map"), .layout = {.sizing = {CLAY_SIZING_FIXED(MAP_SIZE), CLAY_SIZING_FIXED(MAP_SIZE)}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(AUL_SIZE), CLAY_SIZING_FIXED(AUL_SIZE)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {180.0F, 150.0F, 110.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(AUL_SIZE * 0.3F),
              .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {0.0F, 0.0F}}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "Аул", &s_aul_label);
        }
        draw_ring(run, n, step, start);
        draw_hero(run, step, start);
    }
}

void tj_view_journal(game_ctx_t *g, int max_lines) {
    const int avail = tj_journal_count();
    int shown = avail < max_lines ? avail : max_lines;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(620), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2}}) {
        /* oldest of the window first, newest last (bottom) */
        for (int k = shown - 1; k >= 0; k--) {
            tj_log_kind_t kind = TJ_LOG_PLAIN;
            const char *line = tj_journal_get(k, &kind);
            if (line) {
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), line, &s_log_styles[kind]);
            }
        }
    }
}
