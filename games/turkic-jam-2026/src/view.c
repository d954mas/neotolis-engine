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
#include "ui/nt_ui_fit.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_label.h"

#include "audio_assets.h"
#include "aul.h"
#include "config.h"
#include "i18n.h"
#include "journal.h"
#include "rng.h"
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
/* Field building level badges: cream digit on a dark pill; dark digit on a gold pill at max tier. */
static const nt_ui_label_style_t s_badge_num = {.font_id = 0, .font_size = 18, .color = {248.0F, 240.0F, 222.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_badge_num_max = {.font_id = 0, .font_size = 18, .color = {44.0F, 32.0F, 12.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
/* Card effect line + hover tooltip text. */
static const nt_ui_label_style_t s_card_eff = {.font_id = 0, .font_size = 14, .color = {214.0F, 198.0F, 150.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_tip_name = {.font_id = 0, .font_size = 18, .color = {245.0F, 232.0F, 200.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT};
static const nt_ui_label_style_t s_tip_eff = {.font_id = 0, .font_size = 15, .color = {206.0F, 224.0F, 196.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT};
static const nt_ui_label_style_t s_card_name = {.font_id = 0, .font_size = 19, .color = {245.0F, 236.0F, 214.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_die = {.font_id = 0, .font_size = 44, .color = {40.0F, 28.0F, 18.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
/* Dice-event "wheel of fate" window. */
static const nt_ui_label_style_t s_ev_title = {.font_id = 0, .font_size = 28, .color = {240.0F, 212.0F, 150.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_ev_check = {.font_id = 0, .font_size = 18, .color = {226.0F, 196.0F, 120.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_ev_value = {.font_id = 0, .font_size = 24, .color = {248.0F, 236.0F, 204.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_wheel_chip = {.font_id = 0, .font_size = 15, .color = {210.0F, 196.0F, 166.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_wheel_chip_sel = {.font_id = 0, .font_size = 16, .color = {32.0F, 24.0F, 12.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_wheel_hub = {.font_id = 0, .font_size = 32, .color = {245.0F, 232.0F, 200.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
/* Bigger intro/launch text — the opening moment reads large, not like dense HUD copy. */
static const nt_ui_label_style_t s_intro_title = {.font_id = 0, .font_size = 30, .color = {236.0F, 210.0F, 150.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_intro_sub = {.font_id = 0, .font_size = 21, .color = {200.0F, 184.0F, 150.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_banner = {.font_id = 0, .font_size = 25, .color = {234.0F, 222.0F, 198.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_ev_good = {.font_id = 0, .font_size = 24, .color = {126.0F, 200.0F, 134.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_ev_bad = {.font_id = 0, .font_size = 24, .color = {230.0F, 110.0F, 78.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};

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

/* Merge-line family colour (the pedestal/base tint). Война/Табун/Степь/Жильё/Вода. The
 * base disc under each building reads its family at a glance — the key "what stat is
 * this" cue when lining up triples. */
static Clay_Color line_color(int line) {
    switch (line) {
    case 1:
        return (Clay_Color){198.0F, 78.0F, 54.0F, 255.0F}; /* Война -> Сила: ember red */
    case 2:
        return (Clay_Color){196.0F, 150.0F, 86.0F, 255.0F}; /* Табун -> Скорость: sandy tan */
    case 3:
        return (Clay_Color){108.0F, 168.0F, 96.0F, 255.0F}; /* Степь -> Выносл.: steppe green */
    case 4:
        return (Clay_Color){214.0F, 176.0F, 72.0F, 255.0F}; /* Жильё -> припасы: gold */
    case 5:
        return (Clay_Color){86.0F, 166.0F, 178.0F, 255.0F}; /* Вода -> хил: teal */
    default:
        return (Clay_Color){150.0F, 134.0F, 104.0F, 255.0F};
    }
}

/* Gold used for the maxed-tier aura/crown and the "this will merge" telegraph. */
static const Clay_Color TJ_GOLD = {246.0F, 206.0F, 96.0F, 255.0F};

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

// #region battle stage state (combat drawn by the sprite renderer inside the hero panel)
/* Nominal stage size (px). The panel is 332 wide, padding 16 -> 300 inner; height fixed.
 * Sprite pass uses the live bbox (s_bat_*); Clay overlays use these constants — they match. */
#define BAT_W 300.0F
#define BAT_H 240.0F
static char s_battle_tag;        /* CUSTOM data sentinel: marks the arena element, not the map. */
static float s_bat_cx, s_bat_cy; /* stage centre (logical coords), from the element bbox. */
static float s_bat_w = BAT_W;    /* live stage size (px). */
static float s_bat_h = BAT_H;    //
static float s_bat_m[16];        /* LAYOUT -> GL world matrix for the stage (own bbox, no camera). */
static float s_bat_clock;        /* free-running stage clock (idle bob), advanced by the tick. */
static tj_shake_t s_bat_shake;   /* local arena trauma: punches the sprites on every blow. */

/* Cosmetic combat particles (impact sparks + victory burst). View-owned so the sim stays
 * deterministic; positions are stage-local px (origin = stage centre, +y down). */
#define TJ_BAT_PARTS 64 /* power-of-2: the ring-buffer index masks cleanly. */
typedef struct {
    float x, y, vx, vy;
    float life, life0, size;
    Clay_Color col;
} tj_bat_part_t;
static tj_bat_part_t s_parts[TJ_BAT_PARTS];
static int s_part_head;
static float s_prev_hero_t, s_prev_enemy_t; /* edge-detect a fresh blow (timer jumps back up). */
static bool s_prev_win;                     /* edge-detect the victory moment. */
// #endregion

static float clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

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

/* Core sprite emit: one w*h quad of `region` tinted by `color`, centred at (cx, cy) in the
 * given LAYOUT->world matrix. Mirrors nt_ui's plain-region image path (ipu/source/origin) so
 * sprites scale and anchor exactly like UI images. The map (camera-panned) and the battle
 * stage (own bbox, no pan) both build on this. */
static void emit_quad(game_ctx_t *g, uint32_t region, uint32_t color, float w, float h, float cx, float cy, const float world_m[16]) {
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
    float m[16];
    for (int rr = 0; rr < 4; ++rr) {
        m[rr] = sx * world_m[rr];
        m[4 + rr] = -sy * world_m[4 + rr];
        m[8 + rr] = world_m[8 + rr];
        m[12 + rr] = (cx * world_m[rr]) + (cy * world_m[4 + rr]) + world_m[12 + rr];
    }
    nt_sprite_renderer_emit_region(g->atlas, region, m, r->origin_x, r->origin_y, color, 0U);
}

/* Emit centred at the map centre + (ox, oy). */
static void emit_map_quad(game_ctx_t *g, uint32_t region, uint32_t color, float w, float h, float ox, float oy) { emit_quad(g, region, color, w, h, s_map_cx + ox, s_map_cy + oy, s_world_m); }

/* z is now painter order (draw-call order), not a Clay zIndex — kept in the
 * signature so the per-element call sites stay untouched. */
static void map_sprite(game_ctx_t *g, uint32_t region, float w, float h, float ox, float oy, int16_t z) {
    (void)z;
    emit_map_quad(g, region, 0xFFFFFFFFU, w, h, ox, oy);
}

/* Solid colour map marker (events/landmarks/fallbacks) via the white pixel. */
static void map_rect_sprite(game_ctx_t *g, Clay_Color col, float w, float h, float ox, float oy) { emit_map_quad(g, g->white_region, pack_clay_color(col), w, h, ox, oy); }

/* Battle-stage sprite + solid rect (offset in px from the arena centre, +y down). */
static void bat_sprite(game_ctx_t *g, uint32_t region, uint32_t color, float w, float h, float ox, float oy) { emit_quad(g, region, color, w, h, s_bat_cx + ox, s_bat_cy + oy, s_bat_m); }
static void bat_rect(game_ctx_t *g, Clay_Color col, float w, float h, float ox, float oy) { emit_quad(g, g->white_region, pack_clay_color(col), w, h, s_bat_cx + ox, s_bat_cy + oy, s_bat_m); }

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

/* FTUE: frame cell (gx,gy) in the comfortable left-upper part of the map viewport, so the merge
   trio sits clear of the bottom hand and the right hero panel with room to drag up into it. */
void tj_view_focus_ftue_merge(int gx, int gy) {
    if (s_map_pitch <= 0.0F || s_map_vw <= 0.0F) {
        return;
    }
    const float vcx = s_map_cx - s_cam_x; /* viewport centre, no pan */
    const float vcy = s_map_cy - s_cam_y;
    const float tx = vcx - (s_map_vw * 0.16F); /* a touch left of centre */
    const float ty = vcy - (s_map_vh * 0.12F); /* above centre — drag room below */
    const float gxoff = ((float)gx - ((float)(s_map_cols - 1) * 0.5F)) * s_map_pitch;
    const float gyoff = ((float)gy - ((float)(s_map_rows - 1) * 0.5F)) * s_map_pitch;
    const float limx = fmaxf(0.0F, (s_map_extent - s_map_vw) * 0.5F) + s_map_pitch;
    const float limy = fmaxf(0.0F, (s_map_extent - s_map_vh) * 0.5F) + s_map_pitch;
    s_cam_x = fmaxf(-limx, fminf(limx, tx - vcx - gxoff));
    s_cam_y = fmaxf(-limy, fminf(limy, ty - vcy - gyoff));
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

/* 0-based tier from an id like "war_4" (supports tiers 1..9), or -1 if not this prefix. */
static int tier_id_index(const char *id, const char *prefix) {
    const size_t n = strlen(prefix);
    if (strncmp(id, prefix, n) != 0) {
        return -1;
    }
    if (id[n] < '1' || id[n] > '9' || id[n + 1] != '\0') {
        return -1;
    }
    return id[n] - '1';
}

/* Only tiers 1..3 have dedicated art; higher tiers reuse the top sprite and lean on the
 * pedestal colour + level number for identity. */
static uint32_t tier_sprite(const uint32_t arr[3], int tier0) {
    if (tier0 < 0) {
        return NT_ATLAS_INVALID_REGION;
    }
    return arr[(tier0 > 2) ? 2 : tier0];
}

static uint32_t merge_tile_region_for_id(const game_ctx_t *g, const char *id) {
    int tier = tier_id_index(id, "war_");
    if (tier >= 0) {
        return tier_sprite(g->tile_war, tier);
    }
    tier = tier_id_index(id, "horse_");
    if (tier >= 0) {
        return tier_sprite(g->tile_horse, tier);
    }
    tier = tier_id_index(id, "steppe_");
    if (tier >= 0) {
        return tier_sprite(g->tile_steppe, tier);
    }
    tier = tier_id_index(id, "home_");
    if (tier >= 0) {
        return tier_sprite(g->tile_home, tier);
    }
    tier = tier_id_index(id, "water_");
    if (tier >= 0) {
        return tier_sprite(g->tile_water, tier);
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
        inline_sprite(g, icon_region, 32.0F, 32.0F);
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

/* Short "what it gives" line for a building/card -> caller's buffer (so several can coexist
 * in one frame). Live stat boost, per-circle income, or per-circle heal. */
static void tile_effect_str(int tile, char *out, size_t cap) {
    if (tile < 0 || tile >= g_config.tile_count) {
        out[0] = '\0';
        return;
    }
    const tj_tile_def_t *t = &g_config.tiles[tile];
    if (t->boost_stat == TJ_STAT_BODY) {
        (void)snprintf(out, cap, "+%d %s", t->boost_amount, pick_lang("Sabre", "Сабля", "Kilic"));
    } else if (t->boost_stat == TJ_STAT_MIND) {
        (void)snprintf(out, cap, "+%d %s", t->boost_amount, pick_lang("Horse", "Конь", "At"));
    } else if (t->boost_stat == TJ_STAT_SPIRIT) {
        (void)snprintf(out, cap, "+%d %s", t->boost_amount, pick_lang("Amulet", "Оберег", "Tumar"));
    } else if (t->supplies > 0) {
        (void)snprintf(out, cap, "+%d %s", t->supplies, pick_lang("to clan", "в род", "soya")); /* paid to the aul at death, not per circle */
    } else if (t->stamina_restore > 0) {
        (void)snprintf(out, cap, "+%d %s", t->stamina_restore, pick_lang("HP/lap", "ХП/круг", "CAN/tur"));
    } else {
        out[0] = '\0';
    }
}

void tj_view_top_hud(game_ctx_t *g, const tj_run_t *run) {
    static char circle[40];
    static char sup[28];
    static char day[24];
    (void)snprintf(circle, sizeof circle, "%s %d/%d", pick_lang("Circle", "Круг", "Dongu"), run->circle, g_config.laps_to_win);
    (void)snprintf(sup, sizeof sup, "%s %d", pick_lang("Supplies", "Припасы", "Azik"), g_aul.supplies + run->supplies);
    (void)snprintf(day, sizeof day, "%s %d", pick_lang("Day", "День", "Gun"), run->day);
    /* Supplies = persistent aul bank + this run's haul: one number that never resets to 0.
     * Supplies pinned left, Circle+Day centred (HP lives in the hero panel). */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64)},
                     .padding = {16, 16, 10, 10},
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 10,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_BAR_BG}) {
        hud_chip(g, g->icon_supplies_32, sup);
        hud_spacer();
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 10, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            hud_chip(g, g->icon_circle_32, circle);
            hud_chip(g, g->icon_day_32, day);
        }
        hud_spacer();
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
        map_sprite(g, g->aul_fire_01, pitch * 0.6F, pitch * 0.6F, ox - (pitch * 0.02F), oy + (pitch * 0.30F), 4); /* campfire sits in front of the yurts, not floating over them */
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
    const float road_pitch = pitch * 1.04F;
    for (int i = 0; i < run->path_cells && i < TJ_MAX_PATH; i++) {
        map_sprite(g, road_region_for_path_cell(g, run, i), road_pitch, road_pitch, grid_x(run->path_gx[i], cols, pitch), grid_y(run->path_gy[i], rows, pitch), 2);
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
    const uint32_t variants[] = {g->buffer_edge_stones, g->buffer_stakes, g->decor_stones}; /* cart_marks dropped: its diagonal ruts read as a stray road on the no-build band */
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
/* Soft round glow/pad via the Kenney soft-circle (CC0). Used for the building pedestal,
 * drop shadow, the maxed-tier aura and the drag match-glow. No-ops if the region is
 * missing (the building sprite still reads). */
static void cell_glow(game_ctx_t *g, Clay_Color col, float w, float h, float ox, float oy) {
    uint32_t r = g->fx_sand_grain_01;
    if (!has_region(r)) {
        r = g->fx_solid_01; /* fallback to the solid quad if the soft circle isn't in the atlas */
    }
    emit_map_quad(g, r, pack_clay_color(col), w, h, ox, oy);
}

/* Crisp solid quad (fx_solid_01) — a defined plinth, vs the soft cell_glow halo. */
static void cell_plinth(game_ctx_t *g, Clay_Color col, float w, float h, float ox, float oy) { emit_map_quad(g, g->fx_solid_01, pack_clay_color(col), w, h, ox, oy); }

/* Placed buildings: a drop shadow + family-colour pedestal pad lift each piece off the
 * sand and show its line at a glance. Maxed tiles get a pulsing gold aura; while a card
 * is dragged, every building sharing its line+tier gets a pulsing family glow ("these
 * merge with what you hold"). Tier number + crown ride on top as Clay (tj_view_field_badges). */
static void draw_field(game_ctx_t *g, const tj_run_t *run, float pitch, float tile) {
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const int n = cols * rows;
    const float pulse = 0.5F + (0.5F * sinf(g->anim_t * 4.5F));
    int drag_line = -1;
    int drag_tier = -1;
    if (g->drag_tile >= 0 && g->drag_tile < g_config.tile_count) {
        drag_line = g_config.tiles[g->drag_tile].line;
        drag_tier = g_config.tiles[g->drag_tile].tier;
    }
    for (int i = 0; i < n && i < TJ_ZONE_CELLS; i++) {
        const int tidx = run->field_tile[i];
        if (tidx < 0) {
            continue;
        }
        const int gx = i % cols;
        const int gy = i / cols;
        const float ox = grid_x(gx, cols, pitch);
        const float oy = grid_y(gy, rows, pitch);
        float ts = tile;
        if (i == run->fx_cell && run->fx_cell_t > 0.0F) { /* place/merge pop: settle from big -> normal */
            const float dur = (run->fx_cell_mag > 0.5F) ? 0.45F : 0.30F;
            float p = 1.0F - (run->fx_cell_t / dur);
            p = (p < 0.0F) ? 0.0F : ((p > 1.0F) ? 1.0F : p);
            ts = tile * (1.0F + (run->fx_cell_mag * (1.0F - (p * (2.0F - p))))); /* pop: big -> settle (1-ease_out_quad) */
        }
        const int line = (tidx < g_config.tile_count) ? g_config.tiles[tidx].line : 0;
        cell_glow(g, (Clay_Color){0.0F, 0.0F, 0.0F, 120.0F}, tile * 1.2F, tile * 0.7F, ox, oy + (tile * 0.36F)); /* drop shadow */
        if (line > 0) {
            const Clay_Color lc = line_color(line);
            if (tj_config_tile_upgrade(tidx) < 0) { /* maxed: pulsing gold aura */
                cell_glow(g, (Clay_Color){TJ_GOLD.r, TJ_GOLD.g, TJ_GOLD.b, 150.0F + (90.0F * pulse)}, tile * (1.7F + (0.14F * pulse)), tile * (1.7F + (0.14F * pulse)), ox, oy);
            }
            if (drag_line > 0 && line == drag_line && g_config.tiles[tidx].tier == drag_tier) { /* mergeable with held card */
                cell_glow(g, (Clay_Color){lc.r, lc.g, lc.b, 160.0F + (90.0F * pulse)}, tile * (1.55F + (0.12F * pulse)), tile * (1.55F + (0.12F * pulse)), ox, oy);
            }
            /* family pedestal: a soft colour halo + a crisp solid plinth at the feet, so the
             * building clearly stands on a coloured base instead of floating on the sand */
            cell_glow(g, (Clay_Color){lc.r, lc.g, lc.b, 150.0F}, tile * 1.55F, tile * 1.55F, ox, oy);
            cell_plinth(g, (Clay_Color){lc.r * 0.42F, lc.g * 0.42F, lc.b * 0.42F, 255.0F}, tile * 1.2F, tile * 0.52F, ox, oy + (tile * 0.3F));
            cell_plinth(g, (Clay_Color){lc.r, lc.g, lc.b, 255.0F}, tile * 1.04F, tile * 0.4F, ox, oy + (tile * 0.26F));
        }
        const uint32_t region = tile_region_for_index(g, tidx);
        if (has_region(region)) {
            map_sprite(g, region, ts, ts, ox, oy, 4);
        } else {
            map_rect_sprite(g, cell_color(tidx), ts * 0.82F, ts * 0.82F, ox, oy);
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
        bob = -fabsf(sinf(run->move_t * 6.2832F)) * (pitch * 0.03F);
    }
    const uint32_t region = hero_region(g, run);
    if (has_region(region)) {
        map_sprite(g, region, pitch * 1.3F, pitch * 1.3F, hx, hy + bob, 6);
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

/* Placement cue, shown ONLY while a card is being dragged (not just held): a faint green
 * frame on every buildable cell, a stronger cue under the cursor, and the gold merge
 * telegraph. Dragging is the moment "where can I build?" matters; otherwise it's noise. */
static int s_ftue_gap_gx = -1; /* FTUE merge lesson: the cell the player must fill (-1 = none) */
static int s_ftue_gap_gy = -1;
void tj_view_set_ftue_gap(int gx, int gy) {
    s_ftue_gap_gx = gx;
    s_ftue_gap_gy = gy;
}

/* Pulsing gold overlay on the FTUE merge gap, drawn in the world pass so it tracks the camera. */
static void draw_ftue_gap(game_ctx_t *g, const tj_run_t *run, float pitch) {
    if (s_ftue_gap_gx < 0 || s_ftue_gap_gy < 0) {
        return;
    }
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const float pulse = 0.5F + (0.5F * sinf(g->anim_t * 4.5F));
    const Clay_Color gold = {255.0F, 214.0F, 110.0F, 150.0F + (95.0F * pulse)};
    cell_overlay(g, g->ui_hover_cell_overlay_128, gold, pitch, grid_x(s_ftue_gap_gx, cols, pitch), grid_y(s_ftue_gap_gy, rows, pitch));
}

static void draw_build_hints(game_ctx_t *g, const tj_run_t *run, float pitch) {
    if (g->drag_tile < 0) {
        return;
    }
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const bool ftue_lock = (s_ftue_gap_gx >= 0 && s_ftue_gap_gy >= 0); /* lesson: only the gold gap is a legal target */
    if (!ftue_lock) {
        for (int gy = 0; gy < rows; gy++) {
            for (int gx = 0; gx < cols; gx++) {
                if (!is_build_cell(run, gx, gy)) { /* highlight every buildable cell; map viewport clips off-screen */
                    continue;
                }
                cell_overlay(g, g->ui_valid_cell_overlay_128, (Clay_Color){150.0F, 230.0F, 160.0F, 90.0F}, pitch, grid_x(gx, cols, pitch), grid_y(gy, rows, pitch));
            }
        }
    }
    int hgx = -1;
    int hgy = -1;
    if (!tj_view_world_cell_at(g->ptr_x, g->ptr_y, &hgx, &hgy) || !cell_in_view(hgx, hgy, cols, rows, pitch)) {
        return;
    }
    if (ftue_lock && (hgx != s_ftue_gap_gx || hgy != s_ftue_gap_gy)) { /* hovering off the gap reads as invalid */
        cell_overlay(g, g->ui_invalid_cell_overlay_128, (Clay_Color){236.0F, 110.0F, 92.0F, 180.0F}, pitch, grid_x(hgx, cols, pitch), grid_y(hgy, rows, pitch));
        return;
    }
    const bool ok = is_build_cell(run, hgx, hgy);
    const float hox = grid_x(hgx, cols, pitch);
    const float hoy = grid_y(hgy, rows, pitch);
    /* Drag + hover over a buildable cell that would FUSE: gold telegraph on the whole
     * group + the tier+1 result drawn as a faded ghost on the drop cell. The "+N" badge
     * rides on top in Clay (tj_view_drag_overlay). */
    if (g->drag_tile >= 0 && ok) {
        int group[24];
        int result = -1;
        const int gn = tj_run_merge_preview(run, g->drag_tile, hgx, hgy, group, 24, &result);
        if (result >= 0) {
            const float pulse = 0.5F + (0.5F * sinf(g->anim_t * 7.0F));
            for (int k = 0; k < gn && k < 24; k++) {
                cell_glow(g, (Clay_Color){TJ_GOLD.r, TJ_GOLD.g, TJ_GOLD.b, 150.0F + (80.0F * pulse)}, pitch * 1.06F, pitch * 1.06F, grid_x(group[k] % cols, cols, pitch),
                          grid_y(group[k] / cols, rows, pitch));
            }
            const uint32_t rg = tile_region_for_index(g, result);
            if (has_region(rg)) {
                emit_map_quad(g, rg, pack_clay_color((Clay_Color){255.0F, 255.0F, 255.0F, 160.0F + (60.0F * pulse)}), pitch * 0.7F, pitch * 0.7F, hox, hoy);
            }
            return;
        }
    }
    const uint32_t region = ok ? g->ui_hover_cell_overlay_128 : g->ui_invalid_cell_overlay_128;
    const Clay_Color fb = ok ? (Clay_Color){160.0F, 255.0F, 175.0F, 220.0F} : (Clay_Color){236.0F, 110.0F, 92.0F, 180.0F};
    cell_overlay(g, region, fb, pitch, hox, hoy);
}

/* First-run intro effects, drawn in the SPRITE world pass (Clay = UI only): the black
 * screen, drifting warm dust haze, and the dawn-reveal veil. Reads g->intro_* (set by the
 * scene). Haze is a soft smoke puff (Kenney CC0) tinted warm, alpha-blended on black at low
 * opacity so it reads as airborne dust without washing out the intro text. */
static void draw_intro_fx(game_ctx_t *g) {
    float va = 1.0F; /* veil alpha: full black while waiting; eases off once the player taps */
    if (!g->intro_black) {
        const float k = 1.0F - (g->intro_t / TJ_REVEAL_SECONDS);
        va = (k > 0.0F) ? (k * k) : 0.0F;
    }
    if (va <= 0.004F) {
        return;
    }
    if (has_region(g->fx_solid_01)) { /* solid quad has geometry (white_region doesn't render via the sprite path) */
        emit_map_quad(g, g->fx_solid_01, pack_clay_color((Clay_Color){8.0F, 6.0F, 4.0F, 255.0F * va}), s_map_vw + 8.0F, s_map_vh + 8.0F, 0.0F, 0.0F);
    }
    if (!has_region(g->fx_sand_grain_01)) {
        return;
    }
    const float t = g->intro_anim_t;
    const float vw = s_map_vw;
    const float vh = s_map_vh;
    const float vmin = fminf(vw, vh); /* size haze relative to the viewport so it scales */
    for (int i = 0; i < 16; i++) {
        const float spd = 6.0F + ((float)(i % 5) * 5.0F); /* wind: dust haze drifts slowly rightward */
        const float bx = (float)((i * 131) % 1000) / 1000.0F * vw;
        const float by = (float)((i * 197) % 1000) / 1000.0F * vh;
        const float gx = fmodf(bx + (t * spd), vw) - (vw * 0.5F);
        const float gy = (by - (vh * 0.5F)) + (sinf((t * 0.35F) + (float)i) * 14.0F);
        const float sz = (vmin * 0.16F) + ((float)(i % 4) * vmin * 0.05F);
        const float al = (10.0F + ((float)(i % 5) * 8.0F)) * va; /* low: intro text stays clean over the haze */
        const uint32_t col = pack_clay_color((Clay_Color){206.0F, 184.0F, 148.0F, al});
        emit_map_quad(g, g->fx_sand_grain_01, col, sz, sz, gx, gy);
    }
}

// #region battle stage (sprite-pass arena hosted inside the hero panel)
/* Capture the arena element's bbox + transform. No camera pan: the stage is fixed in the panel. */
static void set_battle_from_frame(const nt_ui_custom_frame_t *frame) {
    const Clay_RenderCommand *cmd = (const Clay_RenderCommand *)frame->clay_cmd;
    const Clay_BoundingBox bb = cmd->boundingBox;
    s_bat_cx = bb.x + (bb.width * 0.5F);
    s_bat_cy = bb.y + (bb.height * 0.5F);
    s_bat_w = bb.width;
    s_bat_h = bb.height;
    memcpy(s_bat_m, frame->world_mat4, sizeof s_bat_m);
}

/* Fighter stations + ground line, from the live bbox (origin = centre, +y down). */
static void bat_layout(float *hero_x, float *enemy_x, float *ground_y, float *box_h) {
    *box_h = s_bat_h * 0.70F;    /* target fighter height (each sprite is aspect-fit within this) */
    *ground_y = s_bat_h * 0.33F; /* shared ground line; both fighters rest their feet here */
    *hero_x = -s_bat_w * 0.26F;
    *enemy_x = s_bat_w * 0.26F;
}

/* Draw a fighter aspect-correct, fit into (box_w x box_h), with its VISIBLE feet on ground_y.
 * `foot_pad` = transparent fraction below the art's feet (hero art is top-weighted, like the map
 * which lifts it 0.18*pitch); enemies mostly fill the frame. `scl` grows about the feet, `extra_oy`
 * nudges vertically (idle bob / death sink). */
static void bat_fighter(game_ctx_t *g, uint32_t region, uint32_t color, float box_w, float box_h, float ox, float ground_y, float extra_oy, float foot_pad, float scl) {
    float aspect = 1.0F;
    if (has_region(region)) {
        const nt_texture_region_t *r = nt_atlas_get_region(g->atlas, region);
        if (r->source_h > 0U) {
            aspect = (float)r->source_w / (float)r->source_h;
        }
    }
    float w = box_h * aspect;
    float h = box_h;
    if (w > box_w) { /* too wide: clamp by width, keep aspect */
        w = box_w;
        h = box_w / aspect;
    }
    w *= scl;
    h *= scl;
    const float cy = (ground_y - (h * (0.5F - foot_pad))) + extra_oy; /* visible feet land on the ground line */
    bat_sprite(g, region, color, w, h, ox, cy);
}

/* Push one cosmetic particle into the ring buffer (stage-local px). */
static void bat_spawn(float x, float y, float vx, float vy, float life, float size, Clay_Color col) {
    tj_bat_part_t *p = &s_parts[s_part_head];
    s_part_head = (s_part_head + 1) & (TJ_BAT_PARTS - 1);
    *p = (tj_bat_part_t){.x = x, .y = y, .vx = vx, .vy = vy, .life = life, .life0 = life, .size = size, .col = col};
}

/* A small spray of sparks at (x, y), biased upward (a clean hit pops). */
static void bat_sparks(float x, float y, int n, Clay_Color col) {
    for (int i = 0; i < n; i++) {
        const float ang = rng_range(0.0F, 6.2832F);
        const float spd = rng_range(40.0F, 150.0F);
        bat_spawn(x, y, cosf(ang) * spd, (sinf(ang) * spd) - 60.0F, rng_range(0.30F, 0.55F), rng_range(3.0F, 6.0F), col);
    }
}

/* Per-frame: advance particles + local trauma, and spawn bursts on the rising edge of a blow
 * or the victory moment (the sim drives the timers; the view only reacts). */
void tj_view_battle_tick(game_ctx_t *g, const tj_run_t *run, float dt) {
    (void)g;
    s_bat_clock += dt;
    tj_shake_update(&s_bat_shake, dt);
    for (int i = 0; i < TJ_BAT_PARTS; i++) {
        tj_bat_part_t *p = &s_parts[i];
        if (p->life <= 0.0F) {
            continue;
        }
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += 520.0F * dt;       /* gravity: sparks arc down */
        p->vx -= p->vx * 2.4F * dt; /* air drag */
    }
    if (s_bat_w <= 1.0F) {
        return; /* stage not laid out yet (first frame) */
    }
    float hero_x;
    float enemy_x;
    float ground_y;
    float box_h;
    bat_layout(&hero_x, &enemy_x, &ground_y, &box_h);
    const float hit_y = ground_y - (box_h * 0.45F); /* mid-body, where blows land */
    if (run->fx_enemy_t > s_prev_enemy_t + 0.01F) { /* hero just struck the enemy */
        bat_sparks(enemy_x, hit_y, 7, (Clay_Color){255.0F, 232.0F, 150.0F, 255.0F});
        tj_shake_add(&s_bat_shake, 0.35F);
    }
    if (run->fx_hero_t > s_prev_hero_t + 0.01F) { /* enemy just struck the hero */
        bat_sparks(hero_x, hit_y, 7, (Clay_Color){255.0F, 120.0F, 96.0F, 255.0F});
        tj_shake_add(&s_bat_shake, 0.5F);
    }
    s_prev_enemy_t = run->fx_enemy_t;
    s_prev_hero_t = run->fx_hero_t;
    if (run->combat_win && !s_prev_win) { /* victory: golden burst from the falling enemy */
        for (int i = 0; i < 24; i++) {
            const float ang = rng_range(0.0F, 6.2832F);
            const float spd = rng_range(60.0F, 230.0F);
            bat_spawn(enemy_x, hit_y, cosf(ang) * spd, (sinf(ang) * spd) - 120.0F, rng_range(0.5F, 1.0F), rng_range(3.0F, 7.0F), (Clay_Color){255.0F, 214.0F, 96.0F, 255.0F});
        }
        tj_shake_add(&s_bat_shake, 0.6F);
    }
    s_prev_win = run->combat_win;
}

/* Hero faces east, toward the enemy. Lunges on its own swing (fx_enemy_t), flinches/flashes
 * when struck (fx_hero_t); breathes when idle. */
static void draw_battle_hero(game_ctx_t *g, const tj_run_t *run, float hero_x, float ground_y, float box_h, float shx, float shy, bool active) {
    const float hero_atk = (run->fx_enemy_t > 0.0F) ? (run->fx_enemy_t / 0.6F) : 0.0F;
    const float hero_dmg = (run->fx_hero_t > 0.0F) ? (run->fx_hero_t / 0.6F) : 0.0F;
    const float ox = hero_x + (tj_ease_out_back(hero_atk) * s_bat_w * 0.12F) - (hero_dmg * s_bat_w * 0.05F);
    const float scl = 1.0F + (hero_dmg * 0.10F);
    const float bob = active ? 0.0F : (sinf(s_bat_clock * 2.2F) * s_bat_h * 0.02F);
    const Clay_Color tint = {255.0F, 255.0F - (hero_dmg * 90.0F), 255.0F - (hero_dmg * 90.0F), 255.0F};
    bat_fighter(g, g->hero_wayfarer_walk_e, pack_clay_color(tint), s_bat_w * 0.42F, box_h, ox + shx, ground_y + shy, bob, 0.36F, scl);
}

/* Enemy lunges on its swing (fx_hero_t), flashes when struck (fx_enemy_t), and fades+sinks
 * during the victory celebration. */
static void draw_battle_enemy(game_ctx_t *g, const tj_run_t *run, float enemy_x, float ground_y, float box_h, float shx, float shy) {
    const float atk = (run->fx_hero_t > 0.0F) ? (run->fx_hero_t / 0.6F) : 0.0F;
    const float dmg = (run->fx_enemy_t > 0.0F) ? (run->fx_enemy_t / 0.6F) : 0.0F;
    const float ox = enemy_x - (tj_ease_out_back(atk) * s_bat_w * 0.12F) + (dmg * s_bat_w * 0.05F);
    float sink = 0.0F;
    float alpha = 1.0F;
    float scl = 1.0F + (dmg * 0.10F);
    if (run->combat_win) {
        const float dur = (g_config.combat_win_seconds > 0.1F) ? g_config.combat_win_seconds : 1.2F;
        const float p = clampf(1.0F - (run->combat_win_t / dur), 0.0F, 1.0F);
        alpha = 1.0F - p;
        sink = p * s_bat_h * 0.16F;
        scl = 1.0F - (p * 0.35F);
    }
    const uint32_t boss_region = (current_cell_role(run) == TJ_CELL_BOSS) ? boss_region_for_circle(g, run->circle) : NT_ATLAS_INVALID_REGION;
    const uint32_t eregion = has_region(boss_region) ? boss_region : tile_region_for_index(g, run->combat_tile);
    if (has_region(eregion)) {
        const Clay_Color tint = {255.0F, 255.0F - (dmg * 60.0F), 255.0F - (dmg * 60.0F), 255.0F * alpha};
        bat_fighter(g, eregion, pack_clay_color(tint), s_bat_w * 0.42F, box_h, ox + shx, ground_y + shy, sink, 0.06F, scl);
    } else {
        const float h = box_h * scl;
        bat_rect(g, (Clay_Color){200.0F, 80.0F, 70.0F, 255.0F * alpha}, h * 0.7F, h, ox + shx, ((ground_y - (h * 0.5F)) + sink) + shy);
    }
}

static void draw_battle_particles(game_ctx_t *g, float shx, float shy) {
    for (int i = 0; i < TJ_BAT_PARTS; i++) {
        const tj_bat_part_t *p = &s_parts[i];
        if (p->life <= 0.0F) {
            continue;
        }
        const float a = p->life / p->life0;
        bat_rect(g, (Clay_Color){p->col.r, p->col.g, p->col.b, p->col.a * a}, p->size, p->size, p->x + shx, p->y + shy);
    }
}

/* Draw the arena: ground, hero, enemy (lunge/hit-flash/victory fade), then particles. */
static void draw_battle_stage(game_ctx_t *g, const nt_ui_custom_frame_t *frame) {
    const tj_run_t *run = (const tj_run_t *)g->run;
    if (run == NULL) {
        return;
    }
    set_battle_from_frame(frame);
    nt_sprite_renderer_set_material(g->sprite_material);
    float hero_x;
    float enemy_x;
    float ground_y;
    float box_h;
    bat_layout(&hero_x, &enemy_x, &ground_y, &box_h);
    float shx;
    float shy;
    float shdeg;
    tj_shake_sample(&s_bat_shake, s_bat_h * 0.05F, 0.0F, &shx, &shy, &shdeg);
    (void)shdeg;
    const bool active = run->in_combat || run->combat_win;
    bat_rect(g, (Clay_Color){48.0F, 34.0F, 22.0F, 255.0F}, s_bat_w * 0.94F, s_bat_h * 0.06F, 0.0F, ground_y + shy);
    draw_battle_hero(g, run, hero_x, ground_y, box_h, shx, shy, active);
    if (active) {
        draw_battle_enemy(g, run, enemy_x, ground_y, box_h, shx, shy);
    }
    draw_battle_particles(g, shx, shy);
}
// #endregion

/* The map WORLD: ground/road/aul/field/hero drawn by the sprite renderer. Invoked
 * by nt_ui_walk as a CUSTOM render command (NT_UI_CUSTOM_TYPE_GAME) — during the
 * walk the GL viewport + frame state are set up, so a standalone sprite emit is
 * actually visible (a draw before/inside the build is not). It composites under the
 * HUD/panels because later UI commands draw on top. */
static void world_custom_handler(const nt_ui_custom_frame_t *frame, void *userdata) {
    game_ctx_t *g = (game_ctx_t *)userdata;
    /* One global handler serves every CUSTOM element; the arena is told apart by its data tag. */
    const Clay_RenderCommand *cmd = (const Clay_RenderCommand *)frame->clay_cmd;
    const nt_ui_custom_data_t *cd = (const nt_ui_custom_data_t *)cmd->renderData.custom.customData;
    if (cd != NULL && cd->data == &s_battle_tag) {
        draw_battle_stage(g, frame);
        return;
    }
    const tj_run_t *run = (const tj_run_t *)g->run;
    if (run == NULL || run->grid_cols < 2 || run->grid_rows < 2 || run->path_cells < 1) {
        return;
    }
    set_world_from_frame(frame);
    const int maxdim = (run->grid_cols > run->grid_rows) ? run->grid_cols : run->grid_rows;
    /* ~9 cells across the viewport short side so the aul + road band + the first ring of
     * buildable desert are all on screen by default (else "where do I build?" is off-camera).
     * The large zone still scrolls; a small zone that fits gets the larger fit pitch. */
    const float vmin = fminf(s_map_vw, s_map_vh);
    const float pitch = fmaxf(floorf(vmin / 9.0F), floorf(vmin / (float)(maxdim + 1)));
    s_map_pitch = pitch;
    s_map_extent = (float)maxdim * pitch;
    s_map_cols = run->grid_cols;
    s_map_rows = run->grid_rows;
    const float tile = pitch * 0.66F;
    nt_sprite_renderer_set_material(g->sprite_material);
    if (g->intro_black) {
        draw_intro_fx(g); /* black screen + sand; the world stays hidden until the player sends the hero */
        nt_sprite_renderer_flush();
        return;
    }
    draw_ground(g, run, pitch, tile);
    draw_base_decor(g, run, pitch, tile);
    draw_aul(g, run, pitch);
    draw_road(g, run, pitch);
    draw_road_buffer(g, run, pitch, tile);
    draw_road_events(g, run, pitch);
    draw_global(g, run, pitch, tile);
    draw_field(g, run, pitch, tile);
    draw_build_hints(g, run, pitch);
    draw_ftue_gap(g, run, pitch);
    draw_tamga(g, run, pitch);
    draw_hero(g, run, pitch);
    if (g->intro_active) {
        draw_intro_fx(g); /* dawn-reveal veil + sand lifting off the revealed world */
    }
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
    return pick_lang("Batyr", "Батыр", "Batır");
}

static const char *stat_sabre(void) { return pick_lang("Sabre", "Сабля", "Kilic"); }
static const char *stat_horse(void) { return pick_lang("Horse", "Конь", "At"); }
static const char *stat_amulet(void) { return pick_lang("Amulet", "Оберег", "Tumar"); }

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

static void stat_tooltip(game_ctx_t *g, const char *title, const char *body) {
    nt_ui_label_style_t tip_title = s_panel_title;
    tip_title.font_size = 18;
    tip_title.align = CLAY_TEXT_ALIGN_LEFT;
    nt_ui_label_style_t tip_body = s_dim;
    tip_body.font_size = 16;
    tip_body.align = CLAY_TEXT_ALIGN_LEFT;
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_CENTER},
                       .offset = {-10.0F, 0.0F},
                       .zIndex = 80,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.sizing = {CLAY_SIZING_FIXED(276), CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(12), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6},
          .backgroundColor = {30.0F, 24.0F, 18.0F, 245.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
          .border = {.color = {116.0F, 78.0F, 38.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), title, &tip_title);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), body, &tip_body);
    }
}

static void hero_stat_row(game_ctx_t *g, const char *id_str, uint32_t icon, const char *text, const char *tip) {
    const uint32_t id = nt_ui_id(id_str);
    CLAY({.id = (Clay_ElementId){.id = id},
          .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(30)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        const bool hovered = Clay_Hovered();
        inline_sprite(g, icon, 28.0F, 28.0F);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_stat);
        if (hovered) {
            stat_tooltip(g, text, tip);
        }
    }
}

/* A floating element pinned over the arena, centred at the stage centre + (ox, oy) px. */
#define BAT_FLOAT(ox, oy, z)                                                                                                                                                                           \
    {                                                                                                                                                                                                  \
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT,                                                                                                                                                \
                     .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},                                                                          \
                     .offset = {(ox), (oy)},                                                                                                                                                           \
                     .zIndex = (int16_t)(z),                                                                                                                                                           \
                     .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},                                                                                                                     \
        .layout = {                                                                                                                                                                                    \
            .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}                                                                                                                               \
        }                                                                                                                                                                                              \
    }

/* One bright loot chip (icon + "+N") in the victory column; `fade` ramps it in. */
static void bat_reward_chip(game_ctx_t *g, uint32_t icon, const char *text, float fade) {
    nt_ui_label_style_t st = s_chip;
    st.color = (Clay_Color){255.0F, 240.0F, 200.0F, 255.0F * fade};
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .padding = {8, 8, 2, 2}, .childGap = 6, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {40.0F, 28.0F, 16.0F, 170.0F * fade},
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F)}) {
        inline_sprite(g, icon, 22.0F, 22.0F);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &st);
    }
}

/* The flying loot labels shown during the victory celebration (loot lands when it ends). */
static void bat_reward_column(game_ctx_t *g, const tj_run_t *run, float fade, float rise) {
    static char ls[16];
    static char lw[16];
    static char lg[16];
    static char lh[16];
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                       .offset = {0.0F, (BAT_H * 0.10F) - rise},
                       .zIndex = 34,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        if (run->win_sup > 0) {
            (void)snprintf(ls, sizeof ls, "+%d", run->win_sup);
            bat_reward_chip(g, g->icon_supplies_32, ls, fade);
        }
        if (run->win_wis > 0) {
            (void)snprintf(lw, sizeof lw, "+%d", run->win_wis);
            bat_reward_chip(g, g->icon_wisdom_32, lw, fade);
        }
        if (run->win_glory > 0) {
            (void)snprintf(lg, sizeof lg, "+%d", run->win_glory);
            bat_reward_chip(g, g->icon_glory_32, lg, fade);
        }
        if (run->win_sta > 0) {
            (void)snprintf(lh, sizeof lh, "+%d", run->win_sta);
            bat_reward_chip(g, g->icon_stamina_32, lh, fade);
        }
        bat_reward_chip(g, g->pouch_closed, pick_lang("+pouch", "+мешок", "+torba"), fade);
    }
}

/* Clay layer over the sprite arena: HP bars above each head, rising damage numbers, and the
 * victory header + loot labels. Geometry mirrors bat_layout (BAT_W/BAT_H constants). */
/* Two rising, fading damage numbers (hero + enemy). Distinct static buffers: Clay keeps the ptr. */
static void battle_dmg_numbers(game_ctx_t *g, const tj_run_t *run, float hero_x, float enemy_x) {
    static char hdmg[12];
    static char edmg[12];
    if (run->fx_hero_t > 0.0F && run->fx_hero_dmg > 0) {
        const float t = run->fx_hero_t / 0.6F;
        nt_ui_label_style_t st = s_log_styles[TJ_LOG_BAD];
        st.font_size = 22;
        st.color.a = 255.0F * t;
        (void)snprintf(hdmg, sizeof hdmg, "-%d", run->fx_hero_dmg);
        CLAY(BAT_FLOAT(hero_x, (-BAT_H * 0.30F) - ((1.0F - t) * 30.0F), 32)) { nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), hdmg, &st); }
    }
    if (run->fx_enemy_t > 0.0F && run->fx_enemy_dmg > 0) {
        const float t = run->fx_enemy_t / 0.6F;
        nt_ui_label_style_t st = s_stat;
        st.font_size = 22;
        st.color = (Clay_Color){250.0F, 230.0F, 150.0F, 255.0F * t};
        (void)snprintf(edmg, sizeof edmg, "-%d", run->fx_enemy_dmg);
        CLAY(BAT_FLOAT(enemy_x, (-BAT_H * 0.30F) - ((1.0F - t) * 30.0F), 32)) { nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), edmg, &st); }
    }
}

/* Victory header ("ПОБЕДА") + flying loot labels, ramped in over the celebration. */
static void battle_victory(game_ctx_t *g, const tj_run_t *run) {
    const float dur = (g_config.combat_win_seconds > 0.1F) ? g_config.combat_win_seconds : 1.2F;
    const float p = clampf(1.0F - (run->combat_win_t / dur), 0.0F, 1.0F);
    const float fade = (p < 0.25F) ? (p / 0.25F) : 1.0F; /* quick fade-in, then hold */
    const float rise = p * 18.0F;
    nt_ui_label_style_t hd = s_panel_title;
    hd.font_size = 26;
    hd.color = (Clay_Color){255.0F, 224.0F, 120.0F, 255.0F * fade};
    hd.align = CLAY_TEXT_ALIGN_CENTER;
    CLAY(BAT_FLOAT(0.0F, (-BAT_H * 0.44F) - rise, 33)) { nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("VICTORY", "ПОБЕДА", "ZAFER"), &hd); }
    bat_reward_column(g, run, fade, rise);
}

static void battle_overlays(game_ctx_t *g, const tj_run_t *run) {
    const bool fighting = run->in_combat;
    const bool celebrating = run->combat_win;
    if (!fighting && !celebrating) {
        return; /* idle: just the lone hero on the stage */
    }
    const int smax = (run->stamina_max > 0) ? run->stamina_max : 1;
    const int emax = (run->combat_enemy_max > 0) ? run->combat_enemy_max : 1;
    const int ehp = (run->combat_enemy_hp < 0) ? 0 : run->combat_enemy_hp;
    const float hero_x = -BAT_W * 0.26F;
    const float enemy_x = BAT_W * 0.26F;
    CLAY(BAT_FLOAT(hero_x, -BAT_H * 0.24F, 30)) { hp_bar((float)run->stamina / (float)smax, (Clay_Color){120.0F, 180.0F, 90.0F, 255.0F}, 108.0F); }
    if (fighting) {
        CLAY(BAT_FLOAT(enemy_x, -BAT_H * 0.24F, 30)) { hp_bar((float)ehp / (float)emax, (Clay_Color){200.0F, 80.0F, 70.0F, 255.0F}, 108.0F); }
        CLAY(BAT_FLOAT(0.0F, -BAT_H * 0.46F, 31)) { nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), run->combat_label[0] ? run->combat_label : "Бой", &s_dim); }
    }
    battle_dmg_numbers(g, run, hero_x, enemy_x);
    if (celebrating) {
        battle_victory(g, run);
    }
}

/* The dice event renders inside the hero panel (the unified stage); defined below, after the wheel helpers. */
static void draw_event_panel(game_ctx_t *g, const tj_run_t *run);

void tj_view_hero_panel(game_ctx_t *g, const tj_run_t *run) {
    static char force[40];
    static char speed[40];
    static char vigor[40];
    static char sta[40];
    static char cellinfo[40];
    const char *sabre_tip = pick_lang("+damage; used in rockfall checks", "+урон; проверки завала", "+hasar; kaya sinavi");
    const char *horse_tip = pick_lang("faster attacks; used in chase checks", "чаще атаки; проверки погони", "hizli saldiri; kovalamaca");
    const char *amulet_tip = pick_lang("+max HP and defense; storm checks", "+макс. ХП и защита; проверки бури", "+can ve savunma; firtina");
    (void)snprintf(force, sizeof force, "%s %d", stat_sabre(), run->body + run->bonus_force);
    (void)snprintf(speed, sizeof speed, "%s %d", stat_horse(), run->mind + run->bonus_speed);
    (void)snprintf(vigor, sizeof vigor, "%s %d", stat_amulet(), run->spirit + run->bonus_vigor);
    (void)snprintf(sta, sizeof sta, "%s %d/%d", pick_lang("HP", "ХП", "CAN"), run->stamina, run->stamina_max);
    (void)snprintf(cellinfo, sizeof cellinfo, "%s %d / %d", pick_lang("Cell", "Клетка", "Hucre"), run->cell + 1, run->path_cells);
    const int smax = (run->stamina_max > 0) ? run->stamina_max : 1;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(332), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 10,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), tj_hero_name(run), &s_panel_title);
        if (run->in_event) {
            draw_event_panel(g, run); /* the dice event takes over the stage; combat/idle render below otherwise */
        } else {
            /* Battle arena: hero vs enemy drawn by the sprite renderer (draw_battle_stage), with HP
             * bars / damage numbers / loot labels layered on top as Clay (battle_overlays). */
            CLAY({.id = CLAY_ID("battlestage"),
                  .layout = {.sizing = {CLAY_SIZING_FIXED(BAT_W), CLAY_SIZING_FIXED(BAT_H)}},
                  .backgroundColor = {58.0F, 42.0F, 28.0F, 255.0F},
                  .cornerRadius = CLAY_CORNER_RADIUS(12.0F),
                  .clip = {.horizontal = true, .vertical = true}}) {
                nt_ui_custom(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), &s_battle_tag);
                battle_overlays(g, run);
            }
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), sta, &s_stat);
            hp_bar((float)run->stamina / (float)smax, (Clay_Color){120.0F, 180.0F, 90.0F, 255.0F}, 250.0F);
            hero_stat_row(g, "stat_sabre_tip", g->icon_body_32, force, sabre_tip);
            hero_stat_row(g, "stat_horse_tip", g->icon_mind_32, speed, horse_tip);
            hero_stat_row(g, "stat_amulet_tip", g->icon_spirit_32, vigor, amulet_tip);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), cellinfo, &s_dim);
        }
    }
}

/* Run-over step 1 (right panel): the verdict, the sand line on death, how far the heir got,
 * then a button. Returns true once the player acknowledges and moves on to the aul. */
bool tj_view_death_panel(game_ctx_t *g, const tj_run_t *run) {
    static char res[40];
    (void)snprintf(res, sizeof res, "Дошёл до круга %d", run->circle);
    bool next = false;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(332), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(20),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 14,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), run->won ? "Петля разорвана!" : "Батыр пал", &s_panel_title);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), run->won ? "Имя батыра вписано в степь." : "Песок укрыл его следы, и всё, что он возвёл.", &s_dim);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), res, &s_stat);
        if (tj_button(g, "death_next", run->won ? "Дальше" : "В аул", 258, 56, TJ_BTN_PRIMARY)) {
            next = true;
        }
    }
    return next;
}

/* One aul upgrade as a wide row button: stat icon + name + what it raises + level + supplies cost.
   `lvl`/`cost` must outlive the frame (caller owns the buffers). Returns true if the buy fires. */
static bool aul_upgrade_button(game_ctx_t *g, const char *id_str, uint32_t icon, const char *name, const char *effect, const char *lvl, const char *cost, bool afford) {
    static const nt_ui_label_style_t s_up_name = {.font_id = 0, .font_size = 17, .color = {236.0F, 226.0F, 206.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT};
    static const nt_ui_label_style_t s_up_eff = {.font_id = 0, .font_size = 13, .color = {178.0F, 162.0F, 138.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT};
    static const nt_ui_label_style_t s_up_lvl = {.font_id = 0, .font_size = 12, .color = {170.0F, 156.0F, 132.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_RIGHT};
    const nt_ui_label_style_t s_up_cost = {
        .font_id = 0, .font_size = 18, .color = afford ? (Clay_Color){255.0F, 224.0F, 120.0F, 255.0F} : (Clay_Color){190.0F, 110.0F, 96.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_RIGHT};
    const uint32_t tint = afford ? 0xFF3A5A8AU : 0xFF323036U;
    const float op = afford ? 1.0F : 0.7F;
    const nt_ui_button_style_t style = {
        .idle = {.atlas = g->atlas, .bg_region = g->ui_button_dark_64, .bg_tint = tint, .scale = 1.0F, .opacity = op},
        .hover = {.bg_region = g->ui_button_dark_64, .bg_tint = tint, .scale = afford ? 1.03F : 1.0F, .opacity = op},
        .pressed = {.bg_region = g->ui_button_dark_64, .bg_tint = tint, .scale = 0.98F, .offset_y = 2.0F, .opacity = op},
        .disabled = {.bg_region = g->ui_button_dark_64, .bg_tint = tint, .scale = 1.0F, .opacity = 0.4F},
        .transition_speed = 12.0F,
        .hit_padding_lrtb = {6, 6, 4, 4},
        .slice9_scale = 1.0F,
    };
    const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(50)},
                                                     .padding = {12, 12, 4, 4},
                                                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                                                     .childGap = 10,
                                                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}};
    nt_ui_button_begin(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), nt_ui_id(id_str), &style, &decl, true);
    inline_sprite(g, icon, 30.0F, 30.0F);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 1}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), name, &s_up_name);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), effect, &s_up_eff);
    }
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 1, .childAlignment = {CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), lvl, &s_up_lvl);
        CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 4, .childAlignment = {CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER}}}) {
            inline_sprite(g, g->icon_supplies_32, 18.0F, 18.0F);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), cost, &s_up_cost);
        }
    }
    return nt_ui_button_end(g->ui);
}

