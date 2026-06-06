#include "view.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"

#include "atlas/nt_atlas.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_label.h"

#include "config.h"
#include "i18n.h"
#include "journal.h"
#include "ui_kit.h"

// #region styles
/* One style per journal kind (TJ_LOG_*). */
static const nt_ui_label_style_t s_log_styles[4] = {
    [TJ_LOG_PLAIN] = {.font_id = 0, .font_size = 18, .color = {184.0F, 172.0F, 150.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
    [TJ_LOG_GOOD] = {.font_id = 0, .font_size = 18, .color = {126.0F, 188.0F, 134.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
    [TJ_LOG_BAD] = {.font_id = 0, .font_size = 18, .color = {224.0F, 108.0F, 76.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
    [TJ_LOG_BIG] = {.font_id = 0, .font_size = 20, .color = {232.0F, 196.0F, 98.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
};

static const nt_ui_label_style_t s_chip = {.font_id = 0, .font_size = 19, .color = {214.0F, 204.0F, 184.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_panel_title = {.font_id = 0, .font_size = 20, .color = {224.0F, 198.0F, 142.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT};
static const nt_ui_label_style_t s_stat = {.font_id = 0, .font_size = 20, .color = {232.0F, 222.0F, 202.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_dim = {.font_id = 0, .font_size = 17, .color = {176.0F, 160.0F, 135.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_card_name = {.font_id = 0, .font_size = 19, .color = {245.0F, 236.0F, 214.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};

#define TJ_PANEL_BG {35.0F, 31.0F, 24.0F, 255.0F}
#define TJ_BAR_BG {31.0F, 28.0F, 22.0F, 255.0F}
#define TJ_CHIP_BG {82.0F, 50.0F, 28.0F, 255.0F}
// #endregion

// #region map geometry (winding trail loop around the central aul, Loop Hero style)
#define MAP_SIZE 720.0F
#define MAP_SIDE_PANELS_W 580.0F
#define MAP_TOP_BOTTOM_H 220.0F

static Clay_Color cell_color(int tile_idx) {
    if (tile_idx < 0 || tile_idx >= g_config.tile_count) {
        return (Clay_Color){184.0F, 177.0F, 160.0F, 255.0F};
    }
    switch (g_config.tiles[tile_idx].kind) {
    case TJ_TILE_SAFE:
        return (Clay_Color){90.0F, 175.0F, 110.0F, 255.0F};
    case TJ_TILE_SUPPORT:
        return (Clay_Color){43.0F, 140.0F, 132.0F, 255.0F};
    case TJ_TILE_CHECK:
        return (Clay_Color){205.0F, 110.0F, 88.0F, 255.0F};
    default:
        return (Clay_Color){184.0F, 177.0F, 160.0F, 255.0F};
    }
}

/* Square tile marker, floating-offset from the map centre. */
#define MAP_TILE(sz, col, ox, oy, z)                                                                                                                                                                   \
    {                                                                                                                                                                                                  \
        .layout = {.sizing = {CLAY_SIZING_FIXED(sz), CLAY_SIZING_FIXED(sz)}}, .backgroundColor = (col), .cornerRadius = CLAY_CORNER_RADIUS(3.0F), .floating = {                                        \
            .attachTo = CLAY_ATTACH_TO_PARENT,                                                                                                                                                         \
            .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},                                                                                   \
            .offset = {(ox), (oy)},                                                                                                                                                                    \
            .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,                                                                                                                                                    \
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
            .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,                                                                                                                                                    \
            .zIndex = (z),                                                                                                                                                                             \
        }                                                                                                                                                                                              \
    }

/* Cell coord -> pixel offset from the map centre (grid centred in the area). */
static float grid_x(int gx, int cols, float pitch) { return ((float)gx - ((float)(cols - 1) * 0.5F)) * pitch; }
static float grid_y(int gy, int rows, float pitch) { return ((float)gy - ((float)(rows - 1) * 0.5F)) * pitch; }

static bool has_region(uint32_t region) { return region != 0U && region != NT_ATLAS_INVALID_REGION; }

/* World-space map render state. The map (ground/road/aul/field/hero) is drawn by
 * the sprite renderer, NOT Clay — so the per-cell count is off the UI element
 * budget. tj_view_world sets these each frame from the "map" viewport bbox. */
static float s_world_m[16];      /* Clay layout-space -> GL world (Y-flip around logical_h). */
static float s_map_cx, s_map_cy; /* viewport centre, logical coords; map offsets are relative to it. */
static float s_map_half_w, s_map_half_h;
static float s_invalid_flash_x, s_invalid_flash_y, s_invalid_flash_size;
static uint8_t s_invalid_flash_frames;

static void map_viewport_size(const game_ctx_t *g, float *w, float *h) {
    const float lw = (g->logical_w > 0.0F) ? g->logical_w : 1280.0F;
    const float lh = (g->logical_h > 0.0F) ? g->logical_h : 720.0F;
    *w = lw - MAP_SIDE_PANELS_W;
    *h = lh - MAP_TOP_BOTTOM_H;
    if (*w < 64.0F) {
        *w = lw;
    }
    if (*h < 64.0F) {
        *h = lh;
    }
}

static bool map_quad_fully_visible(float ox, float oy, float w, float h, float half_w, float half_h) {
    const float hw = w * 0.5F;
    const float hh = h * 0.5F;
    return ox - hw >= -half_w && ox + hw <= half_w && oy - hh >= -half_h && oy + hh <= half_h;
}

static bool map_overlay_visible(const game_ctx_t *g, float ox, float oy, float w, float h) {
    float vw;
    float vh;
    map_viewport_size(g, &vw, &vh);
    return map_quad_fully_visible(ox, oy, w, h, vw * 0.5F, vh * 0.5F);
}

static uint32_t overlay_region_or_white(const game_ctx_t *g, uint32_t region) { return has_region(region) ? region : g->white_region; }

static uint32_t overlay_tint_or(uint32_t region, uint32_t fallback) { return has_region(region) ? 0xFFFFFFFFU : fallback; }

static uint8_t clamp_u8(float v) {
    if (v <= 0.0F) {
        return 0U;
    }
    if (v >= 255.0F) {
        return 255U;
    }
    return (uint8_t)(v + 0.5F);
}

static uint32_t pack_clay_color(Clay_Color c) { return (uint32_t)clamp_u8(c.r) | ((uint32_t)clamp_u8(c.g) << 8) | ((uint32_t)clamp_u8(c.b) << 16) | ((uint32_t)clamp_u8(c.a) << 24); }

/* Emit one w*h quad of `region`, centred at the map centre + (ox, oy), tinted by
 * `color`. Mirrors nt_ui's plain-region image path (ipu/source/origin) so map
 * sprites scale and anchor exactly like UI images, minus the Clay element. */
static void emit_map_quad(game_ctx_t *g, uint32_t region, uint32_t color, float w, float h, float ox, float oy) {
    if (!has_region(region)) {
        return;
    }
    const nt_texture_region_t *r = nt_atlas_get_region(g->atlas, region);
    if (r->vertex_count == 0U) {
        return;
    }
    const float ipu = nt_atlas_get_inverse_pixels_per_unit(g->atlas);
    const float sx = w / ((float)r->source_w * ipu);
    const float sy = h / ((float)r->source_h * ipu);
    const float cx = s_map_cx + ox;
    const float cy = s_map_cy + oy;
    if (!map_quad_fully_visible(ox, oy, w, h, s_map_half_w, s_map_half_h)) {
        return;
    }
    float m[16];
    for (int rr = 0; rr < 4; ++rr) {
        m[rr] = sx * s_world_m[rr];
        m[4 + rr] = -sy * s_world_m[4 + rr];
        m[8 + rr] = s_world_m[8 + rr];
        m[12 + rr] = (cx * s_world_m[rr]) + (cy * s_world_m[4 + rr]) + s_world_m[12 + rr];
    }
    nt_sprite_renderer_emit_region(g->atlas, region, m, r->origin_x, r->origin_y, color, 0U);
}

/* z is now painter order (draw-call order), not a Clay zIndex — kept in the
 * signature so the per-element call sites stay untouched. */
static void map_sprite(game_ctx_t *g, uint32_t region, float w, float h, float ox, float oy, int16_t z) {
    (void)z;
    emit_map_quad(g, region, 0xFFFFFFFFU, w, h, ox, oy);
}

/* Solid colour map marker (events/landmarks/fallbacks) via the white pixel. */
static void map_rect_sprite(game_ctx_t *g, Clay_Color col, float w, float h, float ox, float oy) { emit_map_quad(g, g->white_region, pack_clay_color(col), w, h, ox, oy); }

/* Set the world transform + viewport centre from the CUSTOM-element frame. The
 * walk supplies the layout->GL world_mat4 (Y-flip baked) and the element bbox
 * (the centre viewport), so map sprites line up with the Clay layout exactly. */
static void set_world_from_frame(const nt_ui_custom_frame_t *frame) {
    const Clay_RenderCommand *cmd = (const Clay_RenderCommand *)frame->clay_cmd;
    const Clay_BoundingBox bb = cmd->boundingBox;
    s_map_cx = bb.x + (bb.width * 0.5F);
    s_map_cy = bb.y + (bb.height * 0.5F);
    s_map_half_w = bb.width * 0.5F;
    s_map_half_h = bb.height * 0.5F;
    memcpy(s_world_m, frame->world_mat4, sizeof s_world_m);
}

static void inline_sprite(game_ctx_t *g, uint32_t region, float w, float h) {
    if (!has_region(region)) {
        return;
    }
    const nt_ui_image_style_t img = nt_ui_image_style_defaults();
    const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}}};
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
}

static void floating_center_sprite(game_ctx_t *g, uint32_t region, float w, float h, int16_t z) {
    if (!has_region(region)) {
        return;
    }
    const nt_ui_image_style_t img = nt_ui_image_style_defaults();
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}},
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .zIndex = z},
    };
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
}

static bool is_aul_cell(const tj_run_t *run, int gx, int gy) { return gx >= run->aul_x0 && gx < run->aul_x0 + run->aul_w && gy >= run->aul_y0 && gy < run->aul_y0 + run->aul_h; }

static bool is_path_cell(const tj_run_t *run, int gx, int gy) {
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        if (run->path_gx[i] == gx && run->path_gy[i] == gy) {
            return true;
        }
    }
    return false;
}

static bool is_build_cell(const tj_run_t *run, int gx, int gy) {
    for (int i = 0; i < run->build_count && i < TJ_MAX_BUILD; i++) {
        if (run->build_gx[i] == gx && run->build_gy[i] == gy) {
            return true;
        }
    }
    return false;
}

static bool is_buffer_cell(const tj_run_t *run, int gx, int gy) {
    if (is_aul_cell(run, gx, gy) || is_path_cell(run, gx, gy) || is_build_cell(run, gx, gy)) {
        return false;
    }
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        const int dx = abs((int)run->path_gx[i] - gx);
        const int dy = abs((int)run->path_gy[i] - gy);
        if (dx + dy == 1) {
            return true;
        }
    }
    return false;
}

static uint32_t decor_region_for_cell(const game_ctx_t *g, int gx, int gy) {
    const uint32_t decor[] = {g->decor_dune, g->decor_stones, g->decor_dry_grass, g->decor_tracks, g->decor_bones, g->decor_cracks};
    return decor[(uint32_t)((gx * 17) + (gy * 31)) % (uint32_t)(sizeof decor / sizeof decor[0])];
}

static uint32_t tile_region_for_id(const game_ctx_t *g, const char *id) {
    if (strcmp(id, "saxaul") == 0) {
        return g->tile_saxaul;
    }
    if (strcmp(id, "oasis") == 0) {
        return g->tile_oasis;
    }
    if (strcmp(id, "yurt") == 0) {
        return g->tile_yurt;
    }
    if (strcmp(id, "tamga_stone") == 0) {
        return g->tile_tamga_stone;
    }
    if (strcmp(id, "wolf_track") == 0) {
        return g->tile_wolf_track;
    }
    if (strcmp(id, "mirage") == 0) {
        return g->tile_mirage;
    }
    if (strcmp(id, "storm") == 0) {
        return g->tile_storm;
    }
    if (strcmp(id, "last_tamga") == 0) {
        return g->tile_last_tamga;
    }
    return NT_ATLAS_INVALID_REGION;
}

static uint32_t tile_region_for_index(const game_ctx_t *g, int tile_idx) {
    if (tile_idx < 0 || tile_idx >= g_config.tile_count) {
        return NT_ATLAS_INVALID_REGION;
    }
    return tile_region_for_id(g, g_config.tiles[tile_idx].id);
}

static uint32_t card_art_region_for_id(const game_ctx_t *g, const char *id) {
    if (strcmp(id, "saxaul") == 0) {
        return g->card_art_saxaul_64;
    }
    if (strcmp(id, "oasis") == 0) {
        return g->card_art_oasis_64;
    }
    if (strcmp(id, "yurt") == 0) {
        return g->card_art_yurt_64;
    }
    if (strcmp(id, "tamga_stone") == 0) {
        return g->card_art_tamga_stone_64;
    }
    if (strcmp(id, "wolf_track") == 0) {
        return g->card_art_wolf_track_64;
    }
    if (strcmp(id, "mirage") == 0) {
        return g->card_art_mirage_64;
    }
    if (strcmp(id, "storm") == 0) {
        return g->card_art_storm_64;
    }
    if (strcmp(id, "last_tamga") == 0) {
        return g->card_art_last_tamga_64;
    }
    if (strcmp(id, "well") == 0) {
        return g->card_art_well_64;
    }
    if (strcmp(id, "watchtower") == 0) {
        return g->card_art_watchtower_64;
    }
    return NT_ATLAS_INVALID_REGION;
}

static uint32_t placement_icon_region(const game_ctx_t *g, tj_placement_t placement) {
    switch (placement) {
    case TJ_PLACE_ROADSIDE:
        return g->card_placement_roadside_32;
    case TJ_PLACE_FIELD:
        return g->card_placement_field_32;
    case TJ_PLACE_ROAD:
        return g->card_placement_special_32;
    default:
        return NT_ATLAS_INVALID_REGION;
    }
}
// #endregion

static void hud_chip(game_ctx_t *g, uint32_t icon_region, const char *text) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_GROW(0)},
                     .padding = {12, 14, 0, 0},
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = has_region(icon_region) ? 6 : 0,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_CHIP_BG,
          .cornerRadius = CLAY_CORNER_RADIUS(9.0F)}) {
        inline_sprite(g, icon_region, 24.0F, 24.0F);
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
        hud_chip(g, g->icon_supplies_32, sup);
        hud_chip(g, g->icon_wisdom_32, wis);
        hud_chip(g, g->icon_glory_32, glo);
        hud_spacer();
        hud_chip(g, g->icon_circle_32, circle);
        hud_spacer();
        hud_chip(g, g->icon_day_32, day);
        hud_chip(g, g->icon_stamina_32, sta);
        hud_chip(g, g->icon_speed_32, "x1");
    }
}

static void draw_ground(game_ctx_t *g, const tj_run_t *run, float pitch, float tile) {
    (void)tile;
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    if (!has_region(g->ground_sand_base)) {
        map_rect_sprite(g, (Clay_Color){166.0F, 124.0F, 66.0F, 255.0F}, pitch, pitch, 0.0F, 0.0F);
        return;
    }
    const float ground = pitch * 1.03F;
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < cols; gx++) {
            map_sprite(g, g->ground_sand_base, ground, ground, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch), 0);
        }
    }
}

