#include <stdbool.h>
#include <stdint.h>

#include "clay.h"

#include "atlas/nt_atlas.h"
#include "game.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_label.h"

static const nt_ui_label_style_t s_title = {.font_id = 0, .font_size = 20, .color = {246.0F, 222.0F, 176.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_LEFT};
static const nt_ui_label_style_t s_label = {.font_id = 0, .font_size = 13, .color = {210.0F, 216.0F, 228.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_dim = {.font_id = 0, .font_size = 12, .color = {132.0F, 144.0F, 164.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_bad = {.font_id = 0, .font_size = 12, .color = {255.0F, 156.0F, 132.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_chip = {.font_id = 0, .font_size = 14, .color = {232.0F, 226.0F, 210.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};

static bool has_region(uint32_t region) { return region != 0U && region != NT_ATLAS_INVALID_REGION; }

static void qa_label(game_ctx_t *g, const char *text) { nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_label); }
static void qa_dim(game_ctx_t *g, const char *text) { nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_dim); }

static void qa_image(game_ctx_t *g, uint32_t region, float w, float h, const char *fallback) {
    if (!has_region(region)) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {76.0F, 36.0F, 40.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(3.0F),
              .border = {.color = {224.0F, 92.0F, 92.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(1)}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), fallback, &s_bad);
        }
        return;
    }
    const nt_ui_image_style_t img = nt_ui_image_style_defaults();
    const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}}};
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
}

static void qa_float_image(game_ctx_t *g, uint32_t region, float w, float h, float ox, float oy, int16_t z) {
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

#define QA_SECTION(w, h)                                                                                                                                                                               \
    {                                                                                                                                                                                                  \
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .padding = CLAY_PADDING_ALL(8), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6},                                      \
        .backgroundColor = {18.0F, 22.0F, 34.0F, 245.0F}, .cornerRadius = CLAY_CORNER_RADIUS(6.0F), .border = {                                                                                        \
            .color = {58.0F, 68.0F, 88.0F, 255.0F},                                                                                                                                                    \
            .width = CLAY_BORDER_OUTSIDE(1)                                                                                                                                                            \
        }                                                                                                                                                                                              \
    }

static void qa_icon_chip(game_ctx_t *g, uint32_t icon, const char *text) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(88), CLAY_SIZING_FIXED(34)},
                     .padding = {6, 8, 0, 0},
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 5,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {34.0F, 40.0F, 58.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
        qa_image(g, icon, 24.0F, 24.0F, "miss");
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_chip);
    }
}

