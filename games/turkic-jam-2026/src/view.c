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

#include "audio_assets.h"
#include "aul.h"
#include "config.h"
#include "i18n.h"
#include "journal.h"
#include "save.h"
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
static const nt_ui_label_style_t s_die = {.font_id = 0, .font_size = 44, .color = {40.0F, 28.0F, 18.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_die_event = {.font_id = 0, .font_size = 34, .color = {244.0F, 224.0F, 174.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};

#define TJ_PANEL_BG {35.0F, 31.0F, 24.0F, 255.0F}
#define TJ_BAR_BG {31.0F, 28.0F, 22.0F, 255.0F}
#define TJ_CHIP_BG {82.0F, 50.0F, 28.0F, 255.0F}
// #endregion

// #region map geometry (winding trail loop around the central aul, Loop Hero style)
static Clay_Color cell_color(int tile_idx) {
    if (tile_idx < 0 || tile_idx >= g_config.tile_count) {
        return (Clay_Color){104.0F, 92.0F, 72.0F, 255.0F};
    }
    switch (g_config.tiles[tile_idx].kind) {
    case TJ_TILE_SAFE:
        return (Clay_Color){90.0F, 175.0F, 110.0F, 255.0F};
    case TJ_TILE_SUPPORT:
        return (Clay_Color){86.0F, 134.0F, 102.0F, 255.0F};
    case TJ_TILE_CHECK:
        return (Clay_Color){205.0F, 110.0F, 88.0F, 255.0F};
    default:
        return (Clay_Color){140.0F, 124.0F, 96.0F, 255.0F};
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

/* Pan the camera (world px), clamped so the map edge stops at ~the viewport edge
 * (one cell of overscroll), per axis. */
void tj_view_world_pan(float dx, float dy) {
    const float limx = fmaxf(0.0F, (s_map_extent - s_map_vw) * 0.5F) + s_map_pitch;
    const float limy = fmaxf(0.0F, (s_map_extent - s_map_vh) * 0.5F) + s_map_pitch;
    s_cam_x = fmaxf(-limx, fminf(limx, s_cam_x + dx));
    s_cam_y = fmaxf(-limy, fminf(limy, s_cam_y + dy));
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

static int tier_id_index(const char *id, const char *prefix) {
    const size_t n = strlen(prefix);
    if (strncmp(id, prefix, n) != 0) {
        return -1;
    }
    if (id[n] < '1' || id[n] > '3' || id[n + 1] != '\0') {
        return -1;
    }
    return id[n] - '1';
}

static uint32_t merge_tile_region_for_id(const game_ctx_t *g, const char *id) {
    int tier = tier_id_index(id, "war_");
    if (tier >= 0) {
        return g->tile_war[tier];
    }
    tier = tier_id_index(id, "horse_");
    if (tier >= 0) {
        return g->tile_horse[tier];
    }
    tier = tier_id_index(id, "steppe_");
    if (tier >= 0) {
        return g->tile_steppe[tier];
    }
    tier = tier_id_index(id, "home_");
    if (tier >= 0) {
        return g->tile_home[tier];
    }
    tier = tier_id_index(id, "water_");
    if (tier >= 0) {
        return g->tile_water[tier];
    }
    return NT_ATLAS_INVALID_REGION;
}

static uint32_t boss_region_for_circle(const game_ctx_t *g, int circle) {
    if (circle >= g_config.laps_to_win) {
        return g->boss_ring_keeper;
    }
    switch ((circle - 1) % 3) {
    case 0:
        return g->boss_fat;
    case 1:
        return g->boss_swift;
    default:
        return g->boss_fierce;
    }
}

static tj_cell_role_t current_cell_role(const tj_run_t *run) {
    if (run->cell < 0 || run->cell >= run->path_cells || run->cell >= TJ_MAX_PATH) {
        return TJ_CELL_TRAIL;
    }
    return (tj_cell_role_t)run->cell_role[run->cell];
}

static uint32_t die_region_for_sides(const game_ctx_t *g, int sides) {
    switch (sides) {
    case 4:
        return g->die_d[0];
    case 6:
        return g->die_d[1];
    case 8:
        return g->die_d[2];
    case 10:
        return g->die_d[3];
    case 12:
        return g->die_d[4];
    case 20:
        return g->die_d[5];
    default:
        return NT_ATLAS_INVALID_REGION;
    }
}

static uint32_t tile_region_for_id(const game_ctx_t *g, const char *id) {
    const uint32_t merge = merge_tile_region_for_id(g, id);
    if (has_region(merge)) {
        return merge;
    }
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
    const uint32_t merge = merge_tile_region_for_id(g, id);
    if (has_region(merge)) {
        return merge;
    }
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

static const char *pick_lang(const char *en, const char *ru, const char *tr) {
    switch (i18n_get()) {
    case LANG_RU:
        return ru;
    case LANG_TR:
        return tr;
    case LANG_EN:
    default:
        return en;
    }
}

void tj_view_top_hud(game_ctx_t *g, const tj_run_t *run) {
    static char circle[40];
    static char sup[28];
    static char sta[28];
    static char day[24];
    (void)snprintf(circle, sizeof circle, "%s %d/%d", pick_lang("Circle", "Круг", "Dongu"), run->circle, g_config.laps_to_win);
    (void)snprintf(sup, sizeof sup, "%s %d", pick_lang("Supplies", "Припасы", "Azik"), run->supplies);
    (void)snprintf(sta, sizeof sta, "%s %d/%d", pick_lang("HP", "ХП", "CAN"), run->stamina, run->stamina_max);
    (void)snprintf(day, sizeof day, "%s %d", pick_lang("Day", "День", "Gun"), run->day);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64)},
                     .padding = {16, 16, 10, 10},
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 10,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_BAR_BG}) {
        hud_chip(g, g->icon_supplies_32, sup);
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
}

/* What sits on each road cell, drawn ON the trail: enemies/boss as creatures
 * (boss biggest), events/rest as small markers. Trail cells stay clean. */
static void draw_road_events(game_ctx_t *g, const tj_run_t *run, float pitch) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        const float x = grid_x(run->path_gx[i], cols, pitch);
        const float y = grid_y(run->path_gy[i], rows, pitch);
        const tj_cell_role_t role = (tj_cell_role_t)run->cell_role[i];
        if (role == TJ_CELL_FIGHT || role == TJ_CELL_ELITE || role == TJ_CELL_BOSS) {
            float sz = pitch * 0.72F;
            Clay_Color fb = {205.0F, 110.0F, 88.0F, 255.0F};
            if (role == TJ_CELL_ELITE) {
                sz = pitch * 0.9F;
                fb = (Clay_Color){226.0F, 150.0F, 70.0F, 255.0F};
            } else if (role == TJ_CELL_BOSS) {
                sz = pitch * 1.06F;
                fb = (Clay_Color){214.0F, 72.0F, 70.0F, 255.0F};
            }
            const uint32_t boss_region = (role == TJ_CELL_BOSS) ? boss_region_for_circle(g, run->circle) : NT_ATLAS_INVALID_REGION;
            const uint32_t region = has_region(boss_region) ? boss_region : tile_region_for_index(g, run->tile_at[i]);
            if (has_region(region)) {
                map_sprite(g, region, sz, sz, x, y, 6);
            } else {
                map_rect_sprite(g, fb, sz, sz, x, y);
            }
        } else if (role == TJ_CELL_EVENT) {
            map_rect_sprite(g, (Clay_Color){92.0F, 172.0F, 202.0F, 255.0F}, pitch * 0.5F, pitch * 0.5F, x, y);
        } else if (role == TJ_CELL_REST) {
            map_rect_sprite(g, (Clay_Color){120.0F, 190.0F, 110.0F, 255.0F}, pitch * 0.42F, pitch * 0.42F, x, y);
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
        float ts = tile;
        if (i == run->fx_cell && run->fx_cell_t > 0.0F) { /* place/merge pop: settle from big -> normal */
            const float dur = (run->fx_cell_mag > 0.5F) ? 0.45F : 0.30F;
            float p = 1.0F - (run->fx_cell_t / dur);
            p = (p < 0.0F) ? 0.0F : ((p > 1.0F) ? 1.0F : p);
            ts = tile * (1.0F + (run->fx_cell_mag * (1.0F - (p * (2.0F - p))))); /* 1-(ease_out_quad) */
        }
        const uint32_t region = tile_region_for_index(g, run->field_tile[i]);
        if (has_region(region)) {
            map_sprite(g, region, ts, ts, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch), 4);
        } else {
            map_rect_sprite(g, cell_color(run->field_tile[i]), ts, ts, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch));
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
    if (run->phase == TJ_PHASE_AUL_EXIT || run->phase == TJ_PHASE_AUL_READY) {
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
    if (run->phase == TJ_PHASE_AUL_EXIT || run->phase == TJ_PHASE_AUL_READY) {
        hero_exit_pos(run, pitch, &hx, &hy);
    } else {
        hero_walk_pos(run, pitch, &hx, &hy);
    }
    const float hs = pitch * 0.40F;
    /* Walk bob: a small up-down spring while moving so the hero reads as alive. */
    float bob = 0.0F;
    if (run->phase == TJ_PHASE_WALK && !run->in_combat && !run->in_event) {
        bob = -fabsf(sinf(run->move_t * 6.2832F)) * (pitch * 0.08F);
    }
    const uint32_t region = hero_region(g, run);
    if (has_region(region)) {
        /* Hero a bit larger than a cell and lifted so it stands ON the trail, not in it. */
        map_sprite(g, region, pitch * 1.3F, pitch * 1.3F, hx, hy - (pitch * 0.18F) + bob, 6);
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

/* One clickable reward card (surface + art + name). nt_ui_button_begin/end lets the
 * whole card be the hit target with art layered inside. */
static void pack_card(game_ctx_t *g, tj_run_t *run, int i) {
    const int ti = run->pack_offer[0][i];
    const bool has = ti >= 0 && ti < g_config.tile_count;
    const char *name = has ? g_config.tiles[ti].name : "-";
    const uint32_t art = has ? card_art_region_for_id(g, g_config.tiles[ti].id) : NT_ATLAS_INVALID_REGION;
    static const char *ids[3] = {"tj_packcard0", "tj_packcard1", "tj_packcard2"};
    const nt_ui_button_style_t st = {
        .idle = {.atlas = g->atlas, .bg_region = g->ui_card_playable_96x128, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F},
        .hover = {.bg_region = g->ui_card_selected_96x128, .bg_tint = 0xFFFFFFFFU, .scale = 1.06F, .opacity = 1.0F},
        .pressed = {.bg_region = g->ui_card_selected_96x128, .bg_tint = 0xFFFFFFFFU, .scale = 0.97F, .opacity = 1.0F},
        .disabled = {.bg_region = g->ui_card_playable_96x128, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 0.5F},
        .transition_speed = 14.0F,
        .hit_padding_lrtb = {4, 4, 4, 4},
        .slice9_scale = 1.0F,
    };
    const Clay_ElementDeclaration decl = {
        .layout =
            {
                .sizing = {CLAY_SIZING_FIXED(128), CLAY_SIZING_FIXED(168)},
                .padding = CLAY_PADDING_ALL(10),
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap = 8,
                .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
            },
    };
    /* Non-floating art (Clay sequences it per call) + explicit per-card id so two
     * offers of the same tile can't collide on a Clay element ID. */
    nt_ui_button_begin(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), nt_ui_id(ids[i % 3]), &st, &decl, true);
    inline_sprite(g, art, 88.0F, 88.0F);
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), name, &s_card_name);
    if (nt_ui_button_end(g->ui)) {
        tj_run_choose_card(run, i); /* picks the card and closes the modal (pack_open -> false) */
    }
}

/* Reward chooser as a real modal: a full-screen dim scrim (blocks the map visually;
 * the scene gates map input while pack_open) + a centred panel of clickable cards.
 * Public so the scene draws it at the root, ABOVE the clipped map viewport. */
void tj_view_pack_overlay(game_ctx_t *g, tj_run_t *run) {
    if (!run->pack_open || run->packs <= 0) {
        return;
    }
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .zIndex = 50},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {18.0F, 13.0F, 8.0F, 200.0F}}) {
        CLAY({.layout =
                  {
                      .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)},
                      .padding = CLAY_PADDING_ALL(24),
                      .layoutDirection = CLAY_TOP_TO_BOTTOM,
                      .childGap = 16,
                      .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
                  },
              .backgroundColor = {42.0F, 34.0F, 25.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(16.0F),
              .border = {.color = {198.0F, 154.0F, 55.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("Road gift - choose a card", "Дар пути - выбери карту", "Yol hediyesi - kart sec"), &s_panel_title);
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 14}}) {
                for (int i = 0; i < 3; i++) {
                    pack_card(g, run, i);
                }
            }
        }
    }
}