static void draw_base_decor(game_ctx_t *g, const tj_run_t *run, float pitch, float tile) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    for (int i = 0; i < run->build_count && i < TJ_MAX_BUILD; i++) {
        const int gx = run->build_gx[i];
        const int gy = run->build_gy[i];
        if (run->field_tile[(gy * cols) + gx] >= 0) {
            continue;
        }
        const uint32_t region = decor_region_for_cell(g, gx, gy);
        if (has_region(region)) {
            map_sprite(g, region, tile, tile, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch), 1);
        }
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
    if (has_region(g->aul_ground_2x2) && has_region(g->aul_yurt_small_01) && has_region(g->aul_fire_01)) {
        map_sprite(g, g->aul_ground_2x2, w, h, ox, oy, 2);
        map_sprite(g, g->aul_yurt_small_01, pitch, pitch, ox - (pitch * 0.26F), oy - (pitch * 0.15F), 3);
        if (has_region(g->aul_yurt_small_02)) {
            map_sprite(g, g->aul_yurt_small_02, pitch, pitch, ox + (pitch * 0.28F), oy + (pitch * 0.10F), 3);
        }
        map_sprite(g, g->aul_fire_01, pitch, pitch, ox + (pitch * 0.10F), oy - (pitch * 0.25F), 4);
        return;
    }
    map_rect_sprite(g, (Clay_Color){176.0F, 146.0F, 106.0F, 255.0F}, w, h, ox, oy);
}