/* Run-over step 2 (right panel): the aul — banked supplies + heritage upgrades + send the next
 * heir. Returns true if the player pressed "Отправить батыра". */
bool tj_view_aul_panel(game_ctx_t *g, const tj_run_t *run) {
    (void)run;
    static const nt_ui_label_style_t s_sup_big = {.font_id = 0, .font_size = 26, .color = {255.0F, 224.0F, 120.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT};
    static char sup[16];
    static char lvl[4][12];
    static char cost[4][12];
    static char eff[4][28];
    const int lv[4] = {g_aul.up_force, g_aul.up_speed, g_aul.up_vigor, g_aul.up_hand};
    const int c[4] = {tj_aul_upgrade_cost(0), tj_aul_upgrade_cost(1), tj_aul_upgrade_cost(2), tj_aul_upgrade_cost(3)};
    (void)snprintf(sup, sizeof sup, "%d", g_aul.supplies);
    (void)snprintf(eff[0], sizeof eff[0], "%s +%d", pick_lang("damage", "урон", "hasar"), tj_aul_stat_bonus(lv[0]));
    (void)snprintf(eff[1], sizeof eff[1], "%s +%d", pick_lang("speed", "скорость", "hiz"), tj_aul_stat_bonus(lv[1]));
    (void)snprintf(eff[2], sizeof eff[2], "%s +%d", pick_lang("defense", "защита", "savunma"), tj_aul_stat_bonus(lv[2]));
    (void)snprintf(eff[3], sizeof eff[3], "+%d %s", lv[3], pick_lang("in hand", "в руку", "ele"));
    for (int i = 0; i < 4; i++) {
        (void)snprintf(lvl[i], sizeof lvl[i], "ур.%d", lv[i]);
        (void)snprintf(cost[i], sizeof cost[i], "%d", c[i]);
    }
    bool newrun = false;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(332), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 7,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "Аул", &s_panel_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIT(0)},
                         .padding = {10, 10, 6, 6},
                         .layoutDirection = CLAY_LEFT_TO_RIGHT,
                         .childGap = 8,
                         .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {40.0F, 30.0F, 18.0F, 200.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(8.0F)}) {
            inline_sprite(g, g->icon_supplies_32, 26.0F, 26.0F);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), sup, &s_sup_big);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("supplies", "припасы", "erzak"), &s_dim);
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("Upgrades for the next batyr", "Прокачка для батыра", "Yukseltme"), &s_dim);
        if (aul_upgrade_button(g, "aul_f", g->icon_body_32, stat_sabre(), eff[0], lvl[0], cost[0], g_aul.supplies >= c[0])) {
            tj_aul_upgrade(0);
        }
        if (aul_upgrade_button(g, "aul_s", g->icon_mind_32, stat_horse(), eff[1], lvl[1], cost[1], g_aul.supplies >= c[1])) {
            tj_aul_upgrade(1);
        }
        if (aul_upgrade_button(g, "aul_v", g->icon_spirit_32, stat_amulet(), eff[2], lvl[2], cost[2], g_aul.supplies >= c[2])) {
            tj_aul_upgrade(2);
        }
        if (aul_upgrade_button(g, "aul_h", g->icon_deck_32, pick_lang("Hand", "Рука", "El"), eff[3], lvl[3], cost[3], g_aul.supplies >= c[3])) {
            tj_aul_upgrade(3);
        }
        if (tj_button(g, "aul_new", "Отправить батыра", 300, 64, TJ_BTN_PRIMARY)) {
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

/* Localized window text per event kind (start_event's pick: 0=Завал/Сила,
 * 1=Погоня/Скорость, 2=Буря/Выносливость). */
static void event_strings(int kind, const char **title, const char **desc, const char **stat) {
    switch (kind) {
    case 1:
        *title = pick_lang("Chase", "Погоня", "Takip");
        *desc = pick_lang("Enemies hard on your heels.", "Враги гонятся по пятам.", "Dusmanlar pesinde.");
        *stat = pick_lang("SPEED", "СКОРОСТЬ", "HIZ");
        return;
    case 2:
        *title = pick_lang("Storm", "Буря", "Firtina");
        *desc = pick_lang("A sandstorm saps your strength.", "Песчаная буря выматывает.", "Kum firtinasi yorar.");
        *stat = pick_lang("VIGOR", "ВЫНОСЛИВОСТЬ", "DAYANIKLILIK");
        return;
    default:
        *title = pick_lang("Rockfall", "Завал", "Heyelan");
        *desc = pick_lang("Stones block the trail.", "Камни перекрыли тропу.", "Taslar yolu kapadi.");
        *stat = pick_lang("STRENGTH", "СИЛА", "GUC");
        return;
    }
}

/* "Wheel of fate" event window, staged over run->event_t: first the tested stat
 * (title/threat/value), then the wheel spins and eases onto the precomputed roll
 * (each face = a multiplier; 1 = miss, max = win), then stat x mult vs DC, then the
 * outcome. Spin + chip positions are derived from event_t; the sim stores only the
 * result. The winning face rests under the fixed top pointer. */
/* Spin angle (radians) at reveal progress f: still during the intro beat, then a
 * cubic ease-out over [0.14, 0.66] that lands face `target` under the top pointer
 * (4 full turns first, so the deceleration reads as a real spin). */
static float event_spin_amount(float f, int target, int die) {
    const float two_pi = 6.2831853F;
    const float step = two_pi / (float)die;
    const float s_final = (two_pi * 4.0F) - ((float)target * step);
    const float lo = 0.14F;
    const float hi = 0.66F;
    if (f <= lo) {
        return 0.0F;
    }
    if (f >= hi) {
        return s_final;
    }
    const float local = (f - lo) / (hi - lo);
    const float inv = 1.0F - local;
    return (1.0F - (inv * inv * inv)) * s_final;
}

/* Post-spin math + outcome lines. ev_roll 1 = miss, max = win, else stat x mult vs DC. */
static void event_result_text(const tj_run_t *run, int die, const char *statname, char *mathline, size_t ml, char *compareline, size_t cl, char *resline, size_t rl) {
    const float mult = 1.0F + (g_config.event_dice_coeff * (float)run->ev_roll);
    int eff = (int)((float)run->ev_stat * mult);
    if (run->ev_roll <= 1) {
        eff = 0;
        (void)snprintf(mathline, ml, "%s %d * %s = 0", statname, run->ev_stat, pick_lang("FAIL", "ПРОВАЛ", "BASARISIZ"));
        (void)snprintf(compareline, cl, "%s 0 < %s %d", pick_lang("Total", "Итог", "Toplam"), pick_lang("Difficulty", "Сложность", "Zorluk"), run->ev_dc);
    } else if (run->ev_roll >= die) {
        (void)snprintf(mathline, ml, "%s %d * %s = %s", statname, run->ev_stat, pick_lang("SUCCESS", "УСПЕХ", "BASARI"), pick_lang("SUCCESS", "УСПЕХ", "BASARI"));
        (void)snprintf(compareline, cl, "%s: %s > %s %d", pick_lang("Total", "Итог", "Toplam"), pick_lang("SUCCESS", "УСПЕХ", "BASARI"), pick_lang("Difficulty", "Сложность", "Zorluk"), run->ev_dc);
    } else {
        (void)snprintf(mathline, ml, "%s %d * x%g = %d", statname, run->ev_stat, (double)mult, eff);
        (void)snprintf(compareline, cl, "%s %d %s %s %d", pick_lang("Total", "Итог", "Toplam"), eff, run->ev_pass ? ">=" : "<", pick_lang("Difficulty", "Сложность", "Zorluk"), run->ev_dc);
    }
    if (run->ev_pass) {
        (void)snprintf(resline, rl, "%s   +%d", pick_lang("SUCCESS", "УСПЕХ", "BASARI"), run->ev_gain);
    } else {
        (void)snprintf(resline, rl, "%s   -%d HP", pick_lang("FAIL", "ПРОВАЛ", "BASARISIZ"), g_config.event_fail_hp);
    }
}

/* One wheel face at (dx,dy) from the disc centre: roll = i+1 (1 = miss/red,
 * die = win/green, else a multiplier); `sel` = resting under the pointer. `text`
 * must outlive the frame — the caller owns the buffer (Clay stores it by pointer). */
static void event_wheel_label(game_ctx_t *g, int roll, int die, bool sel, float dx, float dy, char *text, size_t cap) {
    if (roll <= 1) {
        (void)snprintf(text, cap, "%s", pick_lang("FAIL", "ПРОВАЛ", "BASARISIZ"));
    } else if (roll >= die) {
        (void)snprintf(text, cap, "%s", pick_lang("SUCCESS", "УСПЕХ", "BASARI"));
    } else {
        (void)snprintf(text, cap, "x%g", (double)(1.0F + (g_config.event_dice_coeff * (float)roll)));
    }
    Clay_Color bg = {0.0F, 0.0F, 0.0F, 0.0F};
    Clay_Color bord = {0.0F, 0.0F, 0.0F, 0.0F};
    nt_ui_label_style_t st = s_wheel_chip;
    st.color = (Clay_Color){246.0F, 226.0F, 170.0F, sel ? 255.0F : 220.0F};
    float w = 50.0F;
    float h = 22.0F;
    if (sel) {
        bg = (Clay_Color){236.0F, 196.0F, 96.0F, 255.0F};
        bord = (Clay_Color){250.0F, 230.0F, 170.0F, 255.0F};
        st = s_wheel_chip_sel;
        st.font_size = 14;
    } else if (roll <= 1) {
        st.font_size = 11;
        st.color = (Clay_Color){255.0F, 214.0F, 188.0F, 190.0F};
        w = 54.0F;
    } else if (roll >= die) {
        st.font_size = 11;
        st.color = (Clay_Color){216.0F, 255.0F, 186.0F, 190.0F};
        w = 50.0F;
    } else {
        st.font_size = 12;
    }
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {dx, dy}, .zIndex = 62},
          .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = bg,
          .cornerRadius = CLAY_CORNER_RADIUS(5.0F),
          .border = {.color = bord, .width = CLAY_BORDER_OUTSIDE(sel ? 2 : 0)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &st);
    }
}