/* True if the cell's quad falls within the viewport (+1 cell margin) — culls the
 * off-screen overlays on the big scrolling map. */
static bool cell_in_view(int gx, int gy, int cols, int rows, float pitch) {
    const float x = grid_x(gx, cols, pitch);
    const float y = grid_y(gy, rows, pitch);
    const float mx = (s_map_vw * 0.5F) + pitch;
    const float my = (s_map_vh * 0.5F) + pitch;
    return x > -mx && x < mx && y > -my && y < my;
}

/* Draw a cell overlay sprite (ornate frame), falling back to a tint if the region
 * is missing so the cue never disappears. */
static void cell_overlay(game_ctx_t *g, uint32_t region, Clay_Color fallback, float pitch, float ox, float oy) {
    if (has_region(region)) {
        map_sprite(g, region, pitch, pitch, ox, oy, 7);
    } else {
        map_rect_sprite(g, fallback, pitch * 0.92F, pitch * 0.92F, ox, oy);
    }
}

/* Placement cue while a card is held: a faint green frame on EVERY buildable cell
 * (so "where do I build?" reads instantly), plus a stronger frame under the cursor. */
static void draw_build_hints(game_ctx_t *g, const tj_run_t *run, float pitch) {
    if (run->hand_count <= 0) {
        return;
    }
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < cols; gx++) {
            if (!is_build_cell(run, gx, gy) || !cell_in_view(gx, gy, cols, rows, pitch)) {
                continue;
            }
            cell_overlay(g, g->ui_valid_cell_overlay_128, (Clay_Color){150.0F, 230.0F, 160.0F, 90.0F}, pitch, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch));
        }
    }
    int hgx = -1;
    int hgy = -1;
    if (!tj_view_world_cell_at(g->ptr_x, g->ptr_y, &hgx, &hgy) || !cell_in_view(hgx, hgy, cols, rows, pitch)) {
        return;
    }
    const bool ok = is_build_cell(run, hgx, hgy);
    const uint32_t region = ok ? g->ui_hover_cell_overlay_128 : g->ui_invalid_cell_overlay_128;
    const Clay_Color fb = ok ? (Clay_Color){160.0F, 255.0F, 175.0F, 220.0F} : (Clay_Color){236.0F, 110.0F, 92.0F, 180.0F};
    cell_overlay(g, region, fb, pitch, grid_x(hgx, cols, pitch), grid_y(hgy, rows, pitch));
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
    /* Big tiles: ~6 cells across the viewport short side; the large zone scrolls
     * (camera pan/drag). A small zone that already fits gets the larger fit pitch. */
    const float vmin = fminf(s_map_vw, s_map_vh);
    const float pitch = fmaxf(floorf(vmin / 6.0F), floorf(vmin / (float)(maxdim + 1)));
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
    /* Viewport element: grows to fill the centre gap, clipped so the big scrolling map
     * stays inside it. The map WORLD is the CUSTOM handler; the storm veil clips with it.
     * The reward chooser is a separate top-level modal (tj_view_pack_overlay), NOT here,
     * so the clip can't hide it and it sits above everything. */
    CLAY({.id = CLAY_ID("map"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .backgroundColor = {78.0F, 54.0F, 30.0F, 255.0F}, .clip = {.horizontal = true, .vertical = true}}) {
        nt_ui_custom(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), run);
        draw_storm(run);
    }
}