static void draw_road_fallback(game_ctx_t *g, const tj_run_t *run, float pitch) {
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
        map_rect_sprite(g, trail, horiz ? pitch : w, horiz ? w : pitch, (xi + xj) * 0.5F, (yi + yj) * 0.5F);
    }
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        map_rect_sprite(g, trail, w, w, grid_x(run->path_gx[i], cols, pitch), grid_y(run->path_gy[i], rows, pitch));
    }
}

static uint32_t road_region_for_path_cell(const game_ctx_t *g, const tj_run_t *run, int i) {
    if (i == 0 && has_region(g->road_entry_aul)) {
        return g->road_entry_aul;
    }
    const int n = run->path_cells;
    const int p = (i + n - 1) % n;
    const int q = (i + 1) % n;
    const int dx0 = (int)run->path_gx[p] - (int)run->path_gx[i];
    const int dy0 = (int)run->path_gy[p] - (int)run->path_gy[i];
    const int dx1 = (int)run->path_gx[q] - (int)run->path_gx[i];
    const int dy1 = (int)run->path_gy[q] - (int)run->path_gy[i];
    const bool n_dir = dy0 < 0 || dy1 < 0;
    const bool e_dir = dx0 > 0 || dx1 > 0;
    const bool s_dir = dy0 > 0 || dy1 > 0;
    const bool w_dir = dx0 < 0 || dx1 < 0;
    if (e_dir && w_dir) {
        return g->road_straight_ew;
    }
    if (n_dir && s_dir) {
        return g->road_straight_ns;
    }
    if (n_dir && e_dir) {
        return g->road_corner_ne;
    }
    if (e_dir && s_dir) {
        return g->road_corner_es;
    }
    if (s_dir && w_dir) {
        return g->road_corner_sw;
    }
    if (w_dir && n_dir) {
        return g->road_corner_wn;
    }
    return NT_ATLAS_INVALID_REGION;
}