static void draw_gameplay_composition(game_ctx_t *g) {
    CLAY(QA_SECTION(350.0F, 278.0F)) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "QA gameplay composition", &s_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(332), CLAY_SIZING_FIXED(230)}}, .backgroundColor = {28.0F, 30.0F, 34.0F, 255.0F}, .cornerRadius = CLAY_CORNER_RADIUS(4.0F)}) {
            for (int gy = 0; gy < 5; gy++) {
                for (int gx = 0; gx < 6; gx++) {
                    const float x = ((float)gx - 2.5F) * 48.0F;
                    const float y = ((float)gy - 2.0F) * 42.0F;
                    qa_float_image(g, g->ground_sand_base, 46.0F, 40.0F, x, y, 0);
                }
            }
            qa_float_image(g, g->decor_dune, 38.0F, 36.0F, -120.0F, -76.0F, 1);
            qa_float_image(g, g->decor_stones, 38.0F, 36.0F, 120.0F, -76.0F, 1);
            qa_float_image(g, g->decor_dry_grass, 38.0F, 36.0F, -72.0F, 76.0F, 1);
            qa_float_image(g, g->decor_tracks, 38.0F, 36.0F, 72.0F, 76.0F, 1);
            qa_float_image(g, g->decor_bones, 38.0F, 36.0F, -120.0F, 34.0F, 1);
            qa_float_image(g, g->decor_cracks, 38.0F, 36.0F, 120.0F, 34.0F, 1);

            qa_float_image(g, g->buffer_edge_stones, 46.0F, 40.0F, -72.0F, -42.0F, 2);
            qa_float_image(g, g->buffer_packed_sand, 46.0F, 40.0F, 72.0F, -42.0F, 2);
            qa_float_image(g, g->buffer_stakes, 46.0F, 40.0F, -72.0F, 42.0F, 2);
            qa_float_image(g, g->buffer_cart_marks, 46.0F, 40.0F, 72.0F, 42.0F, 2);

            qa_float_image(g, g->road_entry_aul, 50.0F, 44.0F, -50.0F, 0.0F, 3);
            qa_float_image(g, g->road_straight_ew, 50.0F, 44.0F, 0.0F, 0.0F, 3);
            qa_float_image(g, g->road_corner_es, 50.0F, 44.0F, 50.0F, 0.0F, 3);
            qa_float_image(g, g->road_straight_ns, 50.0F, 44.0F, 50.0F, 42.0F, 3);
            qa_float_image(g, g->road_corner_sw, 50.0F, 44.0F, 0.0F, 42.0F, 3);
            qa_float_image(g, g->road_corner_wn, 50.0F, 44.0F, -50.0F, 42.0F, 3);
            qa_float_image(g, g->road_corner_ne, 50.0F, 44.0F, -50.0F, -42.0F, 3);
            qa_float_image(g, g->road_current_highlight, 50.0F, 44.0F, 0.0F, 0.0F, 5);

            qa_float_image(g, g->aul_ground_2x2, 82.0F, 76.0F, -4.0F, -76.0F, 3);
            qa_float_image(g, g->aul_yurt_small_01, 42.0F, 40.0F, -26.0F, -78.0F, 4);
            qa_float_image(g, g->aul_yurt_small_02, 42.0F, 40.0F, 18.0F, -68.0F, 4);
            qa_float_image(g, g->aul_fire_01, 32.0F, 32.0F, 6.0F, -88.0F, 5);
            qa_float_image(g, g->hero_wayfarer_idle_s, 46.0F, 56.0F, 0.0F, 0.0F, 6);
        }
    }
}

static void active_tile_cell(game_ctx_t *g, uint32_t tile, uint32_t decor, const char *name) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(78), CLAY_SIZING_FIXED(92)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(62), CLAY_SIZING_FIXED(62)}}, .backgroundColor = {38.0F, 34.0F, 28.0F, 255.0F}, .cornerRadius = CLAY_CORNER_RADIUS(4.0F)}) {
            qa_float_image(g, g->ground_sand_base, 62.0F, 62.0F, 0.0F, 0.0F, 0);
            qa_float_image(g, decor, 56.0F, 56.0F, 0.0F, 0.0F, 1);
            qa_float_image(g, tile, 56.0F, 56.0F, 0.0F, 0.0F, 2);
        }
        qa_dim(g, name);
    }
}

static void draw_active_tiles(game_ctx_t *g) {
    CLAY(QA_SECTION(350.0F, 188.0F)) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "Active tiles over ground/decor", &s_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5}}) {
            active_tile_cell(g, g->tile_saxaul, g->decor_dry_grass, "saxaul");
            active_tile_cell(g, g->tile_yurt, g->decor_tracks, "yurt");
            active_tile_cell(g, g->tile_tamga_stone, g->decor_stones, "tamga");
            active_tile_cell(g, g->tile_wolf_track, g->decor_cracks, "trail");
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5}}) {
            active_tile_cell(g, g->tile_oasis, g->decor_dune, "oasis");
            active_tile_cell(g, g->tile_mirage, g->decor_tracks, "mirage");
            active_tile_cell(g, g->tile_storm, g->decor_cracks, "storm");
            active_tile_cell(g, g->tile_last_tamga, g->decor_bones, "last");
        }
    }
}