/* The fate wheel: a disc with the multiplier faces around the rim (rotated by
 * `spin`), the tested stat in the hub, and a fixed pointer at the top. */
static void event_wheel(game_ctx_t *g, float spin, int hi_idx, int die, int nchips, const char *hubval, char chiptext[][16]) {
    const float wheel = 214.0F;
    const float ring_r = 67.0F;
    const float step = 6.2831853F / (float)die;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(wheel), CLAY_SIZING_FIXED(wheel)}}, .backgroundColor = {26.0F, 20.0F, 13.0F, 0.0F}, .cornerRadius = CLAY_CORNER_RADIUS(wheel * 0.5F)}) {
        if (has_region(g->ui_fortune_wheel_384)) {
            static nt_ui_transform_t s_wheel_xf;
            s_wheel_xf = nt_ui_transform_defaults();
            s_wheel_xf.rotation_z = spin;
            const nt_ui_image_style_t img = nt_ui_image_style_defaults();
            const Clay_ElementDeclaration idecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(wheel), CLAY_SIZING_FIXED(wheel)}}};
            nt_ui_image(g->ui, nt_ui_make_element_data_xform(TJ_LAYER_IMG, NULL, &s_wheel_xf, 1.0F), g->atlas, g->ui_fortune_wheel_384, &img, &idecl);
        }
        for (int i = 0; i < nchips; i++) {
            const float ang = ((float)i * step) + spin;
            event_wheel_label(g, i + 1, die, i == hi_idx, ring_r * sinf(ang), -ring_r * cosf(ang), chiptext[i], sizeof chiptext[i]);
        }
        /* hub: the stat under test, shown from the first beat */
        CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .zIndex = 63},
              .layout = {.sizing = {CLAY_SIZING_FIXED(68.0F), CLAY_SIZING_FIXED(68.0F)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {62.0F, 46.0F, 28.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(34.0F),
              .border = {.color = {150.0F, 112.0F, 48.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), hubval, &s_wheel_hub);
        }
        /* fixed pointer near 12 o'clock — the face resting here is the result */
        CLAY({.floating =
                  {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER}, .offset = {0.0F, -101.0F}, .zIndex = 64},
              .layout = {.sizing = {CLAY_SIZING_FIXED(18.0F), CLAY_SIZING_FIXED(18.0F)}},
              .backgroundColor = {250.0F, 220.0F, 120.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(9.0F),
              .border = {.color = {92.0F, 54.0F, 18.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {}
    }
}

static void event_step(game_ctx_t *g, const char *text, bool active, bool done) {
    nt_ui_label_style_t st = s_wheel_chip;
    st.font_size = 13;
    st.color = active ? (Clay_Color){42.0F, 30.0F, 14.0F, 255.0F} : (Clay_Color){218.0F, 202.0F, 168.0F, 255.0F};
    Clay_Color bg = done ? (Clay_Color){72.0F, 104.0F, 56.0F, 235.0F} : (Clay_Color){54.0F, 42.0F, 28.0F, 225.0F};
    if (active) {
        bg = (Clay_Color){226.0F, 174.0F, 78.0F, 255.0F};
    }
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(82.0F), CLAY_SIZING_FIXED(26.0F)}, .padding = {5, 5, 2, 2}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = bg,
          .cornerRadius = CLAY_CORNER_RADIUS(5.0F),
          .border = {.color = {96.0F, 72.0F, 38.0F, 210.0F}, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &st);
    }
}

static void event_info_card(game_ctx_t *g, const char *label, const char *value, Clay_Color accent, uint32_t icon) {
    nt_ui_label_style_t lbl = s_dim;
    lbl.font_size = 14;
    nt_ui_label_style_t val = s_ev_value;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(136.0F), CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(8),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 3,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {48.0F, 36.0F, 24.0F, 245.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
          .border = {.color = accent, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), label, &lbl);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            if (has_region(icon)) {
                inline_sprite(g, icon, 24.0F, 24.0F);
            }
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), value, &val);
        }
    }
}