static bool can_draw_road_sprites(const game_ctx_t *g, const tj_run_t *run) {
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        if (!has_region(road_region_for_path_cell(g, run, i))) {
            return false;
        }
    }
    return true;
}

/* Road sprites are all-or-fallback so partial art cannot punch holes in the trail. */
static void draw_road(game_ctx_t *g, const tj_run_t *run, float pitch) {
    if (!can_draw_road_sprites(g, run)) {
        draw_road_fallback(g, run, pitch);
        return;
    }
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        map_sprite(g, road_region_for_path_cell(g, run, i), pitch, pitch, grid_x(run->path_gx[i], cols, pitch), grid_y(run->path_gy[i], rows, pitch), 2);
    }
    if (run->cell >= 0 && run->cell < run->path_cells && has_region(g->road_current_highlight)) {
        map_sprite(g, g->road_current_highlight, pitch, pitch, grid_x(run->path_gx[run->cell], cols, pitch), grid_y(run->path_gy[run->cell], rows, pitch), 5);
    }
}

/* Road events (scope on_enter): a kind-coloured marker sitting on the trail. */
static void draw_road_events(game_ctx_t *g, const tj_run_t *run, float pitch) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const float m = pitch * 0.5F;
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        if (run->tile_at[i] >= 0) {
            map_rect_sprite(g, cell_color(run->tile_at[i]), m, m, grid_x(run->path_gx[i], cols, pitch), grid_y(run->path_gy[i], rows, pitch));
        }
    }
}