static void card_preview(game_ctx_t *g, uint32_t surface, uint32_t art, uint32_t placement, const char *name, bool selected) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(112), CLAY_SIZING_FIXED(126)},
                     .padding = CLAY_PADDING_ALL(7),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 3,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = selected ? (Clay_Color){48.0F, 58.0F, 42.0F, 255.0F} : (Clay_Color){28.0F, 32.0F, 44.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
        qa_float_image(g, surface, 104.0F, 118.0F, 0.0F, 0.0F, 0);
        qa_float_image(g, art, 68.0F, 68.0F, 0.0F, -14.0F, 1);
        qa_float_image(g, placement, 22.0F, 22.0F, 34.0F, 38.0F, 2);
        qa_float_image(g, selected ? g->card_badge_count_32 : NT_ATLAS_INVALID_REGION, 22.0F, 22.0F, -34.0F, -42.0F, 2);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(88), CLAY_SIZING_FIXED(78)}}}) {}
        qa_dim(g, name);
    }
}

static void draw_ui_cards(game_ctx_t *g) {
    CLAY(QA_SECTION(520.0F, 248.0F)) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "HUD + cards at game scale", &s_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6}}) {
            qa_icon_chip(g, g->icon_supplies_32, "10");
            qa_icon_chip(g, g->icon_wisdom_32, "2");
            qa_icon_chip(g, g->icon_glory_32, "1");
            qa_icon_chip(g, g->icon_stamina_32, "9");
            qa_icon_chip(g, g->icon_speed_32, "x1");
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6}}) {
            qa_icon_chip(g, g->icon_body_32, "B");
            qa_icon_chip(g, g->icon_mind_32, "M");
            qa_icon_chip(g, g->icon_spirit_32, "S");
            qa_icon_chip(g, g->icon_circle_32, "1/10");
            qa_icon_chip(g, g->icon_day_32, "D1");
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6}}) {
            card_preview(g, g->ui_card_selected_96x128, g->card_art_saxaul_64, g->card_placement_roadside_32, "saxaul", true);
            card_preview(g, g->ui_card_playable_96x128, g->card_art_yurt_64, g->card_placement_field_32, "yurt", false);
            card_preview(g, g->ui_card_playable_96x128, g->card_art_tamga_stone_64, g->card_placement_special_32, "tamga", false);
            card_preview(g, g->ui_card_playable_96x128, g->card_art_wolf_track_64, g->card_placement_roadside_32, "trail", false);
        }
    }
}

static void fx_strip(game_ctx_t *g, const char *name, const uint32_t *frames, uint32_t count, uint32_t bg) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(38)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(88), CLAY_SIZING_FIXED(30)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) { qa_dim(g, name); }
        for (uint32_t i = 0; i < count; i++) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(32)}},
                  .backgroundColor = bg ? (Clay_Color){82.0F, 70.0F, 48.0F, 255.0F} : (Clay_Color){26.0F, 28.0F, 36.0F, 255.0F},
                  .cornerRadius = CLAY_CORNER_RADIUS(3.0F)}) {
                qa_float_image(g, frames[i], 32.0F, 32.0F, 0.0F, 0.0F, 0);
            }
        }
    }
}

static void draw_fx(game_ctx_t *g) {
    CLAY(QA_SECTION(520.0F, 220.0F)) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "First playable FX strips", &s_title);
        fx_strip(g, "dust/sand", g->fx_dust_step, 4, 1);
        fx_strip(g, "placed", g->fx_tile_placed, 4, 0);
        fx_strip(g, "trigger", g->fx_tile_trigger, 4, 0);
        fx_strip(g, "gain+text", g->fx_gain_popup, 3, 0);
        fx_strip(g, "invalid", g->fx_invalid_cell, 2, 0);
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30)},
                         .padding = {6, 6, 0, 0},
                         .layoutDirection = CLAY_LEFT_TO_RIGHT,
                         .childGap = 8,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {18.0F, 20.0F, 28.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(3.0F)}) {
            qa_image(g, g->fx_gain_popup[1], 24.0F, 24.0F, "miss");
            qa_label(g, "+1 supplies text stays readable");
        }
    }
}