static uint32_t event_stat_icon(const game_ctx_t *g, int kind) {
    if (kind == 1) {
        return g->icon_mind_32;
    }
    if (kind == 2) {
        return g->icon_spirit_32;
    }
    return g->icon_body_32;
}

static void draw_event_panel(game_ctx_t *g, const tj_run_t *run) {
    static char chiptext[24][16];
    static char dcbuf[32];
    static char statbuf[40];
    static char phasebuf[64];
    static char hubval[8];
    static char mathline[88];
    static char compareline[96];
    static char resline[56];
    const float dur = (g_config.event_reveal_seconds > 0.1F) ? g_config.event_reveal_seconds : 3.0F;
    float f = (dur > 0.0F) ? (run->event_t / dur) : 1.0F; /* normalized reveal progress */
    if (f > 1.0F) {
        f = 1.0F;
    }
    const int die = (run->ev_die > 1) ? run->ev_die : 10;
    const int nchips = (die < 24) ? die : 24;
    const int target = ((run->ev_roll >= 1) ? run->ev_roll : 1) - 1; /* 0-based landing face */
    const float spin = event_spin_amount(f, target, die);
    const int hi_idx = (f >= 0.74F) ? target : -1; /* the winner highlights once it settles */
    const bool show_stat = f >= 0.22F;
    const bool show_wheel = f >= 0.40F;
    const bool show_math = f >= 0.78F;
    const bool show_res = f >= 0.90F;
    const char *title = NULL;
    const char *desc = NULL;
    const char *statname = NULL;
    event_strings(run->ev_kind, &title, &desc, &statname);
    statname = run->ev_statname[0] ? run->ev_statname : statname;
    (void)snprintf(dcbuf, sizeof dcbuf, "%d", run->ev_dc);
    (void)snprintf(statbuf, sizeof statbuf, "%s %d", statname, run->ev_stat);
    if (!show_stat) {
        (void)snprintf(phasebuf, sizeof phasebuf, "%s", pick_lang("Threat appears", "Сначала: событие и сложность", "Tehlike"));
    } else if (!show_wheel) {
        (void)snprintf(phasebuf, sizeof phasebuf, "%s", pick_lang("My stat answers", "Теперь: мой параметр", "Nitelik"));
    } else if (!show_math) {
        (void)snprintf(phasebuf, sizeof phasebuf, "%s", pick_lang("Wheel is rolling", "Колесо решает множитель", "Cark"));
    } else {
        (void)snprintf(phasebuf, sizeof phasebuf, "%s", pick_lang("Compare and resolve", "Сравнение и итог", "Sonuc"));
    }
    (void)snprintf(hubval, sizeof hubval, "%d", run->ev_stat);
    event_result_text(run, die, statname, mathline, sizeof mathline, compareline, sizeof compareline, resline, sizeof resline);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(300.0F), CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(10),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 8,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = {48.0F, 34.0F, 22.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(12.0F),
          .border = {.color = {126.0F, 88.0F, 40.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), title, &s_ev_title);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), desc, &s_dim);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), phasebuf, &s_ev_check);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            event_step(g, pick_lang("Event", "Событие", "Olay"), !show_stat, true);
            event_step(g, pick_lang("Stat", "Параметр", "Nitelik"), show_stat && !show_wheel, show_wheel);
            event_step(g, pick_lang("Wheel", "Колесо", "Cark"), show_wheel && !show_math, show_math);
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            event_info_card(g, pick_lang("Difficulty", "Сложность", "Zorluk"), dcbuf, (Clay_Color){202.0F, 114.0F, 78.0F, 255.0F}, 0U);
            event_info_card(g, pick_lang("My stat", "Мой параметр", "Nitelik"), show_stat ? statbuf : "?", (Clay_Color){116.0F, 176.0F, 210.0F, 255.0F},
                            show_stat ? event_stat_icon(g, run->ev_kind) : 0U);
        }
        /* Reserve the wheel + result space from the first frame so the staged reveal never grows
           the card (the column was visibly jumping down then back up as lines appeared). */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(214.0F)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            if (show_wheel) {
                event_wheel(g, spin, hi_idx, die, nchips, hubval, chiptext);
            }
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64.0F)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 3, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}}}) {
            if (show_math) {
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), mathline, &s_ev_check);
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), compareline, &s_stat);
            }
            if (show_res) {
                nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), resline, run->ev_pass ? &s_ev_good : &s_ev_bad);
            }
        }
    }
}

