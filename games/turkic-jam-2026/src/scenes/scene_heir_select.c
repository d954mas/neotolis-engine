#include <stdio.h>
#include <string.h>

#include "clay.h"

#include "atlas/nt_atlas.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_label.h"

#include "config.h"
#include "game.h"
#include "i18n.h"
#include "ui_kit.h"

static const nt_ui_label_style_t s_name = {.font_id = 0, .font_size = 26, .color = {255.0F, 214.0F, 120.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_stat = {.font_id = 0, .font_size = 18, .color = {232.0F, 222.0F, 202.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};
static const nt_ui_label_style_t s_perk = {.font_id = 0, .font_size = 16, .color = {126.0F, 188.0F, 134.0F, 255.0F}, .align = CLAY_TEXT_ALIGN_CENTER};

static bool has_region(uint32_t region) { return region != 0U && region != NT_ATLAS_INVALID_REGION; }

static uint32_t heir_region(const game_ctx_t *g, int i) {
    if (i == 0 && has_region(g->hero_body_panel)) {
        return g->hero_body_panel;
    }
    if (i == 1 && has_region(g->hero_mind_panel)) {
        return g->hero_mind_panel;
    }
    if (i == 2 && has_region(g->hero_spirit_panel)) {
        return g->hero_spirit_panel;
    }
    return g->hero_wayfarer_panel;
}

static void heir_image(game_ctx_t *g, int i) {
    const uint32_t region = heir_region(g, i);
    if (!has_region(region)) {
        return;
    }
    const nt_ui_image_style_t img = nt_ui_image_style_defaults();
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(92), CLAY_SIZING_FIXED(132)}},
    };
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
}

static const char *stat_body(i18n_lang_t lang) {
    if (lang == LANG_TR) {
        return "Kilic";
    }
    return lang == LANG_RU ? "Клинок" : "Blade";
}

static const char *stat_mind(i18n_lang_t lang) {
    if (lang == LANG_TR) {
        return "At";
    }
    return lang == LANG_RU ? "Скакун" : "Steed";
}

static const char *stat_spirit(i18n_lang_t lang) {
    if (lang == LANG_TR) {
        return "Kut";
    }
    return lang == LANG_RU ? "Кут" : "Kut";
}

static const char *heir_name(const tj_heir_def_t *h) {
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

static void perk_fmt(char *buf, size_t cap, tj_perk_t perk, int v) {
    const i18n_lang_t lang = i18n_get();
    const char *res = NULL;
    switch (perk) {
    case TJ_PERK_STAMINA_PER_CIRCLE:
        res = (lang == LANG_TR) ? "Dayanıklılık" : (lang == LANG_RU ? "Силы" : "Stamina");
        break;
    case TJ_PERK_SUPPLIES_PER_CIRCLE:
        res = (lang == LANG_TR) ? "Erzak" : (lang == LANG_RU ? "Запасы" : "Supplies");
        break;
    case TJ_PERK_WISDOM_PER_CIRCLE:
        res = (lang == LANG_TR) ? "Bilgelik" : (lang == LANG_RU ? "Мудрость" : "Wisdom");
        break;
    case TJ_PERK_GLORY_PER_CIRCLE:
        res = (lang == LANG_TR) ? "Ün" : (lang == LANG_RU ? "Слава" : "Glory");
        break;
    default:
        break;
    }
    if (res) {
        if (lang == LANG_TR) {
            (void)snprintf(buf, cap, "Yetenek: döngü başına +%d %s", v, res);
        } else if (lang == LANG_RU) {
            (void)snprintf(buf, cap, "Перк: +%d %s за круг", v, res);
        } else {
            (void)snprintf(buf, cap, "Perk: +%d %s per circle", v, res);
        }
    } else if (lang == LANG_TR) {
        (void)snprintf(buf, cap, "Yetenek: yok");
    } else if (lang == LANG_RU) {
        (void)snprintf(buf, cap, "Перк: нет");
    } else {
        (void)snprintf(buf, cap, "Perk: none");
    }
}

static void stat_icon(game_ctx_t *g, uint32_t region) {
    if (!has_region(region)) {
        return;
    }
    const nt_ui_image_style_t img = nt_ui_image_style_defaults();
    const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24)}}};
    nt_ui_image(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_IMG), g->atlas, region, &img, &decl);
}

static void stat_chip(game_ctx_t *g, uint32_t icon, const char *text) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(28)},
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 4,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        stat_icon(g, icon);
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), text, &s_stat);
    }
}

static void hero_card(game_ctx_t *g, int i) {
    const tj_heir_def_t *h = &g_config.heirs[i];
    static char blade[TJ_MAX_HEIRS][24];
    static char steed[TJ_MAX_HEIRS][24];
    static char kut[TJ_MAX_HEIRS][24];
    static char perk[TJ_MAX_HEIRS][56];
    static char id[TJ_MAX_HEIRS][16];
    const i18n_lang_t lang = i18n_get();
    (void)snprintf(blade[i], sizeof blade[i], "%s %d", stat_body(lang), h->body);
    (void)snprintf(steed[i], sizeof steed[i], "%s %d", stat_mind(lang), h->mind);
    (void)snprintf(kut[i], sizeof kut[i], "%s %d", stat_spirit(lang), h->spirit);
    perk_fmt(perk[i], sizeof perk[i], h->perk, h->perk_value);
    (void)snprintf(id[i], sizeof id[i], "heir%d", i);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(270), CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 10,
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {42.0F, 34.0F, 25.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(14.0F)}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(124), CLAY_SIZING_FIXED(148)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {70.0F, 48.0F, 32.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(10.0F)}) {
            heir_image(g, i);
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), heir_name(h), &s_name);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            stat_chip(g, g->icon_body_32, blade[i]);
            stat_chip(g, g->icon_mind_32, steed[i]);
            stat_chip(g, g->icon_spirit_32, kut[i]);
        }
        nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), perk[i], &s_perk);
        if (tj_button(g, id[i], i18n(T_CHOOSE), 200, 54, TJ_BTN_PRIMARY)) {
            g->chosen_heir = i;
            game_goto(g, &SCENE_GAME);
        }
    }
}

static void on_update(game_ctx_t *g, float dt) {
    (void)dt;
    nt_ui_label(g->ui, NT_UI_DATA_LAYER(TJ_LAYER_TEXT), i18n(T_HEIR_TITLE), &TJ_STYLE_TITLE);
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