static void draw_pass6_future_library(game_ctx_t *g) {
    CLAY(QA_SECTION(520.0F, 188.0F)) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "QA-only Pass 6 future library", &s_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(38)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(30)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) { qa_dim(g, "cards"); }
            qa_image(g, g->card_art_oasis_64, 34.0F, 34.0F, "miss");
            qa_image(g, g->card_art_mirage_64, 34.0F, 34.0F, "miss");
            qa_image(g, g->card_art_storm_64, 34.0F, 34.0F, "miss");
            qa_image(g, g->card_art_last_tamga_64, 34.0F, 34.0F, "miss");
            qa_image(g, g->card_art_well_64, 34.0F, 34.0F, "miss");
            qa_image(g, g->card_art_watchtower_64, 34.0F, 34.0F, "miss");
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(38)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(30)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) { qa_dim(g, "tiles"); }
            qa_image(g, g->tile_well_01, 34.0F, 34.0F, "miss");
            qa_image(g, g->tile_watchtower_01, 34.0F, 34.0F, "miss");
            qa_image(g, g->tile_pack_01, 34.0F, 34.0F, "miss");
            qa_image(g, g->tile_small_camp_01, 34.0F, 34.0F, "miss");
            qa_image(g, g->tile_clan_camp_01, 34.0F, 34.0F, "miss");
            qa_image(g, g->tile_hunting_trail_01, 34.0F, 34.0F, "miss");
            qa_image(g, g->tile_vision_01, 34.0F, 34.0F, "miss");
            qa_image(g, g->tile_false_path_01, 34.0F, 34.0F, "miss");
            qa_image(g, g->tile_buried_spring_01, 34.0F, 34.0F, "miss");
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(34)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(30)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) { qa_dim(g, "icons"); }
            qa_image(g, g->icon_aul_upgrade_32, 28.0F, 28.0F, "miss");
            qa_image(g, g->icon_card_gain_32, 28.0F, 28.0F, "miss");
            qa_image(g, g->icon_deck_32, 28.0F, 28.0F, "miss");
            qa_image(g, g->icon_map_32, 28.0F, 28.0F, "miss");
            qa_image(g, g->icon_memory_32, 28.0F, 28.0F, "miss");
            qa_image(g, g->icon_warning_32, 28.0F, 28.0F, "miss");
        }
        qa_dim(g, "Future asset proof only: not production tile/card unlocks");
    }
}

static void equipment_cell(game_ctx_t *g, uint32_t slot, uint32_t item, const char *name) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(72), CLAY_SIZING_FIXED(86)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 3, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(58), CLAY_SIZING_FIXED(58)}}, .backgroundColor = {38.0F, 43.0F, 58.0F, 255.0F}, .cornerRadius = CLAY_CORNER_RADIUS(5.0F)}) {
            qa_float_image(g, slot, 58.0F, 58.0F, 0.0F, 0.0F, 0);
            qa_float_image(g, item, 38.0F, 38.0F, 0.0F, 0.0F, 1);
        }
        qa_dim(g, name);
    }
}

static void draw_hero_panel(game_ctx_t *g) {
    CLAY(QA_SECTION(380.0F, 222.0F)) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "Hero panel + equipment", &s_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(112), CLAY_SIZING_FIXED(142)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = {54.0F, 60.0F, 80.0F, 255.0F},
                  .cornerRadius = CLAY_CORNER_RADIUS(6.0F)}) {
                qa_float_image(g, g->hero_wayfarer_panel, 92.0F, 126.0F, 0.0F, 0.0F, 0);
            }
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}}) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5}}) {
                    equipment_cell(g, g->equip_slot_weapon_01, g->equip_weapon_staff_01, "weapon");
                    equipment_cell(g, g->equip_slot_clothes_01, g->equip_clothes_cloak_01, "clothes");
                }
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5}}) {
                    equipment_cell(g, g->equip_slot_tamga_01, g->equip_tamga_charm_01, "tamga");
                    equipment_cell(g, g->equip_slot_tool_01, g->equip_tool_satchel_01, "tool");
                }
            }
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
            qa_image(g, g->hero_wayfarer_idle_s, 48.0F, 58.0F, "idle");
            qa_image(g, g->hero_wayfarer_walk_s, 48.0F, 58.0F, "walk_s");
            qa_image(g, g->hero_wayfarer_walk_e, 48.0F, 58.0F, "walk_e");
            qa_image(g, g->hero_wayfarer_walk_n, 48.0F, 58.0F, "walk_n");
            qa_image(g, g->hero_wayfarer_walk_w, 48.0F, 58.0F, "walk_w");
        }
    }
}