/* Both combat and the dice event now render inside the hero panel (the unified stage),
 * so this scene-level overlay is intentionally empty. Kept for API/back-compat. */
void tj_view_action_overlay(game_ctx_t *g, const tj_run_t *run) {
    (void)g;
    (void)run;
}

/* Hand fan layout (screen/logical coords). The fan is pinned to the viewport
 * bottom and centred, so it tracks window resizes instead of drifting off. */
#define FAN_CARD_W 120.0F
#define FAN_CARD_H 150.0F
#define FAN_STEP 86.0F      /* per-card step (overlapping fan) */
#define FAN_LIFT 34.0F      /* hovered/selected card rises this much */
#define FAN_BOTTOM 190.0F   /* resting top-left y measured up from the viewport bottom */
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

/* One hand card at an absolute screen position (floats to root so the fan can overlap and a
 * card can lift). Shows the building PICTURE + NAME (type) + INFO line (level + effect). All
 * children are non-floating: floating-to-parent images don't render in this Clay pass, which
 * is why cards used to come up blank. `info` is the caller's "Ур.N · +M ..." string. */
static void draw_fan_card(game_ctx_t *g, int tile, float x, float y, bool active, const nt_ui_transform_t *xf, const char *info) {
    const bool has = tile >= 0 && tile < g_config.tile_count;
    const uint32_t art = has ? card_art_region_for_id(g, g_config.tiles[tile].id) : g->ui_card_back_96x128;
    const Clay_Color lc = line_color(has ? g_config.tiles[tile].line : 0);
    const Clay_Color bg = active ? (Clay_Color){82.0F, 68.0F, 38.0F, 255.0F} : (Clay_Color){46.0F, 36.0F, 26.0F, 255.0F};
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(FAN_CARD_W), CLAY_SIZING_FIXED(FAN_CARD_H)},
                     .padding = CLAY_PADDING_ALL(7),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 4,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}, .offset = {x, y}, .zIndex = active ? 36 : 20},
          .backgroundColor = bg,
          .cornerRadius = CLAY_CORNER_RADIUS(10.0F),
          .border = {.color = active ? (Clay_Color){198.0F, 154.0F, 55.0F, 255.0F} : (Clay_Color){104.0F, 76.0F, 42.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)},
          .userData = (void *)NT_UI_DATA_XFORM(0U, xf, 1.0F)}) {
        /* picture box tinted by family colour -> the line reads even before the name */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(70.0F)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {lc.r * 0.5F, lc.g * 0.5F, lc.b * 0.5F, has ? 255.0F : 60.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(7.0F)}) {
            inline_sprite(g, art, 58.0F, 58.0F);
        }
        /* auto-shrink the name + info so long building names never clip the card */
        const char *nm = has ? g_config.tiles[tile].name : "";
        const float tw = FAN_CARD_W - 16.0F;
        const uint16_t nsz = nt_ui_fit_width(g->ui, s_card_name.font_id, nm, tw, 12, (uint16_t)s_card_name.font_size, (float)s_card_name.letter_tracking);
        nt_ui_label_sized(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), nm, &s_card_name, (float)nsz);
        const char *inf = info ? info : "";
        const uint16_t isz = nt_ui_fit_width(g->ui, s_card_eff.font_id, inf, tw, 10, (uint16_t)s_card_eff.font_size, (float)s_card_eff.letter_tracking);
        nt_ui_label_sized(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), inf, &s_card_eff, (float)isz);
    }
}