static void draw_road_buffer(game_ctx_t *g, const tj_run_t *run, float pitch, float tile) {
    const uint32_t variants[] = {g->buffer_edge_stones, g->buffer_packed_sand, g->buffer_stakes, g->buffer_cart_marks};
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const Clay_Color fallback = {92.0F, 78.0F, 58.0F, 155.0F};
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < cols; gx++) {
            if (!is_buffer_cell(run, gx, gy)) {
                continue;
            }
            const uint32_t region = variants[(uint32_t)((gx * 13) + (gy * 19)) % (uint32_t)(sizeof variants / sizeof variants[0])];
            const float x = grid_x(gx, cols, pitch);
            const float y = grid_y(gy, rows, pitch);
            if (has_region(region)) {
                map_sprite(g, region, tile, tile, x, y, 3);
            } else {
                map_rect_sprite(g, fallback, tile, tile, x, y);
            }
        }
    }
}

/* Global desert objects (scope global): functional landmarks out in the field. */
static void draw_global(game_ctx_t *g, const tj_run_t *run, float pitch, float tile) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    for (int i = 0; i < run->global_count && i < TJ_MAX_GLOBAL; i++) {
        const float x = grid_x(run->global_gx[i], cols, pitch);
        const float y = grid_y(run->global_gy[i], rows, pitch);
        map_rect_sprite(g, cell_color(run->global_tile[i]), tile, tile, x, y);
    }
}

/* The ancestor's Last Tamga: a bright gold mark on a road cell, collected on pass. */
static void draw_tamga(game_ctx_t *g, const tj_run_t *run, float pitch) {
    if (run->tamga_cell < 0 || run->tamga_cell >= run->path_cells) {
        return;
    }
    const float x = grid_x(run->path_gx[run->tamga_cell], run->grid_cols, pitch);
    const float y = grid_y(run->path_gy[run->tamga_cell], run->grid_rows, pitch);
    const float m = pitch * 0.56F;
    map_rect_sprite(g, (Clay_Color){255.0F, 226.0F, 130.0F, 255.0F}, m, m, x, y);
}

/* The open buildable field: every free desert cell away from the road. Empty =
 * clickable cell that places the held card (green while a card is held). */
static void draw_slots(game_ctx_t *g, tj_run_t *run, float pitch, float tile) {
    static uint32_t base = 0U;
    if (base == 0U) {
        base = nt_ui_id("tj_slot");
    }
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    for (int i = 0; i < run->build_count && i < TJ_MAX_BUILD; i++) {
        const int sgx = run->build_gx[i];
        const int sgy = run->build_gy[i];
        if (run->field_tile[(sgy * cols) + sgx] >= 0) {
            continue; /* already built here (drawn by draw_field) */
        }
        const float sx = grid_x(sgx, cols, pitch);
        const float sy = grid_y(sgy, rows, pitch);
        if (!map_overlay_visible(g, sx, sy, tile, tile)) {
            continue;
        }
        const bool valid_overlay = run->hand >= 0 && has_region(g->ui_valid_cell_overlay_128);
        const bool hover_overlay = has_region(g->ui_hover_cell_overlay_128);
        const uint32_t idle_region = valid_overlay ? g->ui_valid_cell_overlay_128 : g->white_region;
        const uint32_t hover_region = hover_overlay ? g->ui_hover_cell_overlay_128 : g->white_region;
        const uint32_t idle_tint = valid_overlay ? 0xFFFFFFFFU : ((run->hand >= 0) ? 0x407CA8C4U : 0xFF2A2422U);
        const nt_ui_button_style_t st = {
            .idle = {.atlas = g->atlas, .bg_region = idle_region, .bg_tint = idle_tint, .scale = 1.0F, .opacity = valid_overlay ? 1.0F : 0.30F},
            .hover = {.bg_region = hover_region, .bg_tint = overlay_tint_or(g->ui_hover_cell_overlay_128, 0xD0BCD674U), .scale = hover_overlay ? 1.0F : 1.04F, .opacity = 1.0F},
            .pressed = {.bg_region = idle_region, .bg_tint = idle_tint, .scale = valid_overlay ? 1.0F : 0.98F, .opacity = 1.0F},
            .disabled = {.bg_region = g->white_region, .bg_tint = 0xFF2A2422U, .scale = 1.0F, .opacity = 0.0F},
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

static nt_ui_button_style_t invisible_button(game_ctx_t *g) {
    return (nt_ui_button_style_t){
        .idle = {.atlas = g->atlas, .bg_region = g->white_region, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 0.0F},
        .hover = {.bg_region = g->white_region, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 0.0F},
        .pressed = {.bg_region = g->white_region, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 0.0F},
        .disabled = {.bg_region = g->white_region, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 0.0F},
        .transition_speed = 1.0F,
        .slice9_scale = 1.0F,
    };
}

static void invalid_flash_at(float x, float y, float size) {
    s_invalid_flash_x = x;
    s_invalid_flash_y = y;
    s_invalid_flash_size = size;
    s_invalid_flash_frames = 96U;
}

static void draw_invalid_flash(game_ctx_t *g) {
    if (s_invalid_flash_frames == 0U) {
        return;
    }
    const uint32_t region = overlay_region_or_white(g, g->ui_invalid_cell_overlay_128);
    nt_ui_image_style_t img = nt_ui_image_style_defaults();
    img.color_packed = has_region(g->ui_invalid_cell_overlay_128) ? 0xFF1828FFU : 0xC055358FU;
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(s_invalid_flash_size), CLAY_SIZING_FIXED(s_invalid_flash_size)}},
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                     .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                     .offset = {s_invalid_flash_x, s_invalid_flash_y},
                     .zIndex = 7},
    };
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
    s_invalid_flash_frames--;
}