void tj_view_log(game_ctx_t *g, int max_lines) {
    const int avail = tj_journal_count();
    const int shown = avail < max_lines ? avail : max_lines;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(14), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}, .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("Chronicle", "Летопись", "Gunluk"), &s_panel_title);
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
    return pick_lang("Heir", "Наследник", "Varis");
}

/* Horizontal fill bar (HP / enemy HP): dark track + colored fill by fraction. */
static void hp_bar(float frac, Clay_Color fill, float total) {
    if (frac < 0.0F) {
        frac = 0.0F;
    }
    if (frac > 1.0F) {
        frac = 1.0F;
    }
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(total), CLAY_SIZING_FIXED(14)}}, .backgroundColor = {40.0F, 28.0F, 20.0F, 255.0F}, .cornerRadius = CLAY_CORNER_RADIUS(4.0F)}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(total * frac), CLAY_SIZING_FIXED(14)}}, .backgroundColor = fill, .cornerRadius = CLAY_CORNER_RADIUS(4.0F)}) {}
    }
}

void tj_view_hero_panel(game_ctx_t *g, const tj_run_t *run) {
    static char force[40];
    static char speed[40];
    static char vigor[40];
    static char sta[40];
    static char enemyline[48];
    static char cellinfo[40];
    (void)snprintf(force, sizeof force, "%s %d", pick_lang("Force", "Сила", "Kuvvet"), run->body + run->bonus_force);
    (void)snprintf(speed, sizeof speed, "%s %d", pick_lang("Speed", "Скорость", "Hiz"), run->mind + run->bonus_speed);
    (void)snprintf(vigor, sizeof vigor, "%s %d", pick_lang("Vigor", "Выносливость", "Dayanma"), run->spirit + run->bonus_vigor);
    (void)snprintf(sta, sizeof sta, "%s %d/%d", pick_lang("HP", "ХП", "CAN"), run->stamina, run->stamina_max);
    (void)snprintf(cellinfo, sizeof cellinfo, "%s %d / %d", pick_lang("Cell", "Клетка", "Hucre"), run->cell + 1, run->path_cells);
    const int smax = (run->stamina_max > 0) ? run->stamina_max : 1;
    const bool fighting = run->in_combat && run->combat_tile >= 0 && run->combat_tile < g_config.tile_count;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 10,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), tj_hero_name(run), &s_panel_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(150)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {70.0F, 48.0F, 32.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(14.0F)}) {
            floating_center_sprite(g, g->hero_wayfarer_panel, 96.0F, 132.0F, 0);
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), sta, &s_stat);
        hp_bar((float)run->stamina / (float)smax, (Clay_Color){120.0F, 180.0F, 90.0F, 255.0F}, 250.0F);
        if (fighting) {
            const int emax = (run->combat_enemy_max > 0) ? run->combat_enemy_max : 1;
            (void)snprintf(enemyline, sizeof enemyline, "%s", run->combat_label[0] ? run->combat_label : g_config.tiles[run->combat_tile].name);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), enemyline, &s_stat);
            hp_bar((float)run->combat_enemy_hp / (float)emax, (Clay_Color){200.0F, 80.0F, 70.0F, 255.0F}, 250.0F);
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), force, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), speed, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), vigor, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), cellinfo, &s_dim);
    }
}

/* Right panel when the run is over: result + aul upgrades + "Новый забег".
 * Returns true if the player pressed "Новый забег". */
bool tj_view_aul_panel(game_ctx_t *g, const tj_run_t *run) {
    static char head[48];
    static char res[40];
    static char sup[40];
    static char u0[48];
    static char u1[48];
    static char u2[48];
    static char u3[48];
    (void)snprintf(head, sizeof head, "%s", run->won ? "Кольцо разорвано!" : "Наследник пал");
    (void)snprintf(res, sizeof res, "Дошёл до круга %d", run->circle);
    (void)snprintf(sup, sizeof sup, "Аул · припасы %d", g_aul.supplies);
    (void)snprintf(u0, sizeof u0, "+Сила  ур.%d  (%d)", g_aul.up_force, tj_aul_upgrade_cost(0));
    (void)snprintf(u1, sizeof u1, "+Скорость  ур.%d  (%d)", g_aul.up_speed, tj_aul_upgrade_cost(1));
    (void)snprintf(u2, sizeof u2, "+Выносл.  ур.%d  (%d)", g_aul.up_vigor, tj_aul_upgrade_cost(2));
    (void)snprintf(u3, sizeof u3, "+Наследие  ур.%d  (%d)", g_aul.up_keep, tj_aul_upgrade_cost(3));
    bool newrun = false;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 8,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), head, &s_panel_title);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), res, &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), sup, &s_dim);
        if (tj_button(g, "aul_f", u0, 258, 44, TJ_BTN_SECONDARY)) {
            tj_aul_upgrade(0);
        }
        if (tj_button(g, "aul_s", u1, 258, 44, TJ_BTN_SECONDARY)) {
            tj_aul_upgrade(1);
        }
        if (tj_button(g, "aul_v", u2, 258, 44, TJ_BTN_SECONDARY)) {
            tj_aul_upgrade(2);
        }
        if (tj_button(g, "aul_k", u3, 258, 44, TJ_BTN_SECONDARY)) {
            tj_aul_upgrade(3);
        }
        if (tj_button(g, "aul_new", "Отправить путника", 258, 58, TJ_BTN_PRIMARY)) {
            newrun = true;
        }
    }
    return newrun;
}