/* Filled dot at a screen position (the drag arrow's dotted trail). */
static void floating_screen_dot(float cx, float cy, float d, Clay_Color col) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(d), CLAY_SIZING_FIXED(d)}},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_TOP}, .offset = {cx, cy}, .zIndex = 38},
          .backgroundColor = col,
          .cornerRadius = CLAY_CORNER_RADIUS(d * 0.5F)}) {}
}

/* Small floating pill at a logical (root-space) point: rounded chip + centred text.
 * Used for the per-building level number and the drag "+N" merge-result badge. */
static void floating_pill(game_ctx_t *g, float lx, float ly, float w, float h, const char *text, Clay_Color bg, Clay_Color border, const nt_ui_label_style_t *style, int16_t z) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_TOP}, .offset = {lx, ly}, .zIndex = z},
          .backgroundColor = bg,
          .cornerRadius = CLAY_CORNER_RADIUS(h * 0.5F),
          .border = {.color = border, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, style);
    }
}

/* Per-building Clay overlays on top of the sprite world: the tier-level number on every
 * placed merge building, plus a gold crown (diamond) on maxed buildings (no higher tier).
 * Positioned from last frame's map transform (1-frame lag is invisible — same basis the
 * drag arrow uses). Called at the scene root, after the map element. */
void tj_view_field_badges(game_ctx_t *g, const tj_run_t *run) {
    if (run->grid_cols < 2 || s_map_pitch <= 0.0F) {
        return;
    }
    static char s_num[TJ_ZONE_CELLS][4];
    const int cols = run->grid_cols;
    const int rows = run->grid_rows;
    const int n = cols * rows;
    const float pitch = s_map_pitch;
    const float bw = fmaxf(18.0F, pitch * 0.36F);
    const float ins_x = (s_map_vw * 0.5F) - (pitch * 0.3F); /* keep badges inside the map area, off the side panels */
    const float ins_y = (s_map_vh * 0.5F) - (pitch * 0.3F);
    for (int i = 0; i < n && i < TJ_ZONE_CELLS; i++) {
        const int tidx = run->field_tile[i];
        if (tidx < 0 || tidx >= g_config.tile_count || g_config.tiles[tidx].line <= 0) {
            continue;
        }
        const int gx = i % cols;
        const int gy = i / cols;
        const float cxp = grid_x(gx, cols, pitch);
        const float cyp = grid_y(gy, rows, pitch);
        if (fabsf(cxp) > ins_x || fabsf(cyp) > ins_y) {
            continue;
        }
        const float lx = s_map_cx + cxp;
        const float ly = s_map_cy + cyp;
        (void)snprintf(s_num[i], sizeof s_num[i], "%d", g_config.tiles[tidx].tier);
        const bool maxed = tj_config_tile_upgrade(tidx) < 0;
        if (maxed) {
            static nt_ui_transform_t s_crown_xf;
            s_crown_xf = nt_ui_transform_defaults();
            s_crown_xf.rotation_z = 0.785F; /* 45deg -> diamond "crown" jewel */
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(bw * 0.62F), CLAY_SIZING_FIXED(bw * 0.62F)}},
                  .floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                               .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                               .offset = {lx, ly - (pitch * 0.44F)},
                               .zIndex = 14},
                  .backgroundColor = {TJ_GOLD.r, TJ_GOLD.g, TJ_GOLD.b, 255.0F},
                  .cornerRadius = CLAY_CORNER_RADIUS(2.0F),
                  .userData = (void *)NT_UI_DATA_XFORM(0U, &s_crown_xf, 1.0F)}) {}
            floating_pill(g, lx + (pitch * 0.28F), ly + (pitch * 0.28F), bw, bw, s_num[i], (Clay_Color){TJ_GOLD.r, TJ_GOLD.g, TJ_GOLD.b, 255.0F}, (Clay_Color){120.0F, 86.0F, 24.0F, 255.0F},
                          &s_badge_num_max, 14);
        } else {
            floating_pill(g, lx + (pitch * 0.28F), ly + (pitch * 0.28F), bw, bw, s_num[i], (Clay_Color){34.0F, 26.0F, 18.0F, 235.0F}, (Clay_Color){198.0F, 166.0F, 96.0F, 230.0F}, &s_badge_num, 14);
        }
    }
}