static void draw_hero_archetype_panels(game_ctx_t *g) {
    CLAY(QA_SECTION(380.0F, 126.0F)) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "QA-only hero archetype panels", &s_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
            CLAY({.layout = {
                      .sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(82)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                qa_image(g, g->hero_body_panel, 48.0F, 72.0F, "body");
                qa_dim(g, "body");
            }
            CLAY({.layout = {
                      .sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(82)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                qa_image(g, g->hero_mind_panel, 48.0F, 72.0F, "mind");
                qa_dim(g, "mind");
            }
            CLAY({.layout = {
                      .sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(82)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                qa_image(g, g->hero_spirit_panel, 48.0F, 72.0F, "spirit");
                qa_dim(g, "spirit");
            }
        }
    }
}

static void stage_cell(game_ctx_t *g, uint32_t region, const char *name) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(58), CLAY_SIZING_FIXED(78)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 3, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        qa_image(g, region, 56.0F, 56.0F, "miss");
        qa_dim(g, name);
    }
}

static void draw_aul_progression(game_ctx_t *g) {
    CLAY(QA_SECTION(380.0F, 160.0F)) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "QA-only Pass 5 aul progression", &s_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 4}}) {
            stage_cell(g, g->aul_stage_01_camp, "01");
            stage_cell(g, g->aul_stage_02_settlement, "02");
            stage_cell(g, g->aul_stage_03_village, "03");
            stage_cell(g, g->aul_stage_04_fortified_aul, "04");
            stage_cell(g, g->aul_stage_05_steppe_capital, "05");
            stage_cell(g, g->aul_tamga_post_01, "post");
        }
        qa_dim(g, "Debug proof only: not production upgrade mechanics");
    }
}

static void draw_surface_swatches(game_ctx_t *g) {
    CLAY(QA_SECTION(380.0F, 88.0F)) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "UI surface swatches", &s_title);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6}}) {
            qa_image(g, g->ui_panel_felt_dark_96, 54.0F, 42.0F, "dark");
            qa_image(g, g->ui_panel_felt_light_96, 54.0F, 42.0F, "light");
            qa_image(g, g->ui_slot_equipment_64, 42.0F, 42.0F, "slot");
            qa_image(g, g->ui_chip_resource_64, 54.0F, 42.0F, "chip");
            qa_image(g, g->ui_tooltip_dark_64, 54.0F, 42.0F, "tip");
            qa_image(g, g->ui_button_dark_64, 54.0F, 42.0F, "btn");
        }
    }
}

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6},
          .backgroundColor = {10.0F, 12.0F, 20.0F, 255.0F}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 12, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "VISUAL QA HARNESS - runtime renderer, not production gameplay/progression", &s_title);
            qa_dim(g, "--visual-qa");
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(350), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
                draw_gameplay_composition(g);
                draw_active_tiles(g);
            }
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(520), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
                draw_ui_cards(g);
                draw_fx(g);
                draw_pass6_future_library(g);
            }
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(380), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
                draw_hero_panel(g);
                draw_hero_archetype_panels(g);
                draw_aul_progression(g);
                draw_surface_swatches(g);
            }
        }
    }
}

const scene_t SCENE_VISUAL_QA = {
    .name = "visual_qa",
    .on_enter = NULL,
    .on_update = on_update,
    .on_exit = NULL,
    .fullscreen = true,
};