/* Fullscreen run-over veil (fades in via death_t) + big verdict. Passthrough so the
 * aul panel underneath stays clickable. */
void tj_view_death_overlay(game_ctx_t *g, const tj_run_t *run) {
    const bool won = run->won;
    float a = run->death_t * 140.0F;
    const float amax = won ? 110.0F : 140.0F;
    if (a > amax) {
        a = amax;
    }
    const Clay_Color veil = won ? (Clay_Color){18.0F, 40.0F, 18.0F, a} : (Clay_Color){92.0F, 16.0F, 12.0F, a};
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                       .zIndex = 45,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = veil}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), won ? "ПОБЕДА" : "СМЕРТЬ", &s_die);
    }
}

/* One fighter line in the corner combat window: small portrait + name + HP bar + value/dmg. */
static void compact_fighter(game_ctx_t *g, uint32_t sprite, const char *name, float frac, Clay_Color hpcol, const char *hpval, const char *dmg, bool show_dmg, const nt_ui_label_style_t *dmgstyle,
                            float punch) {
    const float psz = 44.0F * (1.0F + (0.20F * punch)); /* portrait punches outward on a fresh hit */
    const Clay_Color box = {60.0F + (40.0F * punch), 42.0F + (18.0F * punch), 28.0F, 255.0F};
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(50), CLAY_SIZING_FIXED(54)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = box,
              .cornerRadius = CLAY_CORNER_RADIUS(8.0F)}) {
            floating_center_sprite(g, sprite, psz, psz, 0);
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 3, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), name, &s_stat);
            hp_bar(frac, hpcol, 200.0F);
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), hpval, &s_dim);
                if (show_dmg) {
                    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), dmg, dmgstyle);
                }
            }
        }
    }
}

/* Centered dice-event window: die rolls (spins then locks), then stat x multiplier
 * = effect vs DC, then success/fail — revealed over run->event_t. */
static void event_overlay(game_ctx_t *g, const tj_run_t *run) {
    static char dnum[8];
    static char mathline[56];
    static char resline[40];
    const float dur = (g_config.event_reveal_seconds > 0.1F) ? g_config.event_reveal_seconds : 1.5F;
    const float t = run->event_t;
    const bool spinning = t < dur * 0.45F;
    const bool show_math = t >= dur * 0.45F;
    const bool show_res = t >= dur * 0.72F;
    int shown = run->ev_roll;
    if (spinning && run->ev_die > 0) {
        shown = ((int)(t * 53.0F) % run->ev_die) + 1; /* cycling face while rolling */
    }
    (void)snprintf(dnum, sizeof dnum, "%d", shown);
    const float mult = 1.0F + (g_config.event_dice_coeff * (float)run->ev_roll);
    const int eff = (int)((float)run->ev_stat * mult);
    (void)snprintf(mathline, sizeof mathline, "%s %d  x%.2f  = %d", run->ev_statname, run->ev_stat, (double)mult, eff);
    (void)snprintf(resline, sizeof resline, "DC %d  ->  %s", run->ev_dc, run->ev_pass ? "УСПЕХ" : "ПРОВАЛ");
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .zIndex = 60},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {16.0F, 12.0F, 8.0F, 150.0F}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(460), CLAY_SIZING_FIT(0)},
                         .padding = CLAY_PADDING_ALL(22),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 14,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {40.0F, 34.0F, 24.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(16.0F),
              .border = {.color = {198.0F, 154.0F, 55.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), run->ev_name, &s_panel_title);
            CLAY(
                {.layout = {.sizing = {CLAY_SIZING_FIXED(108), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                 .backgroundColor = {24.0F, 18.0F, 12.0F, 0.0F}}) {
                inline_sprite(g, die_region_for_sides(g, run->ev_die), 88.0F, 88.0F);
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), dnum, &s_die_event);
            }
            if (show_math) {
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), mathline, &s_stat);
            }
            if (show_res) {
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), resline, run->ev_pass ? &s_log_styles[TJ_LOG_GOOD] : &s_log_styles[TJ_LOG_BAD]);
            }
        }
    }
}