/* Hover tooltip: when not dragging, hovering a placed building shows a small card near the
 * cursor with its name + level + what it gives. Reads the last frame's map transform. */
void tj_view_field_tooltip(game_ctx_t *g, const tj_run_t *run) {
    if (g->drag_tile >= 0 || s_map_pitch <= 0.0F) {
        return;
    }
    int gx = -1;
    int gy = -1;
    if (!tj_view_world_cell_at(g->ptr_x, g->ptr_y, &gx, &gy)) {
        return;
    }
    const int idx = (gy * run->grid_cols) + gx;
    if (idx < 0 || idx >= TJ_ZONE_CELLS) {
        return;
    }
    const int t = run->field_tile[idx];
    if (t < 0 || t >= g_config.tile_count || g_config.tiles[t].line <= 0) {
        return;
    }
    static char nm[72];
    static char eff[40];
    tile_effect_str(t, eff, sizeof eff);
    (void)snprintf(nm, sizeof nm, "%s  %s%d", g_config.tiles[t].name, pick_lang("lv.", "ур.", "sv."), g_config.tiles[t].tier);
    /* Edge-aware: in the right half grow toward centre (off the hero panel); near the bottom grow up
       (off the hand). The cursor anchor flips so the box never bleeds onto the side panels. */
    const bool flip_x = g->ptr_x > g->logical_w * 0.5F;
    const bool flip_y = g->ptr_y > g->logical_h * 0.58F;
    Clay_FloatingAttachPointType ap = CLAY_ATTACH_POINT_LEFT_TOP;
    if (flip_x && flip_y) {
        ap = CLAY_ATTACH_POINT_RIGHT_BOTTOM;
    } else if (flip_x) {
        ap = CLAY_ATTACH_POINT_RIGHT_TOP;
    } else if (flip_y) {
        ap = CLAY_ATTACH_POINT_LEFT_BOTTOM;
    }
    const float ox = flip_x ? (g->ptr_x - 18.0F) : (g->ptr_x + 18.0F);
    const float oy = flip_y ? (g->ptr_y - 18.0F) : (g->ptr_y + 18.0F);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .padding = {10, 10, 8, 8}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 3},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = ap, .parent = CLAY_ATTACH_POINT_LEFT_TOP}, .offset = {ox, oy}, .zIndex = 45},
          .backgroundColor = {28.0F, 22.0F, 14.0F, 242.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
          .border = {.color = {150.0F, 116.0F, 54.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), nm, &s_tip_name);
        if (eff[0] != '\0') {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), eff, &s_tip_eff);
        }
    }
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
    static char s_ghost_info[40];
    const int gt = run->hand_cards[drag_idx];
    char geff[28];
    tile_effect_str(gt, geff, sizeof geff);
    const char *glv = pick_lang("lv.", "ур.", "sv.");
    if (geff[0] != '\0') {
        (void)snprintf(s_ghost_info, sizeof s_ghost_info, "%s%d  %s", glv, g_config.tiles[gt].tier, geff);
    } else {
        (void)snprintf(s_ghost_info, sizeof s_ghost_info, "%s%d", glv, g_config.tiles[gt].tier);
    }
    draw_fan_card(g, gt, px - (FAN_CARD_W * 0.5F), py - (FAN_CARD_H * 0.6F), true, &s_ghost_xf, s_ghost_info);
    /* Drop would fuse: a gold pill with the resulting level above the target cell
     * (pairs with the result-ghost sprite drawn in the world pass). */
    int hgx = -1;
    int hgy = -1;
    if (g->drag_tile >= 0 && s_map_pitch > 0.0F && tj_view_world_cell_at(px, py, &hgx, &hgy)) {
        int result = -1;
        (void)tj_run_merge_preview(run, g->drag_tile, hgx, hgy, NULL, 0, &result);
        if (result >= 0) {
            static char s_res[6];
            (void)snprintf(s_res, sizeof s_res, "%d", g_config.tiles[result].tier);
            const float lx = s_map_cx + grid_x(hgx, run->grid_cols, s_map_pitch);
            const float ly = s_map_cy + grid_y(hgy, run->grid_rows, s_map_pitch);
            const float bw = fmaxf(24.0F, s_map_pitch * 0.42F);
            floating_pill(g, lx, ly - (s_map_pitch * 0.46F), bw, bw, s_res, (Clay_Color){TJ_GOLD.r, TJ_GOLD.g, TJ_GOLD.b, 255.0F}, (Clay_Color){120.0F, 86.0F, 24.0F, 255.0F}, &s_badge_num_max, 40);
        }
    }
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
                hint = pick_lang("drag a card onto a green cell; line up 3 alike to merge", "тяни карту на зелёную клетку , собери 3 одинаковых рядом -> апгрейд", "karti yesil hucreye surukle");
            } else if (run->pouch > 0) {
                hint = pick_lang("tap the Pouch to draw a card", "жми «Мешочек» — вытяни карту", "kart cek");
            } else {
                hint = pick_lang("win fights and finish laps to fill the pouch", "побеждай и проходи круги — мешочек копится", "savaslari kazan");
            }
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), hint, &s_dim);
        }
    }
    static nt_ui_transform_t s_fan_xf[TJ_MAX_HAND];
    static char s_card_info[TJ_MAX_HAND][40]; /* "Ур.N · +M ..." per card; must outlive the frame */
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
        const int t = run->hand_cards[i];
        char eff[28];
        tile_effect_str(t, eff, sizeof eff);
        const char *lv = pick_lang("lv.", "ур.", "sv.");
        if (eff[0] != '\0') {
            (void)snprintf(s_card_info[i], sizeof s_card_info[i], "%s%d  %s", lv, g_config.tiles[t].tier, eff);
        } else {
            (void)snprintf(s_card_info[i], sizeof s_card_info[i], "%s%d", lv, g_config.tiles[t].tier);
        }
        draw_fan_card(g, t, fx0 + ((float)i * FAN_STEP), (lift ? fy - FAN_LIFT : fy) + yarc, lift, &s_fan_xf[i], s_card_info[i]);
    }
}

