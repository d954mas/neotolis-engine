/* Build turkic_jam pack:
 *   turkic_jam.ntpack -- atlas (UI base + optional sprites) + sprite/text shaders + font.
 * Usage: build_turkic_jam_packs <pack_dir>
 * Run from the project root directory. */

/* clang-format off */
#include "nt_builder.h"
/* clang-format on */

#include "stb_image.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define HEADER_DIR "games/turkic-jam-2026/generated"
/* Reuse the ui_theme_demo font (same precedent as ui_buttons_demo). */
#define FONT_PATH "examples/ui_theme_demo/raw/font.ttf"

/* Kenney button slice9 corner (px). */
#define BUTTON_BORDER 16

/* ASCII + Cyrillic + Turkish letters used by i18n (EN/RU/TR). Roboto covers all. */
#define TJ_CHARSET                                                                                                                                                                                     \
    NT_CHARSET_ASCII                                                                                                                                                                                   \
    "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"                                                                                                                                                                \
    "абвгдеёжзийклмнопрстуфхцчшщъыьэюя"                                                                                                                                                                \
    "çÇğĞıİöÖşŞüÜ"

static char s_path_buf[512];

typedef struct tj_atlas_asset {
    const char *category;
    const char *path;
    const char *name;
    float origin_x;
    float origin_y;
} tj_atlas_asset_t;

typedef struct tj_optional_category_report {
    const char *batch;
    const char *category;
    uint32_t found;
    uint32_t missing;
} tj_optional_category_report_t;

static uint32_t s_optional_found;
static uint32_t s_optional_missing;
static const char *s_optional_batch = "Batch A";
static tj_optional_category_report_t s_category_report[] = {
    {"Batch A", "ground", 0, 0},
    {"Batch A", "decor", 0, 0},
    {"Batch A", "road", 0, 0},
    {"Batch A", "aul", 0, 0},
    {"Batch A", "tiles", 0, 0},
    {"Batch A", "hero", 0, 0},
    {"Batch A", "ui", 0, 0},
    {"Batch B", "ui", 0, 0},
    {"Batch B", "cards", 0, 0},
    {"Batch B", "equipment", 0, 0},
    {"Batch B", "icons", 0, 0},
    {"Batch B", "fx", 0, 0},
    {"Batch C aul progression", "aul", 0, 0},
    {"Batch C hero archetype panels", "hero", 0, 0},
    {"Pass 6 future library", "cards", 0, 0},
    {"Pass 6 future library", "tiles", 0, 0},
    {"Pass 6 future library", "icons", 0, 0},
};

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    (void)fclose(f);
    return true;
}

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

static tj_optional_category_report_t *category_report_for(const char *batch, const char *category) {
    for (uint32_t i = 0; i < (uint32_t)(sizeof(s_category_report) / sizeof(s_category_report[0])); i++) {
        if (strcmp(s_category_report[i].batch, batch) == 0 && strcmp(s_category_report[i].category, category) == 0) {
            return &s_category_report[i];
        }
    }
    return NULL;
}

static uint16_t ui_slice9_border_for(const char *name) {
    if (strcmp(name, "ui_panel_felt_dark_96") == 0 || strcmp(name, "ui_panel_felt_light_96") == 0) {
        return 24;
    }
    if (strcmp(name, "ui_card_playable_96x128") == 0 || strcmp(name, "ui_card_selected_96x128") == 0) {
        return 18;
    }
    if (strcmp(name, "ui_slot_equipment_64") == 0) {
        return 14;
    }
    if (strcmp(name, "ui_chip_resource_64") == 0 || strcmp(name, "ui_tooltip_dark_64") == 0) {
        return 16;
    }
    if (strcmp(name, "ui_card_back_96x128") == 0) {
        return 18;
    }
    if (strcmp(name, "ui_button_dark_64") == 0) {
        return 16;
    }
    return 0;
}

static void record_optional_asset(const tj_atlas_asset_t *asset, bool found) {
    tj_optional_category_report_t *report = category_report_for(s_optional_batch, asset->category);
    if (report) {
        if (found) {
            report->found++;
        } else {
            report->missing++;
        }
    }
}