/* Centered combat window: hero vs enemy, HP bars, floating damage numbers. */
void tj_view_action_overlay(game_ctx_t *g, const tj_run_t *run) {
    if (run->in_event) {
        event_overlay(g, run);
        return;
    }
    if (!run->in_combat) {
        return;
    }
    static char hpv[24];
    static char ehpv[24];
    static char hdmg[12];
    static char edmg[12];
    const int smax = (run->stamina_max > 0) ? run->stamina_max : 1;
    const int emax = (run->combat_enemy_max > 0) ? run->combat_enemy_max : 1;
    const int ehp = (run->combat_enemy_hp < 0) ? 0 : run->combat_enemy_hp;
    const char *ename = (run->combat_tile >= 0 && run->combat_tile < g_config.tile_count) ? g_config.tiles[run->combat_tile].name : "Враг";
    (void)snprintf(hpv, sizeof hpv, "%d/%d", run->stamina, smax);
    (void)snprintf(ehpv, sizeof ehpv, "%d/%d", ehp, emax);
    (void)snprintf(hdmg, sizeof hdmg, "-%d", run->fx_hero_dmg);
    (void)snprintf(edmg, sizeof edmg, "-%d", run->fx_enemy_dmg);
    const uint32_t boss_region = (current_cell_role(run) == TJ_CELL_BOSS) ? boss_region_for_circle(g, run->circle) : NT_ATLAS_INVALID_REGION;
    const uint32_t eregion = has_region(boss_region) ? boss_region : tile_region_for_index(g, run->combat_tile);
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_BOTTOM, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM},
                       .offset = {-14.0F, -172.0F}, /* bottom-right corner, just above the hand bar */
                       .zIndex = 58,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH}, /* don't block merging on the field */
          .layout = {.sizing = {CLAY_SIZING_FIXED(330), CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(12),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 8,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {42.0F, 30.0F, 22.0F, 248.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(14.0F),
          .border = {.color = {200.0F, 90.0F, 70.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), run->combat_label[0] ? run->combat_label : "Бой", &s_panel_title);
        const float hero_punch = (run->fx_hero_t > 0.0F) ? (run->fx_hero_t / 0.6F) : 0.0F; /* portrait punch on a fresh hit */
        const float enemy_punch = (run->fx_enemy_t > 0.0F) ? (run->fx_enemy_t / 0.6F) : 0.0F;
        compact_fighter(g, g->hero_wayfarer_panel, pick_lang("Hero", "Чемпион", "Kahraman"), (float)run->stamina / (float)smax, (Clay_Color){120.0F, 180.0F, 90.0F, 255.0F}, hpv, hdmg,
                        run->fx_hero_t > 0.0F && run->fx_hero_dmg > 0, &s_log_styles[TJ_LOG_BAD], hero_punch);
        compact_fighter(g, has_region(eregion) ? eregion : 0U, ename, (float)ehp / (float)emax, (Clay_Color){200.0F, 80.0F, 70.0F, 255.0F}, ehpv, edmg, run->fx_enemy_t > 0.0F && run->fx_enemy_dmg > 0,
                        &s_log_styles[TJ_LOG_GOOD], enemy_punch);
    }
}

/* Hand fan layout (screen/logical coords). The fan is pinned to the viewport
 * bottom and centred, so it tracks window resizes instead of drifting off. */
#define FAN_CARD_W 100.0F
#define FAN_CARD_H 124.0F
#define FAN_STEP 74.0F      /* per-card step (overlapping fan) */
#define FAN_LIFT 30.0F      /* hovered/selected card rises this much */
#define FAN_BOTTOM 164.0F   /* resting top-left y measured up from the viewport bottom */
#define FAN_LEFT_MIN 292.0F /* never slide left under the pouch controls */

/* Fan base = top-left of card 0, derived from the live viewport + hand size.
 * Centred horizontally, clamped to clear the pouch, pinned above the bottom bar. */
static void fan_base(const game_ctx_t *g, int count, float *x0, float *y) {
    const float total = (count > 1) ? ((float)(count - 1) * FAN_STEP + FAN_CARD_W) : FAN_CARD_W;
    float x = (g->logical_w - total) * 0.5F;
    if (x < FAN_LEFT_MIN) {
        x = FAN_LEFT_MIN;
    }
    *x0 = x;
    *y = g->logical_h - FAN_BOTTOM;
}

/* Screen centre of hand card `idx` (the drag arrow's origin). */
static void fan_card_center(const game_ctx_t *g, const tj_run_t *run, int idx, float *cx, float *cy) {
    float x0;
    float y;
    fan_base(g, run->hand_count, &x0, &y);
    *cx = x0 + ((float)idx * FAN_STEP) + (FAN_CARD_W * 0.5F);
    *cy = y + (FAN_CARD_H * 0.5F);
}

/* Hand index under a logical point, or -1. Topmost (rightmost) card wins. */
int tj_view_hand_index_at(const game_ctx_t *g, const tj_run_t *run, float lx, float ly) {
    float x0;
    float y;
    fan_base(g, run->hand_count, &x0, &y);
    if (ly < y - FAN_LIFT || ly > y + FAN_CARD_H) {
        return -1;
    }
    for (int i = run->hand_count - 1; i >= 0; i--) {
        const float cx = x0 + ((float)i * FAN_STEP) + (FAN_CARD_W * 0.5F);
        if (lx >= cx - (FAN_CARD_W * 0.5F) && lx <= cx + (FAN_CARD_W * 0.5F)) {
            return i;
        }
    }
    return -1;
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

/* One card at an absolute screen position (floating to root) so the fan can overlap
 * and the hovered/dragged card can lift out. */
static void draw_fan_card(game_ctx_t *g, int tile, float x, float y, bool active, const nt_ui_transform_t *xf) {
    const bool has = tile >= 0 && tile < g_config.tile_count;
    const uint32_t surface = has ? (active ? g->ui_card_selected_96x128 : g->ui_card_playable_96x128) : g->ui_card_back_96x128;
    const uint32_t art = has ? card_art_region_for_id(g, g_config.tiles[tile].id) : NT_ATLAS_INVALID_REGION;
    const Clay_Color bg = active ? (Clay_Color){82.0F, 68.0F, 38.0F, 255.0F} : (Clay_Color){46.0F, 36.0F, 26.0F, 255.0F};
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(FAN_CARD_W), CLAY_SIZING_FIXED(FAN_CARD_H)},
                     .padding = CLAY_PADDING_ALL(6),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_BOTTOM}},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}, .offset = {x, y}, .zIndex = active ? 36 : 20},
          .backgroundColor = bg,
          .cornerRadius = CLAY_CORNER_RADIUS(10.0F),
          .border = {.color = active ? (Clay_Color){198.0F, 154.0F, 55.0F, 255.0F} : (Clay_Color){104.0F, 76.0F, 42.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)},
          .userData = (void *)NT_UI_DATA_XFORM(0U, xf, 1.0F)}) {
        card_sprite(g, surface, FAN_CARD_W - 8.0F, FAN_CARD_H - 8.0F, 0.0F, 0.0F, 0);
        if (has) {
            card_sprite(g, art, 58.0F, 58.0F, 0.0F, -18.0F, 1);
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), has ? g_config.tiles[tile].name : "", active ? &s_card_name : &s_dim);
    }
}

/* Filled dot at a screen position (the drag arrow's dotted trail). */
static void floating_screen_dot(float cx, float cy, float d, Clay_Color col) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(d), CLAY_SIZING_FIXED(d)}},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_TOP}, .offset = {cx, cy}, .zIndex = 38},
          .backgroundColor = col,
          .cornerRadius = CLAY_CORNER_RADIUS(d * 0.5F)}) {}
}

/* While dragging a card: a dotted arrow from its fan slot to the cursor, plus the
 * card riding under the cursor (StS-style targeting). */
void tj_view_drag_overlay(game_ctx_t *g, const tj_run_t *run, int drag_idx) {
    if (drag_idx < 0 || drag_idx >= run->hand_count) {
        return;
    }
    float ox = 0.0F;
    float oy = 0.0F;
    fan_card_center(g, run, drag_idx, &ox, &oy);
    const float px = g->ptr_x;
    const float py = g->ptr_y;
    const int dots = 9;
    for (int k = 1; k <= dots; k++) {
        const float t = (float)k / (float)(dots + 1);
        floating_screen_dot(ox + ((px - ox) * t), oy + ((py - oy) * t), 6.0F + (8.0F * t), (Clay_Color){250.0F, 222.0F, 120.0F, 235.0F});
    }
    static nt_ui_transform_t s_ghost_xf;
    s_ghost_xf = nt_ui_transform_defaults();
    s_ghost_xf.rotation_z = 0.05F; /* slight tilt while dragging */
    s_ghost_xf.scale_x = 1.06F;
    s_ghost_xf.scale_y = 1.06F;
    draw_fan_card(g, run->hand_cards[drag_idx], px - (FAN_CARD_W * 0.5F), py - (FAN_CARD_H * 0.6F), true, &s_ghost_xf);
}

/* Bottom hand: pouch button + a fan of held cards (drag one onto a field cell).
 * `drag_idx` is the card currently being dragged (drawn under the cursor instead). */
