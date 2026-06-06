/* Heir select: pick an archetype (VS/Diablo-style roster) before each run. Stats
 * differ (same point budget), each has a flat per-circle perk. Content in heirs.tsv. */

#include <stdio.h>

#include "clay.h"

#include "ui/nt_ui_label.h"

#include "config.h"
#include "game.h"
#include "ui_kit.h"

static const nt_ui_label_style_t s_name = {.font_id = 0, .font_size = 26, .color = {255.0F, 214.0F, 120.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_stat = {.font_id = 0, .font_size = 18, .color = {232.0F, 222.0F, 202.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_perk = {.font_id = 0, .font_size = 16, .color = {126.0F, 188.0F, 134.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};

static void perk_fmt(char *buf, size_t cap, tj_perk_t perk, int v) {
    const char *res = NULL;
    switch (perk) {
    case TJ_PERK_STAMINA_PER_CIRCLE:
        res = "Силы";
        break;
    case TJ_PERK_SUPPLIES_PER_CIRCLE:
        res = "Запасы";
        break;
    case TJ_PERK_WISDOM_PER_CIRCLE:
        res = "Мудрость";
        break;
    case TJ_PERK_GLORY_PER_CIRCLE:
        res = "Слава";
        break;
    default:
        break;
    }
    if (res) {
        (void)snprintf(buf, cap, "Перк: +%d %s за круг", v, res);
    } else {
        (void)snprintf(buf, cap, "Перк: нет");
    }
}

static void hero_card(game_ctx_t *g, int i) {
    const tj_heir_def_t *h = &g_config.heirs[i];
    static char stat[TJ_MAX_HEIRS][64];
    static char perk[TJ_MAX_HEIRS][56];
    static char id[TJ_MAX_HEIRS][16];
    (void)snprintf(stat[i], sizeof stat[i], "Тело %d   Ум %d   Дух %d", h->body, h->mind, h->spirit);
    perk_fmt(perk[i], sizeof perk[i], h->perk, h->perk_value);
    (void)snprintf(id[i], sizeof id[i], "heir%d", i);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(260), CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(18),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 12,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {42.0F, 34.0F, 25.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(14.0F)}) {
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), h->name, &s_name);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), stat[i], &s_stat);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), perk[i], &s_perk);
        if (tj_button(g, id[i], "Выбрать", 200, 54, TJ_BTN_PRIMARY)) {
            g->chosen_heir = i;
            game_goto(g, &SCENE_GAME);
        }
    }
}

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), "Выбери наследника", &TJ_STYLE_TITLE);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 18}}) {
        for (int i = 0; i < g_config.heir_count && i < TJ_MAX_HEIRS; i++) {
            hero_card(g, i);
        }
    }
}

const scene_t SCENE_HEIR_SELECT = {
    .name = "heir_select",
    .on_enter = NULL,
    .on_update = on_update,
    .on_exit = NULL,
};