static void draw_build_catcher(game_ctx_t *g, tj_run_t *run) {
    if (run->hand < 0) {
        return;
    }
    static uint32_t cid = 0U;
    if (cid == 0U) {
        cid = nt_ui_id("tj_nobuild");
    }
    const nt_ui_button_style_t st = invisible_button(g);
    float vw;
    float vh;
    map_viewport_size(g, &vw, &vh);
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(vw), CLAY_SIZING_FIXED(vh)}},
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {0.0F, 0.0F}, .zIndex = 3},
    };
    if (nt_ui_button(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), cid, &st, &decl, true)) {
        tj_journal_push(TJ_LOG_BAD, "Эта карта ставится в подсвеченную клетку за дорогой.");
    }
}

/* Road catchers are built first so road clicks keep their specific feedback. */
static void draw_road_catchers(game_ctx_t *g, tj_run_t *run, float pitch) {
    if (run->hand < 0) {
        return;
    }
    static uint32_t rbase = 0U;
    if (rbase == 0U) {
        rbase = nt_ui_id("tj_roadcatch");
    }
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const nt_ui_button_style_t st = invisible_button(g);
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        const float x = grid_x(run->path_gx[i], cols, pitch);
        const float y = grid_y(run->path_gy[i], rows, pitch);
        if (!map_overlay_visible(g, x, y, pitch, pitch)) {
            continue;
        }
        const Clay_ElementDeclaration decl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(pitch), CLAY_SIZING_FIXED(pitch)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {x, y}, .zIndex = 4},
        };
        if (nt_ui_button(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), rbase + (uint32_t)i, &st, &decl, true)) {
            invalid_flash_at(x, y, pitch);
            tj_journal_push(TJ_LOG_BAD, "Здесь проходит дорога.");
        }
    }
}

/* Persistent player builds in the desert (survive the per-circle road reshuffle). */
static void draw_field(game_ctx_t *g, const tj_run_t *run, float pitch, float tile) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const int n = cols * rows;
    for (int i = 0; i < n && i < TJ_ZONE_CELLS; i++) {
        if (run->field_tile[i] < 0) {
            continue;
        }
        const int gx = i % cols;
        const int gy = i / cols;
        const uint32_t region = tile_region_for_index(g, run->field_tile[i]);
        if (has_region(region)) {
            map_sprite(g, region, tile, tile, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch), 4);
        } else {
            map_rect_sprite(g, cell_color(run->field_tile[i]), tile, tile, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch));
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

static uint32_t hero_region(const game_ctx_t *g, const tj_run_t *run) {
    if (run->phase == TJ_PHASE_AUL_EXIT) {
        return g->hero_wayfarer_idle_s;
    }
    const int n = (run->path_cells > 0) ? run->path_cells : 1;
    int i0 = run->cell % n;
    if (i0 < 0) {
        i0 = 0;
    }
    const int i1 = (i0 + 1) % n;
    const int dx = (int)run->path_gx[i1] - (int)run->path_gx[i0];
    const int dy = (int)run->path_gy[i1] - (int)run->path_gy[i0];
    if (dx > 0) {
        return g->hero_wayfarer_walk_e;
    }
    if (dx < 0) {
        return g->hero_wayfarer_walk_w;
    }
    if (dy < 0) {
        return g->hero_wayfarer_walk_n;
    }
    return g->hero_wayfarer_walk_s;
}

static void draw_hero(game_ctx_t *g, const tj_run_t *run, float pitch) {
    float hx;
    float hy;
    if (run->phase == TJ_PHASE_AUL_EXIT) {
        hero_exit_pos(run, pitch, &hx, &hy);
    } else {
        hero_walk_pos(run, pitch, &hx, &hy);
    }
    const float hs = pitch * 0.40F;
    const uint32_t region = hero_region(g, run);
    if (has_region(region)) {
        map_sprite(g, region, pitch, pitch, hx, hy, 6);
    } else {
        map_rect_sprite(g, (Clay_Color){255.0F, 212.0F, 96.0F, 255.0F}, hs, hs, hx, hy);
    }
}

/* Sandstorm veil over the map: opaque at the circle change, fades to reveal the
 * new (different) path -> the reshuffle reads as a desert storm, not a hard snap. */