void tj_view_card_hand(game_ctx_t *g, tj_run_t *run, int drag_idx, bool tutorial) {
    static char pouchlbl[40];
    (void)snprintf(pouchlbl, sizeof pouchlbl, "%s (%d)", pick_lang("Pouch", "Мешочек", "Torba"), run->pouch);
    const int hover = (drag_idx >= 0) ? -1 : tj_view_hand_index_at(g, run, g->ptr_x, g->ptr_y);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(156)},
                     .padding = CLAY_PADDING_ALL(12),
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 14,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_BAR_BG}) {
        inline_sprite(g, run->pouch > 0 ? g->pouch_open[3] : g->pouch_closed, 72.0F, 72.0F);
        if (tj_button(g, "pull_pouch", pouchlbl, 190, 96, TJ_BTN_PRIMARY)) {
            tj_run_pull_pouch(run); /* draw one card from the pouch into the fan */
        }
        if (!tutorial) { /* during FTUE the staged panel already gives the instruction */
            const char *hint;
            if (run->hand_count > 0) {
                hint = pick_lang("drag a card onto a green cell; line up 3 alike to merge", "тяни карту на зелёную клетку · собери 3 одинаковых рядом → апгрейд", "karti yesil hucreye surukle");
            } else if (run->pouch > 0) {
                hint = pick_lang("tap the Pouch to draw a card", "жми «Мешочек» — вытяни карту", "kart cek");
            } else {
                hint = pick_lang("win fights and finish laps to fill the pouch", "побеждай и проходи круги — мешочек копится", "savaslari kazan");
            }
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), hint, &s_dim);
        }
    }
    static nt_ui_transform_t s_fan_xf[TJ_MAX_HAND];
    float fx0;
    float fy;
    fan_base(g, run->hand_count, &fx0, &fy);
    const float center = (run->hand_count > 1) ? ((float)(run->hand_count - 1) * 0.5F) : 0.0F;
    for (int i = 0; i < run->hand_count; i++) {
        if (i == drag_idx) {
            continue; /* drawn under the cursor by tj_view_drag_overlay */
        }
        const float off = (float)i - center;
        const bool lift = (i == hover);
        s_fan_xf[i] = nt_ui_transform_defaults();
        s_fan_xf[i].rotation_z = lift ? 0.0F : (off * 0.09F); /* arc tilt; hover straightens */
        if (lift) {
            s_fan_xf[i].scale_x = 1.14F;
            s_fan_xf[i].scale_y = 1.14F;
        }
        const float yarc = (off * off) * 1.7F; /* edges sit lower -> fan arc */
        draw_fan_card(g, run->hand_cards[i], fx0 + ((float)i * FAN_STEP), (lift ? fy - FAN_LIFT : fy) + yarc, lift, &s_fan_xf[i]);
    }
}

/* Small "?" help button, top-right. Returns true on click (toggle the help modal). */
bool tj_view_help_button(game_ctx_t *g) {
    bool clicked = false;
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP}, .offset = {-12.0F, 12.0F}, .zIndex = 52},
          .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) {
        clicked = tj_button(g, "help_q", "?", 44, 44, TJ_BTN_SECONDARY);
    }
    return clicked;
}

/* Help modal: how-to reference (the full lesson lives in the tutorial/FTUE).
 * Returns true when the player closes it. */
bool tj_view_help_modal(game_ctx_t *g) {
    bool close = false;
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .zIndex = 80},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {16.0F, 12.0F, 8.0F, 205.0F}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(540), CLAY_SIZING_FIT(0)},
                         .padding = CLAY_PADDING_ALL(24),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 9,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {40.0F, 34.0F, 24.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(16.0F),
              .border = {.color = {198.0F, 154.0F, 55.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("How to play", "Как играть", "Nasil oynanir"), &s_panel_title);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("The hero walks the ring and fights on his own.", "Герой идёт по кольцу и сам сражается.", "Kahraman kendi savasir."),
                        &s_stat);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT),
                        pick_lang("- Pouch: draw a card, drag it onto a green field cell", "• «Мешочек»: вытяни карту и тяни её на зелёную клетку поля", "- torba"), &s_stat);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT),
                        pick_lang("- 3 alike in a row merge into a stronger one (upgrades you)", "• 3 одинаковых рядом сливаются в сильнее (качают героя)", "- 3 ayni"), &s_stat);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT),
                        pick_lang("- Events are dice rolls; a boss waits at the end of each lap", "• События — бросок кубика; в конце круга — босс", "- olaylar"), &s_stat);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("- On death: upgrade the aul on the right, then a new run", "• Умер → прокачай аул справа → новый забег", "- aul"), &s_stat);
            close = tj_button(g, "help_close", pick_lang("Got it", "Понятно", "Tamam"), 200, 54, TJ_BTN_PRIMARY);
        }
    }
    return close;
}

/* Staged first-run tutorial. `step` 0..4 (scene owns advancement). Draws a pulsing
 * highlight on the step's target + a top-center instruction panel. Returns:
 * 1 = action button pressed (advance), 2 = "Skip" pressed, 0 = nothing.
 * The run is paused by the scene while this is up, so there is no time pressure. */
int tj_view_ftue_overlay(game_ctx_t *g, const tj_run_t *run, int step, float t) {
    (void)run;
    int result = 0;
    const float vw = g->logical_w;
    const float vh = g->logical_h;

    /* Pulsing ring on the target element (pouch for step 1, field for 2-3). */
    float hx = 0.0F;
    float hy = 0.0F;
    float hw = 0.0F;
    float hh = 0.0F;
    bool has_hl = false;
    if (step == 1) {
        hx = 12.0F;
        hy = vh - 126.0F;
        hw = 190.0F;
        hh = 96.0F;
        has_hl = true;
    } else if (step == 2 || step == 3) {
        hx = 298.0F;
        hy = 64.0F;
        hw = vw - 596.0F;
        hh = vh - 220.0F;
        has_hl = true;
    }
    if (has_hl) {
        const float pulse = 0.5F + (0.5F * sinf(t * 4.5F));
        const Clay_Color ring = {255.0F, 214.0F, 110.0F, 150.0F + (95.0F * pulse)};
        CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                           .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                           .offset = {hx, hy},
                           .zIndex = 78,
                           .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.sizing = {CLAY_SIZING_FIXED(hw), CLAY_SIZING_FIXED(hh)}},
              .cornerRadius = CLAY_CORNER_RADIUS(12.0F),
              .border = {.color = ring, .width = CLAY_BORDER_OUTSIDE(3 + (int)(3.0F * pulse))}}) {}
    }

    const char *body;
    switch (step) {
    case 0:
        body = pick_lang("Your hero walks the steppe and fights on his own. Your job: build the field and make him stronger.",
                         "Твой герой сам идёт по степи и сражается. Твоя задача — строить поле и усиливать его.", "Kahraman kendi yurur ve savasir. Senin isin: tarlayi kur.");
        break;
    case 1:
        body = pick_lang("Tap the Pouch (bottom-left) to draw a card.", "Нажми «Мешочек» внизу слева — вытяни карту.", "Sol alttaki torbaya bas - kart cek.");
        break;
    case 2:
        body = pick_lang("Drag the card onto a green field cell.", "Перетащи карту на зелёную клетку поля.", "Karti yesil hucreye surukle.");
        break;
    case 3:
        body = pick_lang("Place 3 alike in a row - they merge and raise the hero's stat.", "Поставь 3 одинаковых рядом — они сольются и поднимут стат героя.",
                         "3 ayni yan yana - birlesip kahramani guclendirir.");
        break;
    default:
        body = pick_lang("Done! Survive, clear laps, beat bosses. If you fall, upgrade the aul and send a new hero. Good luck!",
                         "Готово! Выживай, проходи круги, бей боссов. Погиб — прокачай аул и пошли нового. Удачи!", "Hazir! Hayatta kal, tur gec, boss yen. Iyi sanslar!");
        break;
    }

    static char stepnum[24];
    int shown_step = step; /* pull/place/merge = 1..3 (intro is a separate stage, not numbered) */
    if (shown_step < 1) {
        shown_step = 1;
    } else if (shown_step > 3) {
        shown_step = 3;
    }
    (void)snprintf(stepnum, sizeof stepnum, "%s %d/3", pick_lang("Tutorial", "Обучение", "Egitim"), shown_step);
    const bool action_step = (step == 0 || step >= 4);
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_TOP}, .offset = {0.0F, 74.0F}, .zIndex = 82},
          .layout = {.sizing = {CLAY_SIZING_FIXED(640), CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(20),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 12,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {40.0F, 34.0F, 24.0F, 250.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(16.0F),
          .border = {.color = {198.0F, 154.0F, 55.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), stepnum, &s_panel_title);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), body, &s_stat);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 10}}) {
            if (action_step && tj_button(g, "ftue_next", (step >= 4) ? pick_lang("Play >", "Играть ▶", "Oyna ▶") : pick_lang("Next >", "Дальше ▶", "Ileri ▶"), 200, 50, TJ_BTN_PRIMARY)) {
                result = 1;
            }
            if (step < 4 && tj_button(g, "ftue_skip", pick_lang("Skip", "Пропустить", "Atla"), 150, 44, TJ_BTN_SECONDARY)) {
                result = 2;
            }
        }
    }
    return result;
}