static bool optional_sprite_has_visible_pixel(const char *path) {
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels) {
        return false;
    }
    bool has_alpha = false;
    const int n = w * h;
    for (int i = 0; i < n; i++) {
        if (pixels[(i * 4) + 3] != 0U) {
            has_alpha = true;
            break;
        }
    }
    stbi_image_free(pixels);
    return has_alpha;
}

static void add_optional_sprite(NtBuilderContext *ctx, const tj_atlas_asset_t *asset) {
    if (!file_exists(asset->path)) {
        s_optional_missing++;
        record_optional_asset(asset, false);
        (void)printf("  MISSING [%-9s] %-28s %s\n", asset->category, asset->name, asset->path);
        return;
    }
    if (!optional_sprite_has_visible_pixel(asset->path)) {
        s_optional_missing++;
        record_optional_asset(asset, false);
        (void)printf("  INVALID [%-9s] %-28s %s (fully transparent or unreadable)\n", asset->category, asset->name, asset->path);
        return;
    }

    s_optional_found++;
    record_optional_asset(asset, true);
    nt_atlas_sprite_opts_t opts = nt_atlas_sprite_opts_defaults();
    opts.name = asset->name;
    opts.origin_x = asset->origin_x;
    opts.origin_y = asset->origin_y;
    opts.shape = NT_ATLAS_SPRITE_SHAPE_RECT;
    opts.allow_rotate = NT_ATLAS_SPRITE_ROTATE_NO;
    const uint16_t ui_border = ui_slice9_border_for(asset->name);
    if (ui_border > 0) {
        opts.slice9_left = ui_border;
        opts.slice9_right = ui_border;
        opts.slice9_top = ui_border;
        opts.slice9_bottom = ui_border;
    }
    nt_builder_atlas_add(ctx, asset->path, &opts);
    (void)printf("  FOUND   [%-9s] %s\n", asset->category, asset->name);
}

