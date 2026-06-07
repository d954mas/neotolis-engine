/* Main menu: title, START, language toggle. */

#include "clay.h"

#include "atlas/nt_atlas.h"
#include "input/nt_input.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_label.h"

#include "game.h"
#include "i18n.h"
#include "save.h"
#include "ui_kit.h"

static void cycle_language(void) {
    i18n_cycle();
    save_set_int("lang", (int)i18n_get());
    save_flush();
}

static bool has_region(uint32_t region) { return region != 0U && region != NT_ATLAS_INVALID_REGION; }

static void menu_image(game_ctx_t *g, uint32_t region, float w, float h) {
    if (!has_region(region)) {
        return;
    }
    const nt_ui_image_style_t img = nt_ui_image_style_defaults();
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}},
    };
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
}

static void menu_float_image(game_ctx_t *g, uint32_t region, float w, float h, float ox, float oy, int16_t z) {
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

static void menu_art_strip(game_ctx_t *g) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(420), CLAY_SIZING_FIXED(82)},
                     .padding = CLAY_PADDING_ALL(8),
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 14,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {42.0F, 34.0F, 25.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(8.0F),
          .border = {.color = {104.0F, 76.0F, 42.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(1)}}) {
        menu_image(g, g->hero_wayfarer_panel, 54.0F, 70.0F);
        menu_image(g, g->aul_fire_01, 58.0F, 58.0F);
        menu_image(g, g->tile_saxaul, 52.0F, 52.0F);
        menu_image(g, g->card_art_wolf_track_64, 52.0F, 52.0F);
        menu_image(g, g->icon_last_tamga_32, 38.0F, 38.0F);
    }
}

static void menu_world_panel(game_ctx_t *g) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(560), CLAY_SIZING_FIXED(520)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {166.0F, 132.0F, 80.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(10.0F),
          .border = {.color = {132.0F, 92.0F, 48.0F, 255.0F}, .width = CLAY_BORDER_OUTSIDE(2)}}) {
        menu_float_image(g, g->decor_dune, 92.0F, 92.0F, -210.0F, -155.0F, 1);
        menu_float_image(g, g->decor_tracks, 96.0F, 96.0F, 176.0F, 135.0F, 1);
        menu_float_image(g, g->decor_stones, 84.0F, 84.0F, 214.0F, -116.0F, 1);
        menu_float_image(g, g->road_straight_ew, 116.0F, 104.0F, -104.0F, 0.0F, 2);
        menu_float_image(g, g->road_straight_ew, 116.0F, 104.0F, 0.0F, 0.0F, 2);
        menu_float_image(g, g->road_corner_es, 116.0F, 104.0F, 104.0F, 0.0F, 2);
        menu_float_image(g, g->road_straight_ns, 116.0F, 104.0F, 104.0F, 104.0F, 2);
        menu_float_image(g, g->road_corner_wn, 116.0F, 104.0F, -104.0F, -104.0F, 2);
        menu_float_image(g, g->aul_yurt_small_01, 78.0F, 74.0F, -66.0F, -106.0F, 4);
        menu_float_image(g, g->aul_yurt_small_02, 78.0F, 74.0F, 2.0F, -94.0F, 4);
        menu_float_image(g, g->aul_fire_01, 64.0F, 64.0F, -10.0F, -132.0F, 5);
        menu_float_image(g, g->tile_saxaul, 84.0F, 84.0F, -208.0F, 104.0F, 4);
        menu_float_image(g, g->tile_wolf_track, 84.0F, 84.0F, 204.0F, 104.0F, 4);
        menu_float_image(g, g->tile_tamga_stone, 84.0F, 84.0F, 0.0F, 178.0F, 4);
        menu_float_image(g, g->hero_wayfarer_idle_s, 88.0F, 108.0F, -44.0F, 20.0F, 6);
    }
}

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;

    if (nt_input_key_is_pressed(NT_KEY_L)) {
        cycle_language();
    }

    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(54),
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 58,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {18.0F, 16.0F, 14.0F, 255.0F}}) {
        CLAY(
            {.layout = {.sizing = {CLAY_SIZING_FIXED(500), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 22, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_TITLE), &TJ_STYLE_TITLE);
            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_SUBTITLE), &TJ_STYLE_BODY);
            menu_art_strip(g);

            if (tj_button(g, "menu_start", i18n(T_START), 420, 88, TJ_BTN_PRIMARY)) {
                g->score = 0;
                game_goto(g, &SCENE_GAME);
            }

            if (tj_button(g, "menu_settings", i18n(T_SETTINGS), 320, 62, TJ_BTN_SECONDARY)) {
                g->settings_open = true;
            }

            if (tj_button(g, "menu_lang", i18n_lang_label(i18n_get()), 320, 62, TJ_BTN_SECONDARY)) {
                cycle_language();
            }

            nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_HINT), &TJ_STYLE_HINT);
        }
        menu_world_panel(g);
    }
}

const scene_t SCENE_MENU = {
    .name = "menu",
    .on_enter = NULL,
    .on_update = on_update,
    .on_exit = NULL,
    .fullscreen = true,
};