// #region first-run intro (dawn reveal -> send the wayfarer from the aul)
/* Dawn-from-darkness: a warm-dark veil lifts off the already-placed world, the opening
 * line fading in over the dark then out as it clears. t = seconds since intro start. */
void tj_view_reveal_veil(game_ctx_t *g, float t) {
    float va = 1.0F - (t / TJ_REVEAL_SECONDS);
    if (va > 0.0F) {
        if (va > 1.0F) {
            va = 1.0F;
        }
        const Clay_Color veil = {20.0F, 13.0F, 8.0F, 255.0F * (va * va)}; /* ease-out: lingers dark, clears fast */
        CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                           .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                           .zIndex = 70,
                           .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
              .backgroundColor = veil}) {}
    }
}

/* Right "launch" panel (first run): the hero waits at the fire; the player sends him off.
 * Once the reveal clears, a pulsing ring + a tutorial finger draw the eye to the button.
 * Returns true on press. The same verb repeats in the aul panel (unified launch ritual). */
bool tj_view_launch_panel(game_ctx_t *g, const tj_run_t *run, float t) {
    (void)run;
    bool send = false;
    const bool armed = t >= TJ_REVEAL_SECONDS; /* button cues appear after the world is revealed */
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 12,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("Champion of the clan", "Чемпион рода", "Soyun sampiyonu"), &s_panel_title);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("Ready at the fire.", "Готов у костра.", "Ates basinda."), &s_dim);
        CLAY({.id = CLAY_ID("intro_send_box"), .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) {
            send = tj_button(g, "intro_send", pick_lang("Set out", "Отправить путника", "Yola cikar"), 258, 60, TJ_BTN_PRIMARY);
        }
    }
    if (!armed) {
        return false; /* still revealing the world; no press cues yet */
    }
    const float pulse = 0.5F + (0.5F * sinf(t * 4.5F)); /* pulsing ring hugs the button */
    const Clay_Color ring = {255.0F, 214.0F, 110.0F, 150.0F + (95.0F * pulse)};
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                       .parentId = CLAY_ID("intro_send_box").id,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                       .zIndex = 82,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.sizing = {CLAY_SIZING_FIXED(274.0F), CLAY_SIZING_FIXED(76.0F)}},
          .cornerRadius = CLAY_CORNER_RADIUS(14.0F),
          .border = {.color = ring, .width = CLAY_BORDER_OUTSIDE(3 + (int)(3.0F * pulse))}}) {}
    if (has_region(g->ui_finger_pointer_128)) {
        const float bob = 2.0F + (8.0F * (0.5F + (0.5F * sinf(t * 4.0F)))); /* finger taps up toward the button */
        const nt_ui_image_style_t fimg = nt_ui_image_style_defaults();
        const Clay_ElementDeclaration fdecl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(76.0F), CLAY_SIZING_FIXED(76.0F)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                         .parentId = CLAY_ID("intro_send_box").id,
                         .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM},
                         .offset = {-16.0F, bob},
                         .zIndex = 86,
                         .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
        };
        nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, g->ui_finger_pointer_128, &fimg, &fdecl);
    }
    return send;
}

/* Centered intro banner (the tooltip while waiting, the walkout line after): one short
 * on-theme line in a soft panel near the top so the campfire stays visible. */
void tj_view_intro_banner(game_ctx_t *g, const char *text) {
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_TOP},
                       .offset = {0.0F, 92.0F},
                       .zIndex = 83,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.sizing = {CLAY_SIZING_FIXED(560), CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(16), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {30.0F, 24.0F, 16.0F, 225.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(14.0F),
          .border = {.color = {198.0F, 154.0F, 55.0F, 230.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_stat);
    }
}

/* First-run black-screen open: full black, the opening lines fade in, then a "tap to
 * continue" prompt + finger. The player's tap unlocks web audio (main.c resume) and
 * starts the dawn reveal. t = seconds since the screen appeared. */
/* Warm sand grains drifting on the wind across the black screen (procedural, no asset:
 * tinted rounded Clay rects, deterministic drift by index + time). */
static void draw_intro_sand(game_ctx_t *g, float t) {
    const float vw = g->logical_w;
    const float vh = g->logical_h;
    for (int i = 0; i < 30; i++) {
        const float bx = (float)((i * 131) % 1000) / 1000.0F * vw;
        const float by = (float)((i * 197) % 1000) / 1000.0F * vh;
        const float spd = 16.0F + ((float)(i % 5) * 13.0F); /* wind: grains drift rightward */
        const float gx = fmodf(bx + (t * spd), vw + 6.0F) - 3.0F;
        const float gy = by + (sinf((t * 0.6F) + (float)i) * 9.0F);
        const float sz = 2.0F + (float)(i % 3);
        const float al = 28.0F + ((float)(i % 4) * 30.0F);
        CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                           .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                           .offset = {gx, gy},
                           .zIndex = 89,
                           .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
              .layout = {.sizing = {CLAY_SIZING_FIXED(sz), CLAY_SIZING_FIXED(sz)}},
              .backgroundColor = {206.0F, 184.0F, 142.0F, al},
              .cornerRadius = CLAY_CORNER_RADIUS(sz * 0.5F)}) {}
    }
}