static void add_optional_batch(NtBuilderContext *ctx, const char *batch, const tj_atlas_asset_t *assets, uint32_t count) {
    const uint32_t found_before = s_optional_found;
    const uint32_t missing_before = s_optional_missing;
    s_optional_batch = batch;
    (void)printf("  Optional production sprites: %s\n", batch);
    for (uint32_t i = 0; i < count; i++) {
        add_optional_sprite(ctx, &assets[i]);
    }
    (void)printf("  Optional %s sprites by category:\n", batch);
    for (uint32_t i = 0; i < (uint32_t)(sizeof(s_category_report) / sizeof(s_category_report[0])); i++) {
        const tj_optional_category_report_t *report = &s_category_report[i];
        if (strcmp(report->batch, batch) != 0 || (report->found == 0 && report->missing == 0)) {
            continue;
        }
        (void)printf("    %-9s found %u / missing %u\n", report->category, report->found, report->missing);
    }
    (void)printf("  Optional %s sprites: found %u / missing %u\n", batch, s_optional_found - found_before, s_optional_missing - missing_before);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_turkic_jam_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build Turkic Jam Pack -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(HEADER_DIR);
    MKDIR("games/turkic-jam-2026/raw/ground");
    MKDIR("games/turkic-jam-2026/raw/decor");
    MKDIR("games/turkic-jam-2026/raw/road");
    MKDIR("games/turkic-jam-2026/raw/tiles");
    MKDIR("games/turkic-jam-2026/raw/aul");
    MKDIR("games/turkic-jam-2026/raw/hero");
    MKDIR("games/turkic-jam-2026/raw/ui");
    MKDIR("games/turkic-jam-2026/raw/cards");
    MKDIR("games/turkic-jam-2026/raw/equipment");
    MKDIR("games/turkic-jam-2026/raw/icons");
    MKDIR("games/turkic-jam-2026/raw/fx");

    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);
    MKDIR(cache_dir);

    // #region pack: turkic_jam.ntpack
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "turkic_jam.ntpack"));
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start turkic_jam.ntpack\n");
        return 1;
    }
    nt_builder_set_header_dir(ctx, HEADER_DIR);
    nt_builder_set_cache_dir(ctx, cache_dir);
    nt_builder_set_threads_auto(ctx);
    // #endregion

    // #region shaders (sprite for UI rects + Slug for text)
    nt_builder_add_shader(ctx, "assets/shaders/sprite.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/sprite.frag", NT_BUILD_SHADER_FRAGMENT);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.frag", NT_BUILD_SHADER_FRAGMENT);
    (void)printf("  Shaders added: 4 (sprite + slug_text)\n");
    // #endregion

    // #region atlas: UI base art + optional production sprites
    nt_atlas_opts_t atlas_opts = nt_atlas_opts_defaults();
    atlas_opts.shape = NT_ATLAS_SHAPE_RECT;
    atlas_opts.allow_transform = false;
    atlas_opts.pixels_per_unit = 1.0F;
    atlas_opts.padding = 2;
    atlas_opts.margin = 2;
    atlas_opts.extrude = 1;
    atlas_opts.premultiplied = true;
    atlas_opts.compress = NULL;
    atlas_opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    atlas_opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    atlas_opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    atlas_opts.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    atlas_opts.gen_mipmaps = false;

    nt_builder_begin_atlas(ctx, "turkic_jam_atlas", &atlas_opts);

    static const uint8_t white_pixel[4] = {255, 255, 255, 255};
    nt_atlas_sprite_opts_t white_opts = nt_atlas_sprite_opts_defaults();
    white_opts.name = "_white";
    nt_builder_atlas_add_raw(ctx, white_pixel, 1, 1, &white_opts);
    (void)printf("  Atlas region '_white': 1x1\n");

    /* Kenney CC0 buttons (384x128, 16px corners), reused as slice9 bg art. */
    nt_atlas_sprite_opts_t btn_opts = nt_atlas_sprite_opts_defaults();
    btn_opts.slice9_left = BUTTON_BORDER;
    btn_opts.slice9_right = BUTTON_BORDER;
    btn_opts.slice9_top = BUTTON_BORDER;
    btn_opts.slice9_bottom = BUTTON_BORDER;
    btn_opts.name = "button_blue";
    nt_builder_atlas_add(ctx, "games/turkic-jam-2026/raw/ui/button_blue_depth.png", &btn_opts);
    btn_opts.name = "button_green";
    nt_builder_atlas_add(ctx, "games/turkic-jam-2026/raw/ui/button_green_depth.png", &btn_opts);
    btn_opts.name = "button_red";
    nt_builder_atlas_add(ctx, "games/turkic-jam-2026/raw/ui/button_red_depth.png", &btn_opts);
    (void)printf("  Atlas: 3 Kenney buttons (s9:%d)\n", BUTTON_BORDER);

    const tj_atlas_asset_t world_assets[] = {
        {"ground", "games/turkic-jam-2026/raw/ground/ground_sand_base_01.png", "ground_sand_base_01", 0.5F, 0.5F},
        {"decor", "games/turkic-jam-2026/raw/decor/decor_dune_01.png", "decor_dune_01", 0.5F, 0.5F},
        {"decor", "games/turkic-jam-2026/raw/decor/decor_stones_01.png", "decor_stones_01", 0.5F, 0.5F},
        {"decor", "games/turkic-jam-2026/raw/decor/decor_dry_grass_01.png", "decor_dry_grass_01", 0.5F, 0.5F},
        {"decor", "games/turkic-jam-2026/raw/decor/decor_tracks_01.png", "decor_tracks_01", 0.5F, 0.5F},
        {"decor", "games/turkic-jam-2026/raw/decor/decor_bones_01.png", "decor_bones_01", 0.5F, 0.5F},
        {"decor", "games/turkic-jam-2026/raw/decor/decor_cracks_01.png", "decor_cracks_01", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/road_straight_ns.png", "road_straight_ns", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/road_straight_ew.png", "road_straight_ew", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/road_corner_ne.png", "road_corner_ne", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/road_corner_es.png", "road_corner_es", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/road_corner_sw.png", "road_corner_sw", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/road_corner_wn.png", "road_corner_wn", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/road_entry_aul.png", "road_entry_aul", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/road_current_highlight.png", "road_current_highlight", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/buffer_edge_stones_01.png", "buffer_edge_stones_01", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/buffer_packed_sand_01.png", "buffer_packed_sand_01", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/buffer_stakes_01.png", "buffer_stakes_01", 0.5F, 0.5F},
        {"road", "games/turkic-jam-2026/raw/road/buffer_cart_marks_01.png", "buffer_cart_marks_01", 0.5F, 0.5F},
        {"aul", "games/turkic-jam-2026/raw/aul/aul_ground_2x2.png", "aul_ground_2x2", 0.5F, 0.5F},
        {"aul", "games/turkic-jam-2026/raw/aul/aul_yurt_small_01.png", "aul_yurt_small_01", 0.5F, 0.5F},
        {"aul", "games/turkic-jam-2026/raw/aul/aul_yurt_small_02.png", "aul_yurt_small_02", 0.5F, 0.5F},
        {"aul", "games/turkic-jam-2026/raw/aul/aul_fire_01.png", "aul_fire_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_saxaul_01.png", "tile_saxaul_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_yurt_01.png", "tile_yurt_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_tamga_stone_01.png", "tile_tamga_stone_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_wolf_track_01.png", "tile_wolf_track_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_oasis_01.png", "tile_oasis_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_mirage_01.png", "tile_mirage_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_storm_01.png", "tile_storm_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_last_tamga_01.png", "tile_last_tamga_01", 0.5F, 0.5F},
        {"hero", "games/turkic-jam-2026/raw/hero/hero_wayfarer_idle_s.png", "hero_wayfarer_idle_s", 0.5F, 1.0F},
        {"hero", "games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_s.png", "hero_wayfarer_walk_s", 0.5F, 1.0F},
        {"hero", "games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_e.png", "hero_wayfarer_walk_e", 0.5F, 1.0F},
        {"hero", "games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_n.png", "hero_wayfarer_walk_n", 0.5F, 1.0F},
        {"hero", "games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_w.png", "hero_wayfarer_walk_w", 0.5F, 1.0F},
        {"hero", "games/turkic-jam-2026/raw/hero/hero_wayfarer_panel.png", "hero_wayfarer_panel", 0.5F, 0.5F},
        {"ui", "games/turkic-jam-2026/raw/ui/ui_panel_felt_dark_96.png", "ui_panel_felt_dark_96", 0.5F, 0.5F},
        {"ui", "games/turkic-jam-2026/raw/ui/ui_panel_felt_light_96.png", "ui_panel_felt_light_96", 0.5F, 0.5F},
        {"ui", "games/turkic-jam-2026/raw/ui/ui_card_playable_96x128.png", "ui_card_playable_96x128", 0.5F, 0.5F},
        {"ui", "games/turkic-jam-2026/raw/ui/ui_card_selected_96x128.png", "ui_card_selected_96x128", 0.5F, 0.5F},
        {"ui", "games/turkic-jam-2026/raw/ui/ui_slot_equipment_64.png", "ui_slot_equipment_64", 0.5F, 0.5F},
        {"ui", "games/turkic-jam-2026/raw/ui/ui_chip_resource_64.png", "ui_chip_resource_64", 0.5F, 0.5F},
        {"ui", "games/turkic-jam-2026/raw/ui/ui_tooltip_dark_64.png", "ui_tooltip_dark_64", 0.5F, 0.5F},
    };
    const tj_atlas_asset_t ui_assets[] = {
        {"ui", "games/turkic-jam-2026/raw/ui/ui_card_back_96x128.png", "ui_card_back_96x128", 0.5F, 0.5F},
        {"ui", "games/turkic-jam-2026/raw/ui/ui_button_dark_64.png", "ui_button_dark_64", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_badge_count_32.png", "card_badge_count_32", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_placement_roadside_32.png", "card_placement_roadside_32", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_placement_field_32.png", "card_placement_field_32", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_placement_special_32.png", "card_placement_special_32", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_saxaul_64.png", "card_art_saxaul_64", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_yurt_64.png", "card_art_yurt_64", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_tamga_stone_64.png", "card_art_tamga_stone_64", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_wolf_track_64.png", "card_art_wolf_track_64", 0.5F, 0.5F},
        {"equipment", "games/turkic-jam-2026/raw/equipment/equip_slot_weapon_01.png", "equip_slot_weapon_01", 0.5F, 0.5F},
        {"equipment", "games/turkic-jam-2026/raw/equipment/equip_slot_clothes_01.png", "equip_slot_clothes_01", 0.5F, 0.5F},
        {"equipment", "games/turkic-jam-2026/raw/equipment/equip_slot_tamga_01.png", "equip_slot_tamga_01", 0.5F, 0.5F},
        {"equipment", "games/turkic-jam-2026/raw/equipment/equip_slot_tool_01.png", "equip_slot_tool_01", 0.5F, 0.5F},
        {"equipment", "games/turkic-jam-2026/raw/equipment/equip_weapon_staff_01.png", "equip_weapon_staff_01", 0.5F, 0.5F},
        {"equipment", "games/turkic-jam-2026/raw/equipment/equip_clothes_cloak_01.png", "equip_clothes_cloak_01", 0.5F, 0.5F},
        {"equipment", "games/turkic-jam-2026/raw/equipment/equip_tamga_charm_01.png", "equip_tamga_charm_01", 0.5F, 0.5F},
        {"equipment", "games/turkic-jam-2026/raw/equipment/equip_tool_satchel_01.png", "equip_tool_satchel_01", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_stamina_32.png", "icon_stamina_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_supplies_32.png", "icon_supplies_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_wisdom_32.png", "icon_wisdom_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_glory_32.png", "icon_glory_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_circle_32.png", "icon_circle_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_day_32.png", "icon_day_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_body_32.png", "icon_body_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_mind_32.png", "icon_mind_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_spirit_32.png", "icon_spirit_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_last_tamga_32.png", "icon_last_tamga_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_settings_32.png", "icon_settings_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_speed_32.png", "icon_speed_32", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_dust_step_00.png", "fx_dust_step_00", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_dust_step_01.png", "fx_dust_step_01", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_dust_step_02.png", "fx_dust_step_02", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_dust_step_03.png", "fx_dust_step_03", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_tile_placed_00.png", "fx_tile_placed_00", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_tile_placed_01.png", "fx_tile_placed_01", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_tile_placed_02.png", "fx_tile_placed_02", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_tile_placed_03.png", "fx_tile_placed_03", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_tile_trigger_00.png", "fx_tile_trigger_00", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_tile_trigger_01.png", "fx_tile_trigger_01", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_tile_trigger_02.png", "fx_tile_trigger_02", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_tile_trigger_03.png", "fx_tile_trigger_03", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_gain_popup_00.png", "fx_gain_popup_00", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_gain_popup_01.png", "fx_gain_popup_01", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_gain_popup_02.png", "fx_gain_popup_02", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_invalid_cell_00.png", "fx_invalid_cell_00", 0.5F, 0.5F},
        {"fx", "games/turkic-jam-2026/raw/fx/fx_invalid_cell_01.png", "fx_invalid_cell_01", 0.5F, 0.5F},
    };
    const tj_atlas_asset_t aul_progression_assets[] = {
        {"aul", "games/turkic-jam-2026/raw/aul/aul_tamga_post_01.png", "aul_tamga_post_01", 0.5F, 0.5F},
        {"aul", "games/turkic-jam-2026/raw/aul/aul_stage_01_camp.png", "aul_stage_01_camp", 0.5F, 0.5F},
        {"aul", "games/turkic-jam-2026/raw/aul/aul_stage_02_settlement.png", "aul_stage_02_settlement", 0.5F, 0.5F},
        {"aul", "games/turkic-jam-2026/raw/aul/aul_stage_03_village.png", "aul_stage_03_village", 0.5F, 0.5F},
        {"aul", "games/turkic-jam-2026/raw/aul/aul_stage_04_fortified_aul.png", "aul_stage_04_fortified_aul", 0.5F, 0.5F},
        {"aul", "games/turkic-jam-2026/raw/aul/aul_stage_05_steppe_capital.png", "aul_stage_05_steppe_capital", 0.5F, 0.5F},
    };
    const tj_atlas_asset_t hero_archetype_panel_assets[] = {
        {"hero", "games/turkic-jam-2026/raw/hero/hero_body_panel.png", "hero_body_panel", 0.5F, 0.5F},
        {"hero", "games/turkic-jam-2026/raw/hero/hero_mind_panel.png", "hero_mind_panel", 0.5F, 0.5F},
        {"hero", "games/turkic-jam-2026/raw/hero/hero_spirit_panel.png", "hero_spirit_panel", 0.5F, 0.5F},
    };
    const tj_atlas_asset_t future_library_assets[] = {
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_oasis_64.png", "card_art_oasis_64", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_mirage_64.png", "card_art_mirage_64", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_storm_64.png", "card_art_storm_64", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_last_tamga_64.png", "card_art_last_tamga_64", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_well_64.png", "card_art_well_64", 0.5F, 0.5F},
        {"cards", "games/turkic-jam-2026/raw/cards/card_art_watchtower_64.png", "card_art_watchtower_64", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_well_01.png", "tile_well_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_watchtower_01.png", "tile_watchtower_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_pack_01.png", "tile_pack_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_small_camp_01.png", "tile_small_camp_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_clan_camp_01.png", "tile_clan_camp_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_hunting_trail_01.png", "tile_hunting_trail_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_vision_01.png", "tile_vision_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_false_path_01.png", "tile_false_path_01", 0.5F, 0.5F},
        {"tiles", "games/turkic-jam-2026/raw/tiles/tile_buried_spring_01.png", "tile_buried_spring_01", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_aul_upgrade_32.png", "icon_aul_upgrade_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_card_gain_32.png", "icon_card_gain_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_deck_32.png", "icon_deck_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_map_32.png", "icon_map_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_memory_32.png", "icon_memory_32", 0.5F, 0.5F},
        {"icons", "games/turkic-jam-2026/raw/icons/icon_warning_32.png", "icon_warning_32", 0.5F, 0.5F},
    };
    add_optional_batch(ctx, "Batch A", world_assets, (uint32_t)(sizeof(world_assets) / sizeof(world_assets[0])));
    add_optional_batch(ctx, "Batch B", ui_assets, (uint32_t)(sizeof(ui_assets) / sizeof(ui_assets[0])));
    add_optional_batch(ctx, "Batch C aul progression", aul_progression_assets, (uint32_t)(sizeof(aul_progression_assets) / sizeof(aul_progression_assets[0])));
    add_optional_batch(ctx, "Batch C hero archetype panels", hero_archetype_panel_assets, (uint32_t)(sizeof(hero_archetype_panel_assets) / sizeof(hero_archetype_panel_assets[0])));
    add_optional_batch(ctx, "Pass 6 future library", future_library_assets, (uint32_t)(sizeof(future_library_assets) / sizeof(future_library_assets[0])));
    (void)printf("  Optional production sprites: found %u / missing %u\n", s_optional_found, s_optional_missing);

    nt_builder_end_atlas(ctx);
    // #endregion

    // #region font: ASCII Latin only (reuse ui_theme_demo font)
    nt_builder_add_font(ctx, FONT_PATH,
                        &(nt_font_opts_t){
                            .charset = TJ_CHARSET,
                            .resource_name = "turkic_jam/font",
                        });
    (void)printf("  Font (ASCII+Cyrillic+Turkish) added: turkic_jam/font\n");
    // #endregion

    // #region finish + codegen
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "turkic_jam.ntpack failed: %d\n", r);
        return 1;
    }

    char base_hdr[512];
    (void)snprintf(base_hdr, sizeof(base_hdr), "%s/turkic_jam.h", HEADER_DIR);
    const char *headers[] = {base_hdr};
    char combined[512];
    (void)snprintf(combined, sizeof(combined), "%s/turkic_jam_assets.h", HEADER_DIR);
    nt_builder_merge_headers(headers, 1, combined);
    (void)printf("Generated: %s\n", combined);
    // #endregion

    (void)printf("\n=== Done ===\n");
    return 0;
}