static void draw_storm(game_ctx_t *g, const tj_run_t *run) {
    if (run->storm_t <= 0.0F) {
        return;
    }
    const float dur = (g_config.storm_seconds > 0.0F) ? g_config.storm_seconds : 1.3F;
    float a = run->storm_t / dur;
    if (a > 1.0F) {
        a = 1.0F;
    }
    const Clay_Color sand = {196.0F, 172.0F, 122.0F, a * 235.0F};
    float vw;
    float vh;
    map_viewport_size(g, &vw, &vh);
    CLAY(MAP_RECT(vw, vh, sand, 0.0F, 0.0F, 0.0F, 10)) {}
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

/* The map WORLD: ground/road/aul/field/hero drawn by the sprite renderer. Invoked
 * by nt_ui_walk as a CUSTOM render command (NT_UI_CUSTOM_TYPE_GAME) — during the
 * walk the GL viewport + frame state are set up, so a standalone sprite emit is
 * actually visible (a draw before/inside the build is not). It composites under the
 * HUD/panels because later UI commands draw on top. */
static void world_custom_handler(const nt_ui_custom_frame_t *frame, void *userdata) {
    game_ctx_t *g = (game_ctx_t *)userdata;
    const tj_run_t *run = (const tj_run_t *)g->run;
    if (run == NULL || run->grid_cols < 2 || run->grid_rows < 2 || run->path_cells < 1) {
        return;
    }
    set_world_from_frame(frame);
    const int maxdim = (run->grid_cols > run->grid_rows) ? run->grid_cols : run->grid_rows;
    const float pitch = MAP_SIZE / (float)(maxdim + 1);
    const float tile = pitch * 0.66F;
    nt_sprite_renderer_set_material(g->sprite_material);
    draw_ground(g, run, pitch, tile);
    draw_base_decor(g, run, pitch, tile);
    draw_aul(g, run, pitch);
    draw_road(g, run, pitch);
    draw_road_buffer(g, run, pitch, tile);
    draw_road_events(g, run, pitch);
    draw_global(g, run, pitch, tile);
    draw_field(g, run, pitch, tile);
    draw_tamga(g, run, pitch);
    draw_hero(g, run, pitch);
    nt_sprite_renderer_flush();
}

void tj_view_register_world(game_ctx_t *g) { nt_ui_set_custom_handler(g->ui, world_custom_handler, g); }

void tj_view_map(game_ctx_t *g, tj_run_t *run) {
    if (run->grid_cols < 2 || run->grid_rows < 2 || run->path_cells < 1) {
        return; /* loop not generated yet */
    }
    const int maxdim = (run->grid_cols > run->grid_rows) ? run->grid_cols : run->grid_rows;
    const float pitch = MAP_SIZE / (float)(maxdim + 1);
    const float tile = pitch * 0.66F; /* placed objects beside the trail */

    /* Viewport element: grows to fill the centre gap. The map WORLD is drawn by the
     * CUSTOM handler; Clay keeps only the interaction catchers + on-top overlays. */
    CLAY({.id = CLAY_ID("map"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .backgroundColor = {78.0F, 54.0F, 30.0F, 255.0F}, .clip = {.horizontal = true, .vertical = true}}) {
        nt_ui_custom(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), run);
        draw_road_catchers(g, run, pitch);
        draw_build_catcher(g, run);
        draw_slots(g, run, pitch, tile);
        draw_invalid_flash(g);
        draw_storm(g, run);
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
        const tj_heir_def_t *h = &g_config.heirs[run->heir_index];
        const i18n_lang_t lang = i18n_get();
        if (strcmp(h->id, "hunter") == 0) {
            return lang == LANG_TR ? "Avcı" : (lang == LANG_RU ? h->name : "Hunter");
        }
        if (strcmp(h->id, "shaman") == 0) {
            return lang == LANG_TR ? "Kam" : (lang == LANG_RU ? h->name : "Shaman");
        }
        if (strcmp(h->id, "storyteller") == 0) {
            return lang == LANG_TR ? "Anlatıcı" : (lang == LANG_RU ? h->name : "Storyteller");
        }
        return h->name;
    }
    const i18n_lang_t lang = i18n_get();
    return lang == LANG_TR ? "Varis" : (lang == LANG_RU ? "Наследник" : "Heir");
}

static const char *hero_body_label(i18n_lang_t lang) {
    if (lang == LANG_TR) {
        return "Beden";
    }
    return lang == LANG_RU ? "Тело" : "Body";
}

static const char *hero_mind_label(i18n_lang_t lang) {
    if (lang == LANG_TR) {
        return "Akıl";
    }
    return lang == LANG_RU ? "Ум" : "Mind";
}

static const char *hero_spirit_label(i18n_lang_t lang) {
    if (lang == LANG_TR) {
        return "Ruh";
    }
    return lang == LANG_RU ? "Дух" : "Spirit";
}

static const char *hero_stamina_label(i18n_lang_t lang) {
    if (lang == LANG_TR) {
        return "Dayanıklılık";
    }
    return lang == LANG_RU ? "Силы" : "Stamina";
}

static void equipment_slot(game_ctx_t *g, uint32_t slot_region, uint32_t item_region) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(52), CLAY_SIZING_FIXED(52)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {54.0F, 36.0F, 24.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
          .border = {.color = {132.0F, 92.0F, 48.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        floating_center_sprite(g, slot_region, 52.0F, 52.0F, 0);
        floating_center_sprite(g, item_region, 34.0F, 34.0F, 1);
    }
}

void tj_view_hero_panel(game_ctx_t *g, const tj_run_t *run) {
    static char body[32];
    static char mind[32];
    static char spirit[32];
    static char sta[40];
    static char cellinfo[48];
    const i18n_lang_t lang = i18n_get();
    (void)snprintf(body, sizeof body, "%s %d", hero_body_label(lang), run->body);
    (void)snprintf(mind, sizeof mind, "%s %d", hero_mind_label(lang), run->mind);
    (void)snprintf(spirit, sizeof spirit, "%s %d", hero_spirit_label(lang), run->spirit);
    (void)snprintf(sta, sizeof sta, "%s %d", hero_stamina_label(lang), run->stamina);
    if (lang == LANG_TR) {
        (void)snprintf(cellinfo, sizeof cellinfo, "Hücre %d / %d", run->cell + 1, run->path_cells);
    } else if (lang == LANG_RU) {
        (void)snprintf(cellinfo, sizeof cellinfo, "Клетка %d / %d", run->cell + 1, run->path_cells);
    } else {
        (void)snprintf(cellinfo, sizeof cellinfo, "Cell %d / %d", run->cell + 1, run->path_cells);
    }
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 12,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), tj_hero_name(run), &s_panel_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(150)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {70.0F, 48.0F, 32.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(14.0F)}) {
            floating_center_sprite(g, g->hero_wayfarer_panel, 96.0F, 132.0F, 0);
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            equipment_slot(g, g->equip_slot_weapon_01, g->equip_weapon_staff_01);
            equipment_slot(g, g->equip_slot_clothes_01, g->equip_clothes_cloak_01);
            equipment_slot(g, g->equip_slot_tamga_01, g->equip_tamga_charm_01);
            equipment_slot(g, g->equip_slot_tool_01, g->equip_tool_satchel_01);
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), body, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), mind, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), spirit, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), sta, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), cellinfo, &s_dim);
    }
}

