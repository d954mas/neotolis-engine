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
#include "journal.h"
#include "ui_kit.h"

// #region styles
/* One style per journal kind (TJ_LOG_*). */
static const nt_ui_label_style_t s_log_styles[4] = {
    [TJ_LOG_PLAIN] = {.font_id = 0, .font_size = 18, .color = {150.0F, 158.0F, 172.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
    [TJ_LOG_GOOD] = {.font_id = 0, .font_size = 18, .color = {150.0F, 210.0F, 150.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
    [TJ_LOG_BAD] = {.font_id = 0, .font_size = 18, .color = {232.0F, 138.0F, 120.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
    [TJ_LOG_BIG] = {.font_id = 0, .font_size = 20, .color = {255.0F, 210.0F, 120.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT},
};

static const nt_ui_label_style_t s_chip = {.font_id = 0, .font_size = 19, .color = {214.0F, 204.0F, 184.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_panel_title = {.font_id = 0, .font_size = 20, .color = {196.0F, 168.0F, 124.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT};
static const nt_ui_label_style_t s_stat = {.font_id = 0, .font_size = 20, .color = {210.0F, 216.0F, 228.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_dim = {.font_id = 0, .font_size = 17, .color = {130.0F, 138.0F, 152.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_card_name = {.font_id = 0, .font_size = 19, .color = {245.0F, 236.0F, 214.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};

#define TJ_PANEL_BG {16.0F, 19.0F, 30.0F, 255.0F}
#define TJ_BAR_BG {20.0F, 24.0F, 38.0F, 255.0F}
#define TJ_CHIP_BG {34.0F, 40.0F, 58.0F, 255.0F}
// #endregion

// #region map geometry (winding trail loop around the central aul, Loop Hero style)
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

static bool has_region(uint32_t region) { return region != 0U && region != NT_ATLAS_INVALID_REGION; }

/* World-space map render state. The map (ground/road/aul/field/hero) is drawn by
 * the sprite renderer, NOT Clay — so the per-cell count is off the UI element
 * budget. tj_view_world sets these each frame from the "map" viewport bbox. */
static float s_world_m[16];         /* Clay layout-space -> GL world (Y-flip around logical_h). */
static float s_map_cx, s_map_cy;    /* map centre, logical coords (viewport centre + camera pan). */
static float s_map_vw = 360.0F;     /* viewport size (the "map" element bbox). */
static float s_map_vh = 360.0F;     //
static float s_map_pitch = 40.0F;   /* cell size (px); set by the render handler, read by interaction. */
static float s_cam_x = 0.0F;        /* camera pan offset (world px), added to the viewport centre. */
static float s_cam_y = 0.0F;        //
static float s_map_extent = 360.0F; /* maxdim * pitch; bounds the camera pan. */
static int s_map_cols = 8;          /* grid dims captured for click->cell picking. */
static int s_map_rows = 8;          //

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
    s_map_cx = bb.x + (bb.width * 0.5F) + s_cam_x;
    s_map_cy = bb.y + (bb.height * 0.5F) + s_cam_y;
    s_map_vw = bb.width;
    s_map_vh = bb.height;
    memcpy(s_world_m, frame->world_mat4, sizeof s_world_m);
}

/* Pan the camera (world px), clamped so the map can't slide fully off the viewport. */
void tj_view_world_pan(float dx, float dy) {
    const float lim = s_map_extent * 0.5F;
    s_cam_x = fmaxf(-lim, fminf(lim, s_cam_x + dx));
    s_cam_y = fmaxf(-lim, fminf(lim, s_cam_y + dy));
}

/* Logical point -> grid cell (inverse of grid_x/grid_y about the map centre).
 * Returns false if the point is outside the grid. Uses last frame's transform. */
bool tj_view_world_cell_at(float lx, float ly, int *gx, int *gy) {
    if (s_map_pitch <= 0.0F) {
        return false;
    }
    const int cx = (int)lroundf(((lx - s_map_cx) / s_map_pitch) + ((float)(s_map_cols - 1) * 0.5F));
    const int cy = (int)lroundf(((ly - s_map_cy) / s_map_pitch) + ((float)(s_map_rows - 1) * 0.5F));
    if (cx < 0 || cx >= s_map_cols || cy < 0 || cy >= s_map_rows) {
        return false;
    }
    *gx = cx;
    *gy = cy;
    return true;
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

static bool is_path_cell(const tj_run_t *run, int gx, int gy) {
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        if (run->path_gx[i] == gx && run->path_gy[i] == gy) {
            return true;
        }
    }
    return false;
}

static bool is_build_cell(const tj_run_t *run, int gx, int gy) { return tj_run_cell_buildable(run, gx, gy); }

/* No-build road band around the aul (minus the road itself): gets blocker objects. */
static bool is_buffer_cell(const tj_run_t *run, int gx, int gy) {
    const int d = tj_run_dist_to_aul(run, gx, gy);
    if (d < 1 || d > g_config.map_road_band) {
        return false;
    }
    return !is_path_cell(run, gx, gy);
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
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    if (!has_region(g->ground_sand_base)) {
        map_rect_sprite(g, (Clay_Color){54.0F, 48.0F, 36.0F, 255.0F}, (float)cols * pitch, (float)rows * pitch, 0.0F, 0.0F);
        return;
    }
    (void)tile;
    /* Full-pitch tiles, 1px overlap, so the ground reads as one continuous map (no gaps). */
    const float gsz = pitch + 1.0F;
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < cols; gx++) {
            map_sprite(g, g->ground_sand_base, gsz, gsz, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch), 0);
        }
    }
}

static void draw_base_decor(game_ctx_t *g, const tj_run_t *run, float pitch, float tile) {
    (void)tile;
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const float asz = pitch * 0.36F;
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < cols; gx++) {
            /* Sparse small accents on open buildable cells -> field reads as alive but open. */
            if (((gx * 7) + (gy * 5)) % 3 != 0 || !is_build_cell(run, gx, gy)) {
                continue;
            }
            const uint32_t region = decor_region_for_cell(g, gx, gy);
            if (has_region(region)) {
                map_sprite(g, region, asz, asz, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch), 1);
            }
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
    const float w = (float)run->aul_w * pitch;
    const float h = (float)run->aul_h * pitch;
    if (has_region(g->aul_ground_2x2) && has_region(g->aul_yurt_small_01) && has_region(g->aul_fire_01)) {
        map_sprite(g, g->aul_ground_2x2, w, h, ox, oy, 2);
        map_sprite(g, g->aul_yurt_small_01, pitch * 1.25F, pitch * 1.25F, ox - (pitch * 0.30F), oy - (pitch * 0.18F), 3);
        if (has_region(g->aul_yurt_small_02)) {
            map_sprite(g, g->aul_yurt_small_02, pitch * 1.15F, pitch * 1.15F, ox + (pitch * 0.32F), oy + (pitch * 0.12F), 3);
        }
        map_sprite(g, g->aul_fire_01, pitch * 0.95F, pitch * 0.95F, ox + (pitch * 0.12F), oy - (pitch * 0.28F), 4);
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

/* Non-buildable cells beside the road get a solid blocker object (stones/stakes) so
 * "can't build here" reads from the art, not just a tint. Drawn near cell-size so the
 * cell looks occupied. Sand-like variants are excluded on purpose. */
static void draw_road_buffer(game_ctx_t *g, const tj_run_t *run, float pitch, float tile) {
    (void)tile;
    const uint32_t variants[] = {g->buffer_edge_stones, g->buffer_stakes, g->buffer_cart_marks, g->decor_stones};
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const float bsz = pitch * 0.86F;
    const Clay_Color fallback = {120.0F, 104.0F, 78.0F, 235.0F};
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < cols; gx++) {
            if (!is_buffer_cell(run, gx, gy)) {
                continue;
            }
            const uint32_t region = variants[(uint32_t)((gx * 13) + (gy * 19)) % (uint32_t)(sizeof variants / sizeof variants[0])];
            const float x = grid_x(gx, cols, pitch);
            const float y = grid_y(gy, rows, pitch);
            if (has_region(region)) {
                map_sprite(g, region, bsz, bsz, x, y, 3);
            } else {
                map_rect_sprite(g, fallback, bsz, bsz, x, y);
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
        /* Face the way out (toward road cell 0) instead of a fixed idle, so leaving
         * the aul reads as walking, not sliding. */
        const float acx = (float)run->aul_x0 + ((float)(run->aul_w - 1) * 0.5F);
        const float acy = (float)run->aul_y0 + ((float)(run->aul_h - 1) * 0.5F);
        const float dx = (float)run->path_gx[0] - acx;
        const float dy = (float)run->path_gy[0] - acy;
        if (fabsf(dx) >= fabsf(dy)) {
            return (dx >= 0.0F) ? g->hero_wayfarer_walk_e : g->hero_wayfarer_walk_w;
        }
        return (dy < 0.0F) ? g->hero_wayfarer_walk_n : g->hero_wayfarer_walk_s;
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
        /* Hero a bit larger than a cell and lifted so it stands ON the trail, not in it. */
        map_sprite(g, region, pitch * 1.3F, pitch * 1.3F, hx, hy - (pitch * 0.18F), 6);
    } else {
        map_rect_sprite(g, (Clay_Color){255.0F, 212.0F, 96.0F, 255.0F}, hs, hs, hx, hy);
    }
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
    /* Cover the whole viewport (not the old fixed 360) so the reshuffle is fully veiled. */
    CLAY(MAP_RECT(s_map_vw, s_map_vh, sand, 0.0F, 0.0F, 0.0F, 10)) {}
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

/* Buildable cues (sprite, replaces the old Clay slot buttons): a soft green tint on
 * empty buildable cells while a card is held, brighter on the hovered cell. */
/* While holding a card, highlight the cell under the cursor: green = buildable,
 * red = blocked. The open field already reads as buildable, so only the hover cue
 * is needed (a green wash over the whole ~field would be noise). */
static void draw_build_hints(game_ctx_t *g, const tj_run_t *run, float pitch) {
    if (run->hand < 0) {
        return;
    }
    int hgx = -1;
    int hgy = -1;
    if (!tj_view_world_cell_at(g->ptr_x, g->ptr_y, &hgx, &hgy)) {
        return;
    }
    const bool ok = is_build_cell(run, hgx, hgy);
    const Clay_Color tint = ok ? (Clay_Color){132.0F, 236.0F, 150.0F, 165.0F} : (Clay_Color){236.0F, 110.0F, 92.0F, 120.0F};
    map_rect_sprite(g, tint, pitch * 0.92F, pitch * 0.92F, grid_x(hgx, run->grid_cols, pitch), grid_y(hgy, run->grid_rows, pitch));
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
    /* Show ~9 cells across the viewport's short side -> big tiles; larger zones than
     * that scroll (camera pan). A small zone that fits stays centred. */
    float pitch = floorf(fminf(s_map_vw, s_map_vh) / 9.0F);
    if (pitch > floorf(fminf(s_map_vw, s_map_vh) / (float)(maxdim + 1)) && maxdim + 1 < 9) {
        pitch = floorf(fminf(s_map_vw, s_map_vh) / (float)(maxdim + 1)); /* tiny zone: just fit */
    }
    s_map_pitch = pitch;
    s_map_extent = (float)maxdim * pitch;
    s_map_cols = run->grid_cols;
    s_map_rows = run->grid_rows;
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
    draw_build_hints(g, run, pitch);
    draw_tamga(g, run, pitch);
    draw_hero(g, run, pitch);
    nt_sprite_renderer_flush();
}

void tj_view_register_world(game_ctx_t *g) { nt_ui_set_custom_handler(g->ui, world_custom_handler, g); }

void tj_view_map(game_ctx_t *g, tj_run_t *run) {
    if (run->grid_cols < 2 || run->grid_rows < 2 || run->path_cells < 1) {
        return; /* loop not generated yet */
    }
    /* Viewport element: grows to fill the centre gap. The map WORLD (incl. build cues)
     * is drawn by the CUSTOM handler; placement is click->cell (scene). Clay keeps only
     * the on-top overlays (storm veil, reward pack). */
    CLAY({.id = CLAY_ID("map"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .backgroundColor = {78.0F, 54.0F, 30.0F, 255.0F}, .clip = {.horizontal = true, .vertical = true}}) {
        nt_ui_custom(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), run);
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

static void equipment_slot(game_ctx_t *g, uint32_t slot_region, uint32_t item_region) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(52), CLAY_SIZING_FIXED(52)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {38.0F, 43.0F, 58.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
          .border = {.color = {74.0F, 82.0F, 98.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        floating_center_sprite(g, slot_region, 52.0F, 52.0F, 0);
        floating_center_sprite(g, item_region, 34.0F, 34.0F, 1);
    }
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
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(150)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {54.0F, 60.0F, 80.0F, 255.0F},
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
    uint32_t surface = g->ui_card_back_96x128;
    if (has_tile) {
        surface = active ? g->ui_card_selected_96x128 : g->ui_card_playable_96x128;
    }
    const uint32_t art = has_tile ? card_art_region_for_id(g, g_config.tiles[tile_index].id) : NT_ATLAS_INVALID_REGION;
    const uint32_t placement = has_tile ? placement_icon_region(g, g_config.tiles[tile_index].placement) : NT_ATLAS_INVALID_REGION;
    const Clay_Color bg = active ? (Clay_Color){46.0F, 54.0F, 40.0F, 255.0F} : (Clay_Color){26.0F, 30.0F, 44.0F, 255.0F};
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(132), CLAY_SIZING_FIXED(128)},
                     .padding = CLAY_PADDING_ALL(10),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 4,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = bg,
          .cornerRadius = CLAY_CORNER_RADIUS(10.0F),
          .border = {.color = active ? (Clay_Color){150.0F, 210.0F, 150.0F, 255.0F} : (Clay_Color){44.0F, 50.0F, 66.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
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
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "  кликни свободный слот у дороги, чтобы поставить", &s_dim);
        }
    }
}