void tj_view_intro_black(game_ctx_t *g, float t) {
    const float a1 = (t < 0.4F) ? (t / 0.4F) : 1.0F;                                /* "следы" fades in 0..0.4 */
    const float a2 = (t < 1.0F) ? 0.0F : ((t < 1.6F) ? ((t - 1.0F) / 0.6F) : 1.0F); /* "путника" 1.0..1.6 */
    const nt_ui_label_style_t l1 = {.font_id = 0, .font_size = 32, .color = {236.0F, 214.0F, 170.0F, 255.0F * a1}, .align = CLAY_TEXT_ALIGN_CENTER};
    const nt_ui_label_style_t l2 = {.font_id = 0, .font_size = 26, .color = {214.0F, 188.0F, 150.0F, 255.0F * a2}, .align = CLAY_TEXT_ALIGN_CENTER};
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                       .zIndex = 88,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
          .backgroundColor = {8.0F, 6.0F, 4.0F, 255.0F}}) {}
    draw_intro_sand(g, t); /* drifting sand on the wind */
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                       .zIndex = 90,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 16, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("Sand erases every footprint.", "Песок стирает следы.", "Kum izleri siler."), &l1);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("The path awaits its first wayfarer.", "Путь ждёт первого путника.", "Yol ilk yolcusunu bekliyor."), &l2);
    }
    if (t < 1.9F) {
        return; /* let the lines land before inviting the tap */
    }
    const float pulse = 0.55F + (0.45F * sinf(t * 3.2F));
    const nt_ui_label_style_t hint = {.font_id = 0, .font_size = 18, .color = {198.0F, 178.0F, 140.0F, 120.0F + (135.0F * pulse)}, .align = CLAY_TEXT_ALIGN_CENTER};
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM},
                       .offset = {0.0F, -96.0F},
                       .zIndex = 91,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("tap to continue", "нажми, чтобы продолжить", "devam icin dokun"), &hint);
    }
    if (has_region(g->ui_finger_pointer_128)) {
        const float bob = sinf(t * 3.2F) * 7.0F;
        const nt_ui_image_style_t fimg = nt_ui_image_style_defaults();
        const Clay_ElementDeclaration fdecl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(72.0F), CLAY_SIZING_FIXED(72.0F)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                         .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM},
                         .offset = {120.0F, -88.0F + bob},
                         .zIndex = 92,
                         .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
        };
        nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, g->ui_finger_pointer_128, &fimg, &fdecl);
    }
}
// #endregion

// #region settings modal
#define TJ_RESET_HOLD_SECONDS 5.0F

static float s_settings_reset_hold;

static int settings_pct(float v01) { return (int)((v01 * 100.0F) + 0.5F); }

static void settings_set_lang(i18n_lang_t lang) {
    i18n_set(lang);
    save_set_int("lang", (int)lang);
    save_flush();
}

static tj_btn_variant_t settings_lang_variant(i18n_lang_t lang) { return i18n_get() == lang ? TJ_BTN_PRIMARY : TJ_BTN_SECONDARY; }

/* Full reset: wipe progress (best + aul + tamga), keep UI prefs (lang+volumes),
 * close the modal, and drop back to the menu at zero. */
static void settings_full_reset(game_ctx_t *g) {
    const int lang = save_get_int("lang", LANG_RU);
    const int music = save_get_int("music_vol", 60);
    const int sfx = save_get_int("sfx_vol", 70);
    save_clear();
    save_set_int("lang", lang);
    save_set_int("music_vol", music);
    save_set_int("sfx_vol", sfx);
    save_flush();
    tj_aul_load();
    g->best = 0;
    g->score = 0;
    g->settings_open = false;
    game_goto(g, &SCENE_MENU);
}

static void settings_volume_row(game_ctx_t *g, const char *label, const char *slider_id, const char *save_key, float cur, void (*apply)(float)) {
    float nv = cur;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 18, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(150), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), label, &s_stat);
        }
        nv = tj_slider(g, slider_id, cur);
    }
    if (settings_pct(nv) != settings_pct(cur)) {
        apply(nv);
        save_set_int(save_key, settings_pct(nv));
        save_flush();
    }
}

/* Gear button, top-right (left of the "?" help button). True on click. */
bool tj_view_settings_button(game_ctx_t *g) {
    bool clicked = false;
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP}, .offset = {-64.0F, 12.0F}, .zIndex = 52},
          .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) {
        const nt_ui_button_style_t style = {
            .idle = {.atlas = g->atlas, .bg_region = g->ui_button_dark_64, .bg_tint = 0xFF3A5A8AU, .scale = 1.0F, .opacity = 1.0F},
            .hover = {.bg_region = g->ui_button_dark_64, .bg_tint = 0xFF3A5A8AU, .scale = 1.06F, .opacity = 1.0F},
            .pressed = {.bg_region = g->ui_button_dark_64, .bg_tint = 0xFF3A5A8AU, .scale = 0.95F, .offset_y = 3.0F, .opacity = 1.0F},
            .disabled = {.bg_region = g->ui_button_dark_64, .bg_tint = 0xFF3A5A8AU, .scale = 1.0F, .opacity = 0.4F},
            .transition_speed = 12.0F,
            .hit_padding_lrtb = {8, 8, 8, 8},
            .slice9_scale = 1.0F,
        };
        const Clay_ElementDeclaration decl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(48)}, .padding = CLAY_PADDING_ALL(6), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}};
        nt_ui_button_begin(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), nt_ui_id("settings_gear"), &style, &decl, true);
        if (g->icon_settings_32 != 0U && g->icon_settings_32 != NT_ATLAS_INVALID_REGION) {
            const nt_ui_image_style_t img = nt_ui_image_style_defaults();
            const Clay_ElementDeclaration idecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(32)}}};
            nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), g->atlas, g->icon_settings_32, &img, &idecl);
        }
        clicked = nt_ui_button_end(g->ui);
    }
    return clicked;
}

/* Settings modal: scrim + card with music/SFX sliders, language, and a 5s
 * hold-to-reset. Returns true when the player closes it. */
bool tj_view_settings_modal(game_ctx_t *g, float dt) {
    bool close = false;
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .zIndex = 80},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {16.0F, 12.0F, 8.0F, 210.0F}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(640), CLAY_SIZING_FIT(0)},
                         .padding = CLAY_PADDING_ALL(28),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 18,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {40.0F, 34.0F, 24.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(18.0F),
              .border = {.color = {198.0F, 154.0F, 55.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_SETTINGS), &TJ_STYLE_HEADING);

            settings_volume_row(g, i18n(T_MUSIC), "set_music_vol", "music_vol", tj_audio_get_music_volume(), tj_audio_set_music_volume);
            settings_volume_row(g, i18n(T_SFX), "set_sfx_vol", "sfx_vol", tj_audio_get_sfx_volume(), tj_audio_set_sfx_volume);

            CLAY(
                {.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 12, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                if (tj_button(g, "set_en", i18n_lang_label(LANG_EN), 170, 54, settings_lang_variant(LANG_EN))) {
                    settings_set_lang(LANG_EN);
                }
                if (tj_button(g, "set_ru", i18n_lang_label(LANG_RU), 170, 54, settings_lang_variant(LANG_RU))) {
                    settings_set_lang(LANG_RU);
                }
                if (tj_button(g, "set_tr", i18n_lang_label(LANG_TR), 170, 54, settings_lang_variant(LANG_TR))) {
                    settings_set_lang(LANG_TR);
                }
            }

            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_RESET_HINT), &s_dim);
            if (tj_hold_button(g, "set_hold_reset", i18n(T_RESET), dt, TJ_RESET_HOLD_SECONDS, &s_settings_reset_hold)) {
                settings_full_reset(g);
            }

            close = tj_button(g, "set_close", i18n(T_BACK), 240, 60, TJ_BTN_SECONDARY);
        }
    }
    return close;
}
// #endregion