/* Small "?" help button, top-right. Returns true on click (toggle the help modal). */
bool tj_view_help_button(game_ctx_t *g) {
    bool clicked = false;
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP}, .offset = {-12.0F, 12.0F}, .zIndex = 52},
          .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) {
        clicked = tj_button(g, "help_q", "?", 40, 40, TJ_BTN_SECONDARY);
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
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("The batyr walks the loop and fights on his own.", "Батыр идёт по кругу и сам сражается.", "Batır kendi savasir."), &s_stat);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT),
                        pick_lang("- Pouch: draw a card, drag it onto a green field cell", "- «Мешочек»: вытяни карту и тяни её на зелёную клетку поля", "- torba"), &s_stat);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT),
                        pick_lang("- 3 alike in a row merge into a stronger one (upgrades you)", "- 3 одинаковых рядом сливаются в сильнее (качают героя)", "- 3 ayni"), &s_stat);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT),
                        pick_lang("- Events spin a wheel of fate; a boss waits at the end of each lap", "- События — колесо судьбы; в конце круга — босс", "- olaylar"), &s_stat);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("- On death: upgrade the aul on the right, then a new run", "- Умер -> прокачай аул справа -> новый забег", "- aul"),
                        &s_stat);
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

    /* Pulsing ring on the target element (pouch for step 2 = pull, field for 3-4 = place/merge). */
    float hx = 0.0F;
    float hy = 0.0F;
    float hw = 0.0F;
    float hh = 0.0F;
    bool has_hl = false;
    if (step == 2) {
        hx = 94.0F;
        hy = vh - 130.0F;
        hw = 198.0F;
        hh = 104.0F;
        has_hl = true;
    } else if (step == 3 && g->drag_tile < 0) {
        const float cardx = fmaxf((vw - 100.0F) * 0.5F, 292.0F); /* ring the source card only while idle (drag takes over) */
        hx = cardx - 6.0F;
        hy = vh - 170.0F;
        hw = 112.0F;
        hh = 136.0F;
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
    if (step == 2 && has_region(g->ui_finger_pointer_128)) { /* finger taps down onto the «Мешочек» button */
        const float bob = 4.0F + (8.0F * (0.5F + (0.5F * sinf(t * 4.0F))));
        nt_ui_image_style_t fimg = nt_ui_image_style_defaults();
        fimg.flip_bits = 2U; /* NT_SPRITE_FLAG_FLIP_Y: point the hand down at the pouch below it */
        const Clay_ElementDeclaration fdecl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(64.0F), CLAY_SIZING_FIXED(76.0F)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                         .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                         .offset = {hx + 66.0F, hy - 80.0F - bob},
                         .zIndex = 86,
                         .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
        };
        nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, g->ui_finger_pointer_128, &fimg, &fdecl);
    }
    if (step == 3 && g->drag_tile < 0 && s_ftue_gap_gx >= 0 && s_map_pitch > 0.0F && has_region(g->ui_finger_pointer_128)) {
        /* A finger glides from the source card up to the gold gap, on a loop — shows HOW to drag. */
        const float cardx = fmaxf((vw - 100.0F) * 0.5F, 292.0F);
        const float sx = cardx + 44.0F;
        const float sy = vh - 120.0F;
        const float ex = s_map_cx + grid_x(s_ftue_gap_gx, run->grid_cols, s_map_pitch);
        const float ey = s_map_cy + grid_y(s_ftue_gap_gy, run->grid_rows, s_map_pitch);
        const float cyc = fmodf(t * 0.5F, 1.0F);
        const float ease = cyc * cyc * (3.0F - (2.0F * cyc));
        const float fx = sx + ((ex - sx) * ease);
        const float fy = sy + ((ey - sy) * ease);
        float fade = 1.0F;
        if (cyc < 0.12F) {
            fade = cyc / 0.12F;
        } else if (cyc > 0.82F) {
            fade = (1.0F - cyc) / 0.18F;
        }
        const uint32_t a = (uint32_t)(fade * 255.0F);
        nt_ui_image_style_t fimg = nt_ui_image_style_defaults();
        fimg.color_packed = (a << 24) | 0x00FFFFFFU; /* 0xAABBGGRR: fade the hand in/out across the glide */
        const Clay_ElementDeclaration fdecl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(60.0F), CLAY_SIZING_FIXED(72.0F)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                         .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                         .offset = {fx, fy},
                         .zIndex = 86,
                         .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
        };
        nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, g->ui_finger_pointer_128, &fimg, &fdecl);
    }

    const char *body;
    switch (step) {
    case 1:
        body = pick_lang("The batyr walks and fights on his own. Win a fight to earn your first Pouch.", "Батыр идёт и сам бьётся. Победи в бою — добудь первый мешочек.",
                         "Batır kendi savasir. Ilk torbayı kazan.");
        break;
    case 2:
        body = pick_lang("Victory! The Pouch holds a tile-card. Tap the Pouch (bottom-left) to draw it.", "Победа! В мешочке — карта-тайл. Нажми «Мешочек» внизу слева и вытяни её.",
                         "Zafer! Torbaya bas, kart cek.");
        break;
    case 3:
        body = pick_lang("Drag the tile onto the glowing cell - makes 3 in a row, they merge and raise a stat.",
                         "Перетащи тайл на подсвеченную клетку — выйдет 3 в ряд, они сольются и поднимут стат батыра.", "Karti parlayan hucreye surukle.");
        break;
    default:
        body = pick_lang("Done! Survive, clear laps, beat bosses. If you fall, upgrade the aul and send a new batyr. Good luck!",
                         "Готово! Выживай, проходи круги, бей боссов. Погиб — прокачай аул и пошли нового батыра. Удачи!", "Hazir! Hayatta kal, tur gec, boss yen. Iyi sanslar!");
        break;
    }

    static char stepnum[24];
    if (step == 1) {
        (void)snprintf(stepnum, sizeof stepnum, "%s", pick_lang("Combat", "Бой", "Savas"));
    } else {
        int shown_step = step - 1; /* pull = 1, place+merge = 2 */
        if (shown_step < 1) {
            shown_step = 1;
        } else if (shown_step > 2) {
            shown_step = 2;
        }
        (void)snprintf(stepnum, sizeof stepnum, "%s %d/2", pick_lang("Tutorial", "Обучение", "Egitim"), shown_step);
    }
    const bool action_step = (step >= 4);
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
            if (action_step && tj_button(g, "ftue_next", pick_lang("Play", "Играть", "Oyna"), 200, 50, TJ_BTN_PRIMARY)) {
                result = 1;
            }
        }
    }
    return result;
}

// #region first-run intro (dawn reveal -> send the wayfarer from the aul)
/* The black screen, drifting sand and the dawn-reveal veil are drawn in the sprite world
 * pass (draw_intro_fx). Clay carries only the intro UI (text, buttons, finger). */

/* Right "launch" panel (first run): the hero waits at the fire; the player sends him off.
 * Once the reveal clears, a pulsing ring + a tutorial finger draw the eye to the button.
 * Returns true on press. The same verb repeats in the aul panel (unified launch ritual). */
bool tj_view_launch_panel(game_ctx_t *g, const tj_run_t *run, float t) {
    (void)run;
    bool send = false;
    const bool armed = t >= TJ_REVEAL_SECONDS; /* button cues appear after the world is revealed */
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(332), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 12,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = TJ_PANEL_BG}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("Batyr of the clan", "Батыр рода", "Soyun batırı"), &s_intro_title);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("Ready at the fire.", "Готов у костра.", "Ates basinda."), &s_intro_sub);
        CLAY({.id = CLAY_ID("intro_send_box"), .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) {
            send = tj_button(g, "intro_send", pick_lang("Set out", "Отправить батыра", "Yola cikar"), 258, 76, TJ_BTN_PRIMARY);
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
        nt_ui_image_style_t fimg = nt_ui_image_style_defaults();
        fimg.color_packed = 0xFFFFFFFFU; /* untinted: black-outline hand renders as-is */
        const Clay_ElementDeclaration fdecl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(75.0F), CLAY_SIZING_FIXED(88.0F)}}, /* keep the hand's 160x188 aspect */
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
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_banner);
    }
}

/* First-run black-screen open (UI layer only): the opening lines fade in, then a
 * "tap to continue" prompt + finger. The black backdrop + drifting sand are drawn by
 * the sprite world pass (draw_intro_fx); Clay carries UI only. The tap unlocks web audio
 * (main.c resume) and starts the dawn reveal. t = seconds since the screen appeared. */
void tj_view_intro_black(game_ctx_t *g, float t) {
    const float a1 = (t < 0.4F) ? (t / 0.4F) : 1.0F;                                /* "следы" fades in 0..0.4 */
    const float a2 = (t < 1.0F) ? 0.0F : ((t < 1.6F) ? ((t - 1.0F) / 0.6F) : 1.0F); /* "путника" 1.0..1.6 */
    const nt_ui_label_style_t l1 = {.font_id = 0, .font_size = 32, .color = {236.0F, 214.0F, 170.0F, 255.0F * a1}, .align = CLAY_TEXT_ALIGN_CENTER};
    const nt_ui_label_style_t l2 = {.font_id = 0, .font_size = 26, .color = {214.0F, 188.0F, 150.0F, 255.0F * a2}, .align = CLAY_TEXT_ALIGN_CENTER};
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER},
                       .zIndex = 90,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 16, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("Sand erases every footprint.", "Песок стирает следы.", "Kum izleri siler."), &l1);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("The path awaits its first batyr.", "Путь ждёт первого батыра.", "Yol ilk batırını bekliyor."), &l2);
    }
    if (t < 1.9F) {
        return; /* let the lines land before inviting the tap */
    }
    const float pulse = 0.55F + (0.45F * sinf(t * 3.2F));
    const nt_ui_label_style_t hint = {.font_id = 0, .font_size = 18, .color = {198.0F, 178.0F, 140.0F, 120.0F + (135.0F * pulse)}, .align = CLAY_TEXT_ALIGN_CENTER};
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM},
                       .offset = {0.0F, -84.0F},
                       .zIndex = 91,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
          .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), pick_lang("tap to continue", "нажми, чтобы продолжить", "devam icin dokun"), &hint);
    }
    if (has_region(g->ui_finger_pointer_128)) {
        const float bob = sinf(t * 3.2F) * 7.0F;
        nt_ui_image_style_t fimg = nt_ui_image_style_defaults();
        fimg.color_packed = 0xFFFFFFFFU; /* untinted: black-outline hand renders as-is */
        const Clay_ElementDeclaration fdecl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(72.0F), CLAY_SIZING_FIXED(84.0F)}}, /* keep the hand's 160x188 aspect */
            .floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                         .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM},
                         .offset = {0.0F, -176.0F + bob},
                         .zIndex = 92,
                         .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
        };
        nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, g->ui_finger_pointer_128, &fimg, &fdecl);
    }
}
// #endregion

// #region settings modal
#define TJ_RESET_HOLD_SECONDS 1.5F

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
    g->request_restart = true; /* scene_game starts a fresh run next frame (intro replays: ftue_done was wiped) */
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
            .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}, .padding = CLAY_PADDING_ALL(6), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}};
        nt_ui_button_begin(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), nt_ui_id("settings_gear"), &style, &decl, true);
        if (g->icon_settings_32 != 0U && g->icon_settings_32 != NT_ATLAS_INVALID_REGION) {
            const nt_ui_image_style_t img = nt_ui_image_style_defaults();
            const Clay_ElementDeclaration idecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(28), CLAY_SIZING_FIXED(28)}}};
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