static void card_surface(game_ctx_t *g, uint32_t region) {
    if (!has_region(region)) {
        return;
    }
    const nt_ui_image_style_t img = nt_ui_image_style_defaults();
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(112.0F), CLAY_SIZING_FIXED(128.0F)}},
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .zIndex = 0},
    };
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
}

/* Clay floating image at a pixel offset from the parent centre (card art/badges).
 * Cards live in the UI tree, so they stay Clay images — unlike the sprite-rendered
 * map. (This was map_sprite before the map moved to the sprite renderer.) */
static void card_sprite(game_ctx_t *g, uint32_t region, float w, float h, float ox, float oy, int16_t z) {
    if (!has_region(region)) {
        return;
    }
    const nt_ui_image_style_t img = nt_ui_image_style_defaults();
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}},
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {ox, oy}, .zIndex = z},
    };
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
}

static void hand_card(game_ctx_t *g, int tile_index, bool active) {
    const bool has_tile = tile_index >= 0 && tile_index < g_config.tile_count;
    const char *name = has_tile ? g_config.tiles[tile_index].name : "пусто";
    const uint32_t surface = has_tile ? (active ? g->ui_card_selected_96x128 : g->ui_card_playable_96x128) : g->ui_card_back_96x128;
    const uint32_t art = has_tile ? card_art_region_for_id(g, g_config.tiles[tile_index].id) : NT_ATLAS_INVALID_REGION;
    const uint32_t placement = has_tile ? placement_icon_region(g, g_config.tiles[tile_index].placement) : NT_ATLAS_INVALID_REGION;
    const Clay_Color bg = active ? (Clay_Color){38.0F, 76.0F, 64.0F, 255.0F} : (Clay_Color){42.0F, 34.0F, 25.0F, 255.0F};
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(132), CLAY_SIZING_FIXED(128)},
                     .padding = CLAY_PADDING_ALL(10),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 4,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = bg,
          .cornerRadius = CLAY_CORNER_RADIUS(10.0F),
          .border = {.color = active ? (Clay_Color){198.0F, 154.0F, 55.0F, 255.0F} : (Clay_Color){104.0F, 76.0F, 42.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        card_surface(g, surface);
        if (has_tile) {
            card_sprite(g, art, 72.0F, 72.0F, 0.0F, -14.0F, 1);
            card_sprite(g, placement, 24.0F, 24.0F, 38.0F, 40.0F, 2);
            card_sprite(g, active ? g->card_badge_count_32 : NT_ATLAS_INVALID_REGION, 24.0F, 24.0F, -38.0F, -44.0F, 2);
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(90), CLAY_SIZING_FIXED(78)}}}) {}
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), name, active ? &s_card_name : &s_dim);
    }
}

/* Bottom hand: the held card + empty slots. Reward choice floats over the aul. */
void tj_view_card_hand(game_ctx_t *g, tj_run_t *run) {
    const bool has = (run->hand >= 0 && run->hand < g_config.tile_count);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(156)},
                     .padding = CLAY_PADDING_ALL(12),
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 12,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_BAR_BG}) {
        hand_card(g, has ? run->hand : -1, has);
        for (int i = 1; i < 5; i++) {
            hand_card(g, -1, false);
        }
        if (has) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "  кликни подсвеченную клетку за дорогой, чтобы поставить", &s_dim);
        }
    }
}
