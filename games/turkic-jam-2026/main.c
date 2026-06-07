/* Turkic Jam 2026 — game base template.
 * Engine bootstrap + render services + a scene loop. Scenes live in src/scenes
 * and only ever build UI; all engine init stays here. */

// #region includes
#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "audio/nt_audio.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "devapi/nt_devapi.h"
#include "font/nt_font.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_scale.h"
#include "window/nt_window.h"

#include "nt_pack_format.h"
#include "turkic_jam_assets.h"

#include "audio_assets.h"
#include "aul.h"
#include "config.h"
#include "game.h"
#include "i18n.h"
#include "rng.h"
#include "save.h"
#include "view.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#else
#include "stb_image_write.h"
#endif

#include "clay.h"
// #endregion

#define TJ_UI_ARENA_SIZE ((size_t)2U * 1024U * 1024U)
#define TJ_SCRATCH_ARENA_SIZE ((size_t)256U * 1024U)
#define TJ_ARRAY_COUNT(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

static NT_UI_DECLARE_ARENA(s_ui_arena, TJ_UI_ARENA_SIZE);

static game_ctx_t g;

static nt_buffer_t s_frame_ubo;
static nt_resource_t s_atlas_handle;
static nt_resource_t s_atlas_tex_handle;
static nt_resource_t s_sprite_vs;
static nt_resource_t s_sprite_fs;
static nt_resource_t s_text_vs;
static nt_resource_t s_text_fs;
static nt_resource_t s_font_resource;
static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static bool s_atlas_bound;
static bool s_font_bound;
#ifndef NT_PLATFORM_WEB
static const char *s_dump_frame_path;
static uint32_t s_dump_frame_after;
static bool s_exit_after_frame;
static bool s_dump_frame_done;
#endif

void game_goto(game_ctx_t *gg, const scene_t *next) { gg->next = next; }

#if NT_DEVAPI_ENABLED
/* Game-registered devapi endpoint: confirms the loaded balance config. */
static int ep_game_config(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    return snprintf(o, (size_t)cap, "{\"path_cells\":%d,\"laps_to_win\":%d,\"start_stamina\":%d,\"tiles\":%d,\"heirs\":%d}", g_config.path_cells, g_config.laps_to_win, g_config.start_stamina,
                    g_config.tile_count, g_config.heir_count);
}
#endif

static void apply_transition(void) {
    if (!g.next) {
        return;
    }
    if (g.scene && g.scene->on_exit) {
        g.scene->on_exit(&g);
    }
    g.prev = g.scene;
    g.scene = g.next;
    g.next = NULL;
    if (g.scene->on_enter) {
        g.scene->on_enter(&g);
    }
}

static void bind_optional_world_regions(void);

static void bind_atlas(void) {
    g.atlas = s_atlas_handle;
    g.white_region = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS__WHITE.value);
    NT_ASSERT(g.white_region != NT_ATLAS_INVALID_REGION);
    bind_optional_world_regions();
    nt_ui_set_atlas_white_region(g.ui, s_atlas_handle, g.white_region);
    s_atlas_bound = true;
    nt_log_info("turkic_jam: atlas bound");
}

static void bind_font(void) {
    nt_font_add(g.font, s_font_resource);
    nt_ui_set_font(g.ui, 0U, g.font);
    s_font_bound = true;
    nt_log_info("turkic_jam: font bound");
}

static uint32_t find_atlas_region(const char *name) {
#define TJ_FIND_REGION(n, id)                                                                                                                                                                          \
    if (strcmp(name, n) == 0) {                                                                                                                                                                        \
        return nt_atlas_find_region(s_atlas_handle, id.value);                                                                                                                                         \
    }
    TJ_FIND_REGION("ground_sand_base_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_GROUND_SAND_BASE_01);
    TJ_FIND_REGION("decor_dune_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DECOR_DUNE_01);
    TJ_FIND_REGION("decor_stones_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DECOR_STONES_01);
    TJ_FIND_REGION("decor_dry_grass_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DECOR_DRY_GRASS_01);
    TJ_FIND_REGION("decor_tracks_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DECOR_TRACKS_01);
    TJ_FIND_REGION("decor_bones_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DECOR_BONES_01);
    TJ_FIND_REGION("decor_cracks_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DECOR_CRACKS_01);
    TJ_FIND_REGION("road_straight_ns", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ROAD_STRAIGHT_NS);
    TJ_FIND_REGION("road_straight_ew", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ROAD_STRAIGHT_EW);
    TJ_FIND_REGION("road_corner_ne", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ROAD_CORNER_NE);
    TJ_FIND_REGION("road_corner_es", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ROAD_CORNER_ES);
    TJ_FIND_REGION("road_corner_sw", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ROAD_CORNER_SW);
    TJ_FIND_REGION("road_corner_wn", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ROAD_CORNER_WN);
    TJ_FIND_REGION("road_entry_aul", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ROAD_ENTRY_AUL);
    TJ_FIND_REGION("road_current_highlight", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ROAD_CURRENT_HIGHLIGHT);
    TJ_FIND_REGION("buffer_edge_stones_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BUFFER_EDGE_STONES_01);
    TJ_FIND_REGION("buffer_packed_sand_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BUFFER_PACKED_SAND_01);
    TJ_FIND_REGION("buffer_stakes_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BUFFER_STAKES_01);
    TJ_FIND_REGION("buffer_cart_marks_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BUFFER_CART_MARKS_01);
    TJ_FIND_REGION("aul_ground_2x2", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_GROUND_2X2);
    TJ_FIND_REGION("aul_yurt_small_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_YURT_SMALL_01);
    TJ_FIND_REGION("aul_yurt_small_02", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_YURT_SMALL_02);
    TJ_FIND_REGION("aul_fire_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_FIRE_01);
    TJ_FIND_REGION("aul_tamga_post_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_TAMGA_POST_01);
    TJ_FIND_REGION("aul_stage_01_camp", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_01_CAMP);
    TJ_FIND_REGION("aul_stage_02_settlement", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_02_SETTLEMENT);
    TJ_FIND_REGION("aul_stage_03_village", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_03_VILLAGE);
    TJ_FIND_REGION("aul_stage_04_fortified_aul", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_04_FORTIFIED_AUL);
    TJ_FIND_REGION("aul_stage_05_steppe_capital", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_05_STEPPE_CAPITAL);
    TJ_FIND_REGION("tile_saxaul_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_SAXAUL_01);
    TJ_FIND_REGION("tile_oasis_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_OASIS_01);
    TJ_FIND_REGION("tile_yurt_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_YURT_01);
    TJ_FIND_REGION("tile_tamga_stone_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_TAMGA_STONE_01);
    TJ_FIND_REGION("tile_wolf_track_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_WOLF_TRACK_01);
    TJ_FIND_REGION("tile_mirage_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_MIRAGE_01);
    TJ_FIND_REGION("tile_storm_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_STORM_01);
    TJ_FIND_REGION("tile_last_tamga_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_LAST_TAMGA_01);
    TJ_FIND_REGION("tile_war_1", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_WAR_1);
    TJ_FIND_REGION("tile_war_2", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_WAR_2);
    TJ_FIND_REGION("tile_war_3", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_WAR_3);
    TJ_FIND_REGION("tile_horse_1", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_HORSE_1);
    TJ_FIND_REGION("tile_horse_2", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_HORSE_2);
    TJ_FIND_REGION("tile_horse_3", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_HORSE_3);
    TJ_FIND_REGION("tile_steppe_1", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_STEPPE_1);
    TJ_FIND_REGION("tile_steppe_2", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_STEPPE_2);
    TJ_FIND_REGION("tile_steppe_3", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_STEPPE_3);
    TJ_FIND_REGION("tile_home_1", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_HOME_1);
    TJ_FIND_REGION("tile_home_2", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_HOME_2);
    TJ_FIND_REGION("tile_home_3", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_HOME_3);
    TJ_FIND_REGION("tile_water_1", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_WATER_1);
    TJ_FIND_REGION("tile_water_2", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_WATER_2);
    TJ_FIND_REGION("tile_water_3", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_WATER_3);
    TJ_FIND_REGION("boss_fat", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BOSS_FAT);
    TJ_FIND_REGION("boss_swift", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BOSS_SWIFT);
    TJ_FIND_REGION("boss_fierce", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BOSS_FIERCE);
    TJ_FIND_REGION("boss_ring_keeper", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BOSS_RING_KEEPER);
    TJ_FIND_REGION("tile_well_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_WELL_01);
    TJ_FIND_REGION("tile_watchtower_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_WATCHTOWER_01);
    TJ_FIND_REGION("tile_pack_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_PACK_01);
    TJ_FIND_REGION("tile_small_camp_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_SMALL_CAMP_01);
    TJ_FIND_REGION("tile_clan_camp_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_CLAN_CAMP_01);
    TJ_FIND_REGION("tile_hunting_trail_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_HUNTING_TRAIL_01);
    TJ_FIND_REGION("tile_vision_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_VISION_01);
    TJ_FIND_REGION("tile_false_path_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_FALSE_PATH_01);
    TJ_FIND_REGION("tile_buried_spring_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_TILE_BURIED_SPRING_01);
    TJ_FIND_REGION("hero_wayfarer_idle_s", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_HERO_WAYFARER_IDLE_S);
    TJ_FIND_REGION("hero_wayfarer_walk_s", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_HERO_WAYFARER_WALK_S);
    TJ_FIND_REGION("hero_wayfarer_walk_e", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_HERO_WAYFARER_WALK_E);
    TJ_FIND_REGION("hero_wayfarer_walk_n", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_HERO_WAYFARER_WALK_N);
    TJ_FIND_REGION("hero_wayfarer_walk_w", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_HERO_WAYFARER_WALK_W);
    TJ_FIND_REGION("hero_wayfarer_panel", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_HERO_WAYFARER_PANEL);
    TJ_FIND_REGION("hero_body_panel", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_HERO_BODY_PANEL);
    TJ_FIND_REGION("hero_mind_panel", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_HERO_MIND_PANEL);
    TJ_FIND_REGION("hero_spirit_panel", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_HERO_SPIRIT_PANEL);
    TJ_FIND_REGION("ui_panel_felt_dark_96", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_PANEL_FELT_DARK_96);
    TJ_FIND_REGION("ui_panel_felt_light_96", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_PANEL_FELT_LIGHT_96);
    TJ_FIND_REGION("ui_card_playable_96x128", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_CARD_PLAYABLE_96X128);
    TJ_FIND_REGION("ui_card_selected_96x128", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_CARD_SELECTED_96X128);
    TJ_FIND_REGION("ui_slot_equipment_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_SLOT_EQUIPMENT_64);
    TJ_FIND_REGION("ui_chip_resource_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_CHIP_RESOURCE_64);
    TJ_FIND_REGION("ui_tooltip_dark_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_TOOLTIP_DARK_64);
    TJ_FIND_REGION("die_d4", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DIE_D4);
    TJ_FIND_REGION("die_d6", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DIE_D6);
    TJ_FIND_REGION("die_d8", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DIE_D8);
    TJ_FIND_REGION("die_d10", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DIE_D10);
    TJ_FIND_REGION("die_d12", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DIE_D12);
    TJ_FIND_REGION("die_d20", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_DIE_D20);
    TJ_FIND_REGION("pouch_closed", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_POUCH_CLOSED);
    TJ_FIND_REGION("pouch_open_0", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_POUCH_OPEN_0);
    TJ_FIND_REGION("pouch_open_1", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_POUCH_OPEN_1);
    TJ_FIND_REGION("pouch_open_2", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_POUCH_OPEN_2);
    TJ_FIND_REGION("pouch_open_3", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_POUCH_OPEN_3);
    TJ_FIND_REGION("ui_card_back_96x128", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_CARD_BACK_96X128);
    TJ_FIND_REGION("ui_button_dark_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_BUTTON_DARK_64);
    TJ_FIND_REGION("ui_valid_cell_overlay_128", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_VALID_CELL_OVERLAY_128);
    TJ_FIND_REGION("ui_invalid_cell_overlay_128", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_INVALID_CELL_OVERLAY_128);
    TJ_FIND_REGION("ui_hover_cell_overlay_128", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_UI_HOVER_CELL_OVERLAY_128);
    TJ_FIND_REGION("card_badge_count_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_BADGE_COUNT_32);
    TJ_FIND_REGION("card_placement_roadside_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_PLACEMENT_ROADSIDE_32);
    TJ_FIND_REGION("card_placement_field_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_PLACEMENT_FIELD_32);
    TJ_FIND_REGION("card_placement_special_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_PLACEMENT_SPECIAL_32);
    TJ_FIND_REGION("card_art_saxaul_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_SAXAUL_64);
    TJ_FIND_REGION("card_art_yurt_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_YURT_64);
    TJ_FIND_REGION("card_art_tamga_stone_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_TAMGA_STONE_64);
    TJ_FIND_REGION("card_art_wolf_track_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_WOLF_TRACK_64);
    TJ_FIND_REGION("card_art_oasis_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_OASIS_64);
    TJ_FIND_REGION("card_art_mirage_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_MIRAGE_64);
    TJ_FIND_REGION("card_art_storm_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_STORM_64);
    TJ_FIND_REGION("card_art_last_tamga_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_LAST_TAMGA_64);
    TJ_FIND_REGION("card_art_well_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_WELL_64);
    TJ_FIND_REGION("card_art_watchtower_64", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_CARD_ART_WATCHTOWER_64);
    TJ_FIND_REGION("equip_slot_weapon_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_EQUIP_SLOT_WEAPON_01);
    TJ_FIND_REGION("equip_slot_clothes_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_EQUIP_SLOT_CLOTHES_01);
    TJ_FIND_REGION("equip_slot_tamga_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_EQUIP_SLOT_TAMGA_01);
    TJ_FIND_REGION("equip_slot_tool_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_EQUIP_SLOT_TOOL_01);
    TJ_FIND_REGION("equip_weapon_staff_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_EQUIP_WEAPON_STAFF_01);
    TJ_FIND_REGION("equip_clothes_cloak_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_EQUIP_CLOTHES_CLOAK_01);
    TJ_FIND_REGION("equip_tamga_charm_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_EQUIP_TAMGA_CHARM_01);
    TJ_FIND_REGION("equip_tool_satchel_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_EQUIP_TOOL_SATCHEL_01);
    TJ_FIND_REGION("icon_stamina_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_STAMINA_32);
    TJ_FIND_REGION("icon_supplies_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_SUPPLIES_32);
    TJ_FIND_REGION("icon_wisdom_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_WISDOM_32);
    TJ_FIND_REGION("icon_glory_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_GLORY_32);
    TJ_FIND_REGION("icon_circle_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_CIRCLE_32);
    TJ_FIND_REGION("icon_day_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_DAY_32);
    TJ_FIND_REGION("icon_body_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_BODY_32);
    TJ_FIND_REGION("icon_mind_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_MIND_32);
    TJ_FIND_REGION("icon_spirit_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_SPIRIT_32);
    TJ_FIND_REGION("icon_last_tamga_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_LAST_TAMGA_32);
    TJ_FIND_REGION("icon_settings_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_SETTINGS_32);
    TJ_FIND_REGION("icon_speed_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_SPEED_32);
    TJ_FIND_REGION("icon_aul_upgrade_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_AUL_UPGRADE_32);
    TJ_FIND_REGION("icon_card_gain_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_CARD_GAIN_32);
    TJ_FIND_REGION("icon_deck_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_DECK_32);
    TJ_FIND_REGION("icon_map_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_MAP_32);
    TJ_FIND_REGION("icon_memory_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_MEMORY_32);
    TJ_FIND_REGION("icon_warning_32", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_ICON_WARNING_32);
    TJ_FIND_REGION("fx_solid_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_SOLID_01);
    TJ_FIND_REGION("fx_sand_grain_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_SAND_GRAIN_01);
    TJ_FIND_REGION("fx_dust_step_00", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_DUST_STEP_00);
    TJ_FIND_REGION("fx_dust_step_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_DUST_STEP_01);
    TJ_FIND_REGION("fx_dust_step_02", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_DUST_STEP_02);
    TJ_FIND_REGION("fx_dust_step_03", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_DUST_STEP_03);
    TJ_FIND_REGION("fx_tile_placed_00", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_TILE_PLACED_00);
    TJ_FIND_REGION("fx_tile_placed_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_TILE_PLACED_01);
    TJ_FIND_REGION("fx_tile_placed_02", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_TILE_PLACED_02);
    TJ_FIND_REGION("fx_tile_placed_03", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_TILE_PLACED_03);
    TJ_FIND_REGION("fx_tile_trigger_00", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_TILE_TRIGGER_00);
    TJ_FIND_REGION("fx_tile_trigger_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_TILE_TRIGGER_01);
    TJ_FIND_REGION("fx_tile_trigger_02", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_TILE_TRIGGER_02);
    TJ_FIND_REGION("fx_tile_trigger_03", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_TILE_TRIGGER_03);
    TJ_FIND_REGION("fx_gain_popup_00", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_GAIN_POPUP_00);
    TJ_FIND_REGION("fx_gain_popup_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_GAIN_POPUP_01);
    TJ_FIND_REGION("fx_gain_popup_02", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_GAIN_POPUP_02);
    TJ_FIND_REGION("fx_invalid_cell_00", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_INVALID_CELL_00);
    TJ_FIND_REGION("fx_invalid_cell_01", ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_FX_INVALID_CELL_01);
#undef TJ_FIND_REGION
    return NT_ATLAS_INVALID_REGION;
}

static uint32_t count_bound_regions(const uint32_t *regions, uint32_t count) {
    uint32_t found = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (regions[i] != 0U && regions[i] != NT_ATLAS_INVALID_REGION) {
            found++;
        }
    }
    return found;
}

#if NT_DEVAPI_ENABLED
#ifndef NT_PLATFORM_WEB
/* devapi: dump the next rendered frame to a PNG (re-arms each call). "visual_qa.dump path=<file>"
 * Reuses the engine framebuffer readback; the file path is relative to the exe cwd. */
static char s_devapi_dump_path[260];
static int ep_dump_frame(int c, char **v, char *o, int cap, void *u) {
    (void)u;
    const char *path = "devapi_frame.png";
    for (int i = 0; i < c; i++) {
        if (strncmp(v[i], "path=", 5) == 0) {
            path = v[i] + 5;
        }
    }
    (void)snprintf(s_devapi_dump_path, sizeof s_devapi_dump_path, "%s", path);
    s_dump_frame_path = s_devapi_dump_path;
    s_dump_frame_after = 0U;
    s_dump_frame_done = false; /* re-arm: the frame loop dumps on the next renderable frame */
    return snprintf(o, (size_t)cap, "{\"dump\":\"%s\"}", s_devapi_dump_path);
}
#endif

/* QA-only evidence endpoint: proves the desktop runtime reached the visual QA scene. */
static int ep_visual_qa_status(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    const uint32_t batch_a_regions[] = {
        g.ground_sand_base,
        g.decor_dune,
        g.decor_stones,
        g.decor_dry_grass,
        g.decor_tracks,
        g.decor_bones,
        g.decor_cracks,
        g.road_straight_ns,
        g.road_straight_ew,
        g.road_corner_ne,
        g.road_corner_es,
        g.road_corner_sw,
        g.road_corner_wn,
        g.road_entry_aul,
        g.road_current_highlight,
        g.buffer_edge_stones,
        g.buffer_packed_sand,
        g.buffer_stakes,
        g.buffer_cart_marks,
        g.aul_ground_2x2,
        g.aul_yurt_small_01,
        g.aul_yurt_small_02,
        g.aul_fire_01,
        g.tile_saxaul,
        g.tile_yurt,
        g.tile_tamga_stone,
        g.tile_wolf_track,
        g.tile_oasis,
        g.tile_mirage,
        g.tile_storm,
        g.tile_last_tamga,
        g.tile_war[0],
        g.tile_war[1],
        g.tile_war[2],
        g.tile_horse[0],
        g.tile_horse[1],
        g.tile_horse[2],
        g.tile_steppe[0],
        g.tile_steppe[1],
        g.tile_steppe[2],
        g.tile_home[0],
        g.tile_home[1],
        g.tile_home[2],
        g.tile_water[0],
        g.tile_water[1],
        g.tile_water[2],
        g.boss_fat,
        g.boss_swift,
        g.boss_fierce,
        g.boss_ring_keeper,
        g.hero_wayfarer_idle_s,
        g.hero_wayfarer_walk_s,
        g.hero_wayfarer_walk_e,
        g.hero_wayfarer_walk_n,
        g.hero_wayfarer_walk_w,
        g.hero_wayfarer_panel,
        g.ui_panel_felt_dark_96,
        g.ui_panel_felt_light_96,
        g.ui_card_playable_96x128,
        g.ui_card_selected_96x128,
        g.ui_slot_equipment_64,
        g.ui_chip_resource_64,
        g.ui_tooltip_dark_64,
        g.die_d[0],
        g.die_d[1],
        g.die_d[2],
        g.die_d[3],
        g.die_d[4],
        g.die_d[5],
        g.pouch_closed,
        g.pouch_open[0],
        g.pouch_open[1],
        g.pouch_open[2],
        g.pouch_open[3],
    };
    const uint32_t batch_b_regions[] = {
        g.ui_card_back_96x128,
        g.ui_button_dark_64,
        g.ui_valid_cell_overlay_128,
        g.ui_invalid_cell_overlay_128,
        g.ui_hover_cell_overlay_128,
        g.card_badge_count_32,
        g.card_placement_roadside_32,
        g.card_placement_field_32,
        g.card_placement_special_32,
        g.card_art_saxaul_64,
        g.card_art_yurt_64,
        g.card_art_tamga_stone_64,
        g.card_art_wolf_track_64,
        g.equip_slot_weapon_01,
        g.equip_slot_clothes_01,
        g.equip_slot_tamga_01,
        g.equip_slot_tool_01,
        g.equip_weapon_staff_01,
        g.equip_clothes_cloak_01,
        g.equip_tamga_charm_01,
        g.equip_tool_satchel_01,
        g.icon_stamina_32,
        g.icon_supplies_32,
        g.icon_wisdom_32,
        g.icon_glory_32,
        g.icon_circle_32,
        g.icon_day_32,
        g.icon_body_32,
        g.icon_mind_32,
        g.icon_spirit_32,
        g.icon_last_tamga_32,
        g.icon_settings_32,
        g.icon_speed_32,
        g.fx_dust_step[0],
        g.fx_dust_step[1],
        g.fx_dust_step[2],
        g.fx_dust_step[3],
        g.fx_tile_placed[0],
        g.fx_tile_placed[1],
        g.fx_tile_placed[2],
        g.fx_tile_placed[3],
        g.fx_tile_trigger[0],
        g.fx_tile_trigger[1],
        g.fx_tile_trigger[2],
        g.fx_tile_trigger[3],
        g.fx_gain_popup[0],
        g.fx_gain_popup[1],
        g.fx_gain_popup[2],
        g.fx_invalid_cell[0],
        g.fx_invalid_cell[1],
    };
    const uint32_t batch_c_aul_regions[] = {
        g.aul_tamga_post_01, g.aul_stage_01_camp, g.aul_stage_02_settlement, g.aul_stage_03_village, g.aul_stage_04_fortified_aul, g.aul_stage_05_steppe_capital,
    };
    const uint32_t batch_c_hero_panel_regions[] = {
        g.hero_body_panel,
        g.hero_mind_panel,
        g.hero_spirit_panel,
    };
    const uint32_t pass6_future_regions[] = {
        g.card_art_oasis_64,     g.card_art_mirage_64,  g.card_art_storm_64,  g.card_art_last_tamga_64, g.card_art_well_64,      g.card_art_watchtower_64, g.tile_well_01,
        g.tile_watchtower_01,    g.tile_pack_01,        g.tile_small_camp_01, g.tile_clan_camp_01,      g.tile_hunting_trail_01, g.tile_vision_01,         g.tile_false_path_01,
        g.tile_buried_spring_01, g.icon_aul_upgrade_32, g.icon_card_gain_32,  g.icon_deck_32,           g.icon_map_32,           g.icon_memory_32,         g.icon_warning_32,
    };
    return snprintf(o, (size_t)cap,
                    "{\"scene\":\"%s\",\"visual_qa\":%s,\"resources_ready\":%s,\"logical\":[%u,%u],\"batch_a\":[%u,%u],\"batch_b\":[%u,%u],\"batch_c_aul\":[%u,%u],"
                    "\"batch_c_hero_panels\":[%u,%u],\"pass6_future\":[%u,%u],"
                    "\"draw_groups\":[\"gameplay_composition\",\"active_tiles\",\"hud_cards_icons\",\"hero_equipment\",\"fx_strips\",\"aul_progression\",\"ui_surfaces\",\"future_library\",\"hero_"
                    "archetype_panels\"]}",
                    g.scene ? g.scene->name : "", (g.scene == &SCENE_VISUAL_QA) ? "true" : "false", g.resources_ready ? "true" : "false", (uint32_t)g.logical_w, (uint32_t)g.logical_h,
                    count_bound_regions(batch_a_regions, TJ_ARRAY_COUNT(batch_a_regions)), TJ_ARRAY_COUNT(batch_a_regions), count_bound_regions(batch_b_regions, TJ_ARRAY_COUNT(batch_b_regions)),
                    TJ_ARRAY_COUNT(batch_b_regions), count_bound_regions(batch_c_aul_regions, TJ_ARRAY_COUNT(batch_c_aul_regions)), TJ_ARRAY_COUNT(batch_c_aul_regions),
                    count_bound_regions(batch_c_hero_panel_regions, TJ_ARRAY_COUNT(batch_c_hero_panel_regions)), TJ_ARRAY_COUNT(batch_c_hero_panel_regions),
                    count_bound_regions(pass6_future_regions, TJ_ARRAY_COUNT(pass6_future_regions)), TJ_ARRAY_COUNT(pass6_future_regions));
}
#endif

static void bind_optional_world_regions(void) {
    g.ground_sand_base = find_atlas_region("ground_sand_base_01");
    g.decor_dune = find_atlas_region("decor_dune_01");
    g.decor_stones = find_atlas_region("decor_stones_01");
    g.decor_dry_grass = find_atlas_region("decor_dry_grass_01");
    g.decor_tracks = find_atlas_region("decor_tracks_01");
    g.decor_bones = find_atlas_region("decor_bones_01");
    g.decor_cracks = find_atlas_region("decor_cracks_01");
    g.road_straight_ns = find_atlas_region("road_straight_ns");
    g.road_straight_ew = find_atlas_region("road_straight_ew");
    g.road_corner_ne = find_atlas_region("road_corner_ne");
    g.road_corner_es = find_atlas_region("road_corner_es");
    g.road_corner_sw = find_atlas_region("road_corner_sw");
    g.road_corner_wn = find_atlas_region("road_corner_wn");
    g.road_entry_aul = find_atlas_region("road_entry_aul");
    g.road_current_highlight = find_atlas_region("road_current_highlight");
    g.buffer_edge_stones = find_atlas_region("buffer_edge_stones_01");
    g.buffer_packed_sand = find_atlas_region("buffer_packed_sand_01");
    g.buffer_stakes = find_atlas_region("buffer_stakes_01");
    g.buffer_cart_marks = find_atlas_region("buffer_cart_marks_01");
    g.aul_ground_2x2 = find_atlas_region("aul_ground_2x2");
    g.aul_yurt_small_01 = find_atlas_region("aul_yurt_small_01");
    g.aul_yurt_small_02 = find_atlas_region("aul_yurt_small_02");
    g.aul_fire_01 = find_atlas_region("aul_fire_01");
    g.aul_tamga_post_01 = find_atlas_region("aul_tamga_post_01");
    g.aul_stage_01_camp = find_atlas_region("aul_stage_01_camp");
    g.aul_stage_02_settlement = find_atlas_region("aul_stage_02_settlement");
    g.aul_stage_03_village = find_atlas_region("aul_stage_03_village");
    g.aul_stage_04_fortified_aul = find_atlas_region("aul_stage_04_fortified_aul");
    g.aul_stage_05_steppe_capital = find_atlas_region("aul_stage_05_steppe_capital");
    g.tile_saxaul = find_atlas_region("tile_saxaul_01");
    g.tile_oasis = find_atlas_region("tile_oasis_01");
    g.tile_yurt = find_atlas_region("tile_yurt_01");
    g.tile_tamga_stone = find_atlas_region("tile_tamga_stone_01");
    g.tile_wolf_track = find_atlas_region("tile_wolf_track_01");
    g.tile_mirage = find_atlas_region("tile_mirage_01");
    g.tile_storm = find_atlas_region("tile_storm_01");
    g.tile_last_tamga = find_atlas_region("tile_last_tamga_01");
    g.tile_war[0] = find_atlas_region("tile_war_1");
    g.tile_war[1] = find_atlas_region("tile_war_2");
    g.tile_war[2] = find_atlas_region("tile_war_3");
    g.tile_horse[0] = find_atlas_region("tile_horse_1");
    g.tile_horse[1] = find_atlas_region("tile_horse_2");
    g.tile_horse[2] = find_atlas_region("tile_horse_3");
    g.tile_steppe[0] = find_atlas_region("tile_steppe_1");
    g.tile_steppe[1] = find_atlas_region("tile_steppe_2");
    g.tile_steppe[2] = find_atlas_region("tile_steppe_3");
    g.tile_home[0] = find_atlas_region("tile_home_1");
    g.tile_home[1] = find_atlas_region("tile_home_2");
    g.tile_home[2] = find_atlas_region("tile_home_3");
    g.tile_water[0] = find_atlas_region("tile_water_1");
    g.tile_water[1] = find_atlas_region("tile_water_2");
    g.tile_water[2] = find_atlas_region("tile_water_3");
    g.boss_fat = find_atlas_region("boss_fat");
    g.boss_swift = find_atlas_region("boss_swift");
    g.boss_fierce = find_atlas_region("boss_fierce");
    g.boss_ring_keeper = find_atlas_region("boss_ring_keeper");
    g.tile_well_01 = find_atlas_region("tile_well_01");
    g.tile_watchtower_01 = find_atlas_region("tile_watchtower_01");
    g.tile_pack_01 = find_atlas_region("tile_pack_01");
    g.tile_small_camp_01 = find_atlas_region("tile_small_camp_01");
    g.tile_clan_camp_01 = find_atlas_region("tile_clan_camp_01");
    g.tile_hunting_trail_01 = find_atlas_region("tile_hunting_trail_01");
    g.tile_vision_01 = find_atlas_region("tile_vision_01");
    g.tile_false_path_01 = find_atlas_region("tile_false_path_01");
    g.tile_buried_spring_01 = find_atlas_region("tile_buried_spring_01");
    g.hero_wayfarer_idle_s = find_atlas_region("hero_wayfarer_idle_s");
    g.hero_wayfarer_walk_s = find_atlas_region("hero_wayfarer_walk_s");
    g.hero_wayfarer_walk_e = find_atlas_region("hero_wayfarer_walk_e");
    g.hero_wayfarer_walk_n = find_atlas_region("hero_wayfarer_walk_n");
    g.hero_wayfarer_walk_w = find_atlas_region("hero_wayfarer_walk_w");
    g.hero_wayfarer_panel = find_atlas_region("hero_wayfarer_panel");
    g.hero_body_panel = find_atlas_region("hero_body_panel");
    g.hero_mind_panel = find_atlas_region("hero_mind_panel");
    g.hero_spirit_panel = find_atlas_region("hero_spirit_panel");
    g.ui_panel_felt_dark_96 = find_atlas_region("ui_panel_felt_dark_96");
    g.ui_panel_felt_light_96 = find_atlas_region("ui_panel_felt_light_96");
    g.ui_card_playable_96x128 = find_atlas_region("ui_card_playable_96x128");
    g.ui_card_selected_96x128 = find_atlas_region("ui_card_selected_96x128");
    g.ui_slot_equipment_64 = find_atlas_region("ui_slot_equipment_64");
    g.ui_chip_resource_64 = find_atlas_region("ui_chip_resource_64");
    g.ui_tooltip_dark_64 = find_atlas_region("ui_tooltip_dark_64");
    g.die_d[0] = find_atlas_region("die_d4");
    g.die_d[1] = find_atlas_region("die_d6");
    g.die_d[2] = find_atlas_region("die_d8");
    g.die_d[3] = find_atlas_region("die_d10");
    g.die_d[4] = find_atlas_region("die_d12");
    g.die_d[5] = find_atlas_region("die_d20");
    g.pouch_closed = find_atlas_region("pouch_closed");
    g.pouch_open[0] = find_atlas_region("pouch_open_0");
    g.pouch_open[1] = find_atlas_region("pouch_open_1");
    g.pouch_open[2] = find_atlas_region("pouch_open_2");
    g.pouch_open[3] = find_atlas_region("pouch_open_3");
    g.ui_card_back_96x128 = find_atlas_region("ui_card_back_96x128");
    g.ui_button_dark_64 = find_atlas_region("ui_button_dark_64");
    g.ui_finger_pointer_128 = find_atlas_region("ui_finger_pointer_128");
    g.ui_valid_cell_overlay_128 = find_atlas_region("ui_valid_cell_overlay_128");
    g.ui_invalid_cell_overlay_128 = find_atlas_region("ui_invalid_cell_overlay_128");
    g.ui_hover_cell_overlay_128 = find_atlas_region("ui_hover_cell_overlay_128");
    g.card_badge_count_32 = find_atlas_region("card_badge_count_32");
    g.card_placement_roadside_32 = find_atlas_region("card_placement_roadside_32");
    g.card_placement_field_32 = find_atlas_region("card_placement_field_32");
    g.card_placement_special_32 = find_atlas_region("card_placement_special_32");
    g.card_art_saxaul_64 = find_atlas_region("card_art_saxaul_64");
    g.card_art_yurt_64 = find_atlas_region("card_art_yurt_64");
    g.card_art_tamga_stone_64 = find_atlas_region("card_art_tamga_stone_64");
    g.card_art_wolf_track_64 = find_atlas_region("card_art_wolf_track_64");
    g.card_art_oasis_64 = find_atlas_region("card_art_oasis_64");
    g.card_art_mirage_64 = find_atlas_region("card_art_mirage_64");
    g.card_art_storm_64 = find_atlas_region("card_art_storm_64");
    g.card_art_last_tamga_64 = find_atlas_region("card_art_last_tamga_64");
    g.card_art_well_64 = find_atlas_region("card_art_well_64");
    g.card_art_watchtower_64 = find_atlas_region("card_art_watchtower_64");
    g.equip_slot_weapon_01 = find_atlas_region("equip_slot_weapon_01");
    g.equip_slot_clothes_01 = find_atlas_region("equip_slot_clothes_01");
    g.equip_slot_tamga_01 = find_atlas_region("equip_slot_tamga_01");
    g.equip_slot_tool_01 = find_atlas_region("equip_slot_tool_01");
    g.equip_weapon_staff_01 = find_atlas_region("equip_weapon_staff_01");
    g.equip_clothes_cloak_01 = find_atlas_region("equip_clothes_cloak_01");
    g.equip_tamga_charm_01 = find_atlas_region("equip_tamga_charm_01");
    g.equip_tool_satchel_01 = find_atlas_region("equip_tool_satchel_01");
    g.icon_stamina_32 = find_atlas_region("icon_stamina_32");
    g.icon_supplies_32 = find_atlas_region("icon_supplies_32");
    g.icon_wisdom_32 = find_atlas_region("icon_wisdom_32");
    g.icon_glory_32 = find_atlas_region("icon_glory_32");
    g.icon_circle_32 = find_atlas_region("icon_circle_32");
    g.icon_day_32 = find_atlas_region("icon_day_32");
    g.icon_body_32 = find_atlas_region("icon_body_32");
    g.icon_mind_32 = find_atlas_region("icon_mind_32");
    g.icon_spirit_32 = find_atlas_region("icon_spirit_32");
    g.icon_last_tamga_32 = find_atlas_region("icon_last_tamga_32");
    g.icon_settings_32 = find_atlas_region("icon_settings_32");
    g.icon_speed_32 = find_atlas_region("icon_speed_32");
    g.icon_aul_upgrade_32 = find_atlas_region("icon_aul_upgrade_32");
    g.icon_card_gain_32 = find_atlas_region("icon_card_gain_32");
    g.icon_deck_32 = find_atlas_region("icon_deck_32");
    g.icon_map_32 = find_atlas_region("icon_map_32");
    g.icon_memory_32 = find_atlas_region("icon_memory_32");
    g.icon_warning_32 = find_atlas_region("icon_warning_32");
    g.fx_solid_01 = find_atlas_region("fx_solid_01");
    g.fx_sand_grain_01 = find_atlas_region("fx_sand_grain_01");
    g.fx_dust_step[0] = find_atlas_region("fx_dust_step_00");
    g.fx_dust_step[1] = find_atlas_region("fx_dust_step_01");
    g.fx_dust_step[2] = find_atlas_region("fx_dust_step_02");
    g.fx_dust_step[3] = find_atlas_region("fx_dust_step_03");
    g.fx_tile_placed[0] = find_atlas_region("fx_tile_placed_00");
    g.fx_tile_placed[1] = find_atlas_region("fx_tile_placed_01");
    g.fx_tile_placed[2] = find_atlas_region("fx_tile_placed_02");
    g.fx_tile_placed[3] = find_atlas_region("fx_tile_placed_03");
    g.fx_tile_trigger[0] = find_atlas_region("fx_tile_trigger_00");
    g.fx_tile_trigger[1] = find_atlas_region("fx_tile_trigger_01");
    g.fx_tile_trigger[2] = find_atlas_region("fx_tile_trigger_02");
    g.fx_tile_trigger[3] = find_atlas_region("fx_tile_trigger_03");
    g.fx_gain_popup[0] = find_atlas_region("fx_gain_popup_00");
    g.fx_gain_popup[1] = find_atlas_region("fx_gain_popup_01");
    g.fx_gain_popup[2] = find_atlas_region("fx_gain_popup_02");
    g.fx_invalid_cell[0] = find_atlas_region("fx_invalid_cell_00");
    g.fx_invalid_cell[1] = find_atlas_region("fx_invalid_cell_01");
    const uint32_t batch_a_regions[] = {
        g.ground_sand_base,
        g.decor_dune,
        g.decor_stones,
        g.decor_dry_grass,
        g.decor_tracks,
        g.decor_bones,
        g.decor_cracks,
        g.road_straight_ns,
        g.road_straight_ew,
        g.road_corner_ne,
        g.road_corner_es,
        g.road_corner_sw,
        g.road_corner_wn,
        g.road_entry_aul,
        g.road_current_highlight,
        g.buffer_edge_stones,
        g.buffer_packed_sand,
        g.buffer_stakes,
        g.buffer_cart_marks,
        g.aul_ground_2x2,
        g.aul_yurt_small_01,
        g.aul_yurt_small_02,
        g.aul_fire_01,
        g.tile_saxaul,
        g.tile_yurt,
        g.tile_tamga_stone,
        g.tile_wolf_track,
        g.tile_oasis,
        g.tile_mirage,
        g.tile_storm,
        g.tile_last_tamga,
        g.tile_war[0],
        g.tile_war[1],
        g.tile_war[2],
        g.tile_horse[0],
        g.tile_horse[1],
        g.tile_horse[2],
        g.tile_steppe[0],
        g.tile_steppe[1],
        g.tile_steppe[2],
        g.tile_home[0],
        g.tile_home[1],
        g.tile_home[2],
        g.tile_water[0],
        g.tile_water[1],
        g.tile_water[2],
        g.boss_fat,
        g.boss_swift,
        g.boss_fierce,
        g.boss_ring_keeper,
        g.hero_wayfarer_idle_s,
        g.hero_wayfarer_walk_s,
        g.hero_wayfarer_walk_e,
        g.hero_wayfarer_walk_n,
        g.hero_wayfarer_walk_w,
        g.hero_wayfarer_panel,
        g.ui_panel_felt_dark_96,
        g.ui_panel_felt_light_96,
        g.ui_card_playable_96x128,
        g.ui_card_selected_96x128,
        g.ui_slot_equipment_64,
        g.ui_chip_resource_64,
        g.ui_tooltip_dark_64,
        g.die_d[0],
        g.die_d[1],
        g.die_d[2],
        g.die_d[3],
        g.die_d[4],
        g.die_d[5],
        g.pouch_closed,
        g.pouch_open[0],
        g.pouch_open[1],
        g.pouch_open[2],
        g.pouch_open[3],
    };
    nt_log_info("turkic_jam: optional Batch A atlas regions %u/%u", count_bound_regions(batch_a_regions, (uint32_t)(sizeof(batch_a_regions) / sizeof(batch_a_regions[0]))),
                (unsigned)(sizeof(batch_a_regions) / sizeof(batch_a_regions[0])));
    const uint32_t batch_b_regions[] = {
        g.ui_card_back_96x128,
        g.ui_button_dark_64,
        g.ui_valid_cell_overlay_128,
        g.ui_invalid_cell_overlay_128,
        g.ui_hover_cell_overlay_128,
        g.card_badge_count_32,
        g.card_placement_roadside_32,
        g.card_placement_field_32,
        g.card_placement_special_32,
        g.card_art_saxaul_64,
        g.card_art_yurt_64,
        g.card_art_tamga_stone_64,
        g.card_art_wolf_track_64,
        g.equip_slot_weapon_01,
        g.equip_slot_clothes_01,
        g.equip_slot_tamga_01,
        g.equip_slot_tool_01,
        g.equip_weapon_staff_01,
        g.equip_clothes_cloak_01,
        g.equip_tamga_charm_01,
        g.equip_tool_satchel_01,
        g.icon_stamina_32,
        g.icon_supplies_32,
        g.icon_wisdom_32,
        g.icon_glory_32,
        g.icon_circle_32,
        g.icon_day_32,
        g.icon_body_32,
        g.icon_mind_32,
        g.icon_spirit_32,
        g.icon_last_tamga_32,
        g.icon_settings_32,
        g.icon_speed_32,
        g.fx_dust_step[0],
        g.fx_dust_step[1],
        g.fx_dust_step[2],
        g.fx_dust_step[3],
        g.fx_tile_placed[0],
        g.fx_tile_placed[1],
        g.fx_tile_placed[2],
        g.fx_tile_placed[3],
        g.fx_tile_trigger[0],
        g.fx_tile_trigger[1],
        g.fx_tile_trigger[2],
        g.fx_tile_trigger[3],
        g.fx_gain_popup[0],
        g.fx_gain_popup[1],
        g.fx_gain_popup[2],
        g.fx_invalid_cell[0],
        g.fx_invalid_cell[1],
    };
    nt_log_info("turkic_jam: optional Batch B atlas regions %u/%u", count_bound_regions(batch_b_regions, (uint32_t)(sizeof(batch_b_regions) / sizeof(batch_b_regions[0]))),
                (unsigned)(sizeof(batch_b_regions) / sizeof(batch_b_regions[0])));
    const uint32_t batch_c_aul_regions[] = {
        g.aul_tamga_post_01, g.aul_stage_01_camp, g.aul_stage_02_settlement, g.aul_stage_03_village, g.aul_stage_04_fortified_aul, g.aul_stage_05_steppe_capital,
    };
    nt_log_info("turkic_jam: optional Batch C aul progression atlas regions %u/%u", count_bound_regions(batch_c_aul_regions, (uint32_t)(sizeof(batch_c_aul_regions) / sizeof(batch_c_aul_regions[0]))),
                (unsigned)(sizeof(batch_c_aul_regions) / sizeof(batch_c_aul_regions[0])));
    const uint32_t batch_c_hero_panel_regions[] = {
        g.hero_body_panel,
        g.hero_mind_panel,
        g.hero_spirit_panel,
    };
    nt_log_info("turkic_jam: optional Batch C hero archetype panels atlas regions %u/%u",
                count_bound_regions(batch_c_hero_panel_regions, (uint32_t)(sizeof(batch_c_hero_panel_regions) / sizeof(batch_c_hero_panel_regions[0]))),
                (unsigned)(sizeof(batch_c_hero_panel_regions) / sizeof(batch_c_hero_panel_regions[0])));
    const uint32_t pass6_future_regions[] = {
        g.card_art_oasis_64,     g.card_art_mirage_64,  g.card_art_storm_64,  g.card_art_last_tamga_64, g.card_art_well_64,      g.card_art_watchtower_64, g.tile_well_01,
        g.tile_watchtower_01,    g.tile_pack_01,        g.tile_small_camp_01, g.tile_clan_camp_01,      g.tile_hunting_trail_01, g.tile_vision_01,         g.tile_false_path_01,
        g.tile_buried_spring_01, g.icon_aul_upgrade_32, g.icon_card_gain_32,  g.icon_deck_32,           g.icon_map_32,           g.icon_memory_32,         g.icon_warning_32,
    };
    nt_log_info("turkic_jam: optional Pass 6 future library atlas regions %u/%u", count_bound_regions(pass6_future_regions, (uint32_t)(sizeof(pass6_future_regions) / sizeof(pass6_future_regions[0]))),
                (unsigned)(sizeof(pass6_future_regions) / sizeof(pass6_future_regions[0])));
}

static void try_bind_resources(void) {
    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        bind_atlas();
    }
    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        bind_font();
    }
    g.resources_ready = s_atlas_bound && s_font_bound;
}

/* Shared shell: dark backdrop. Most scenes sit in a centered card; a fullscreen
 * scene (the run) instead fills the whole screen itself. */
static void build_scene_shell(const nt_ui_transform_t *shake_xform) {
    const bool fullscreen = g.scene && g.scene->fullscreen;
    CLAY({.id = CLAY_ID("root"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .padding = fullscreen ? CLAY_PADDING_ALL(0) : CLAY_PADDING_ALL(24),
                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {18.0F, 16.0F, 14.0F, 255.0F}}) {
        /* Settings modal floats above whichever scene is active. Built first so
           its controls win pointer capture; scene_game pauses on g.settings_open. */
        if (g.settings_open && tj_view_settings_modal(&g, g_nt_app.dt)) {
            g.settings_open = false;
        }
        if (fullscreen) {
            if (g.scene->on_update) {
                g.scene->on_update(&g, g_nt_app.dt);
            }
        } else {
            CLAY({.id = CLAY_ID("card"),
                  .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)},
                             .padding = CLAY_PADDING_ALL(48),
                             .layoutDirection = CLAY_TOP_TO_BOTTOM,
                             .childGap = 22,
                             .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = {35.0F, 31.0F, 24.0F, 240.0F},
                  .cornerRadius = CLAY_CORNER_RADIUS(28),
                  .userData = (void *)NT_UI_DATA_XFORM(0U, shake_xform, 1.0F)}) {
                if (g.scene->on_update) {
                    g.scene->on_update(&g, g_nt_app.dt);
                }
            }
        }
    }
}

#ifndef NT_PLATFORM_WEB
static bool dump_frame_png(const char *path, uint32_t width, uint32_t height) {
    if (path == NULL || width == 0 || height == 0) {
        return false;
    }
    const uint64_t byte_count64 = (uint64_t)width * (uint64_t)height * 4ULL;
    if (byte_count64 > SIZE_MAX) {
        nt_log_error("visual_qa: dump-frame too large (%ux%u)", width, height);
        return false;
    }
    const size_t byte_count = (size_t)byte_count64;
    const size_t stride = (size_t)width * 4U;
    uint8_t *pixels = (uint8_t *)malloc(byte_count);
    if (pixels == NULL) {
        nt_log_error("visual_qa: dump-frame allocation failed (%zu bytes)", byte_count);
        return false;
    }
    if (!nt_gfx_readback_rgba8(pixels, width, height)) {
        free(pixels);
        nt_log_error("visual_qa: framebuffer readback failed");
        return false;
    }

    bool all_black = true;
    bool all_white = true;
    bool all_same = true;
    uint8_t first[4] = {pixels[0], pixels[1], pixels[2], pixels[3]};
    uint8_t min_rgb = 255U;
    uint8_t max_rgb = 0U;
    uint32_t checksum = 2166136261U;
    for (size_t i = 0; i < byte_count; i += 4U) {
        const uint8_t r = pixels[i + 0U];
        const uint8_t gch = pixels[i + 1U];
        const uint8_t b = pixels[i + 2U];
        const uint8_t a = pixels[i + 3U];
        all_black = all_black && r == 0U && gch == 0U && b == 0U;
        all_white = all_white && r == 255U && gch == 255U && b == 255U;
        all_same = all_same && r == first[0] && gch == first[1] && b == first[2] && a == first[3];
        if (r < min_rgb) {
            min_rgb = r;
        }
        if (gch < min_rgb) {
            min_rgb = gch;
        }
        if (b < min_rgb) {
            min_rgb = b;
        }
        if (r > max_rgb) {
            max_rgb = r;
        }
        if (gch > max_rgb) {
            max_rgb = gch;
        }
        if (b > max_rgb) {
            max_rgb = b;
        }
        checksum ^= r;
        checksum *= 16777619U;
        checksum ^= gch;
        checksum *= 16777619U;
        checksum ^= b;
        checksum *= 16777619U;
        checksum ^= a;
        checksum *= 16777619U;
    }

    for (uint32_t y = 0; y < height / 2U; y++) {
        uint8_t *top = pixels + (size_t)y * stride;
        uint8_t *bottom = pixels + (size_t)(height - 1U - y) * stride;
        for (size_t x = 0; x < stride; x++) {
            const uint8_t tmp = top[x];
            top[x] = bottom[x];
            bottom[x] = tmp;
        }
    }

    const int written = stbi_write_png(path, (int)width, (int)height, 4, pixels, (int)stride);
    free(pixels);
    nt_log_info("visual_qa: dump-frame path=%s size=%ux%u all_black=%d all_white=%d all_same=%d rgb_range=%u..%u checksum=0x%08X", path, width, height, all_black ? 1 : 0, all_white ? 1 : 0,
                all_same ? 1 : 0, min_rgb, max_rgb, checksum);
    if (!written) {
        nt_log_error("visual_qa: PNG write failed: %s", path);
        return false;
    }
    return true;
}
#endif

// #region frame
static void frame(void) {
    nt_window_poll();
    nt_input_poll();

    /* Audio: resume on the first user gesture (web autoplay policy), advance
       async loads + reap voices, and click SFX on press. */
    if (nt_audio_get_state() == NT_AUDIO_SUSPENDED && nt_input_mouse_is_pressed(NT_BUTTON_LEFT)) {
        nt_audio_try_resume();
    }
    nt_audio_update();
    tj_audio_assets_poll();
    if (nt_input_mouse_is_pressed(NT_BUTTON_LEFT)) {
        tj_audio_play_click();
    }

    nt_devapi_apply_pending(); /* inject queued synthetic input (no-op unless devapi on) */
    nt_devapi_net_poll();      /* serve debug/bot commands */
    nt_mem_scratch_reset();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    apply_transition();

    nt_resource_step();
    nt_material_step();
    try_bind_resources();

    tj_shake_update(&g.shake, g_nt_app.dt);

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);

    nt_ui_scale_desc_t scale_desc = {.ref_w = TJ_REF_W, .ref_h = TJ_REF_H, .mode = NT_UI_SCALE_EXPAND};
    nt_ui_scale_t scale = nt_ui_compute_scale(&scale_desc, fb_w, fb_h);
    nt_ui_scale_ortho_t ortho = nt_ui_scale_ortho(&scale);
    g.logical_w = scale.logical_w;
    g.logical_h = scale.logical_h;
    nt_devapi_set_view(fb_w, fb_h, scale.logical_w, scale.logical_h);

    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_mat4_identity(view_m);
    glm_ortho(ortho.left, ortho.right, ortho.bottom, ortho.top, -1.0F, 1.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.resolution[0] = fb_w;
    uniforms.resolution[1] = fb_h;
    uniforms.resolution[2] = (fb_w > 0.0F) ? (1.0F / fb_w) : 0.0F;
    uniforms.resolution[3] = (fb_h > 0.0F) ? (1.0F / fb_h) : 0.0F;
    uniforms.near_far[0] = -1.0F;
    uniforms.near_far[1] = 1.0F;

    nt_gfx_begin_frame();
    nt_gfx_begin_segment("frame");
    if (g_nt_gfx.context_restored) {
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        nt_resource_invalidate(NT_ASSET_TEXTURE);
        nt_resource_invalidate(NT_ASSET_FONT);
        nt_gfx_destroy_buffer(s_frame_ubo);
        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_frame_uniforms_t),
            .label = "frame_uniforms",
        });
        nt_sprite_renderer_restore_gpu();
        nt_text_renderer_restore_gpu();
        s_atlas_bound = false;
        s_font_bound = false;
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .clear_color = {0.07F, 0.06F, 0.05F, 1.0F},
        .clear_depth = 1.0F,
    });

    nt_font_step();

    const nt_material_info_t *sprite_info = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    const bool can_render = s_atlas_bound && s_font_bound && sprite_info && sprite_info->ready && text_info && text_info->ready;

    if (can_render) {
        nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        const nt_pointer_t mouse_logical = nt_ui_scale_apply_pointer(&scale, g_nt_input.pointers[0]);
        g.ptr_x = mouse_logical.x; /* expose to scenes for map click->cell + hover */
        g.ptr_y = mouse_logical.y;
        nt_ui_begin(g.ui, scale.logical_w, scale.logical_h, g_nt_app.dt, &mouse_logical, 1);

        /* Screen-shake (juice): trauma -> small offset+rotation on the card. */
        nt_ui_transform_t shake_xform = nt_ui_transform_defaults();
        {
            float sdx;
            float sdy;
            float sdeg;
            tj_shake_sample(&g.shake, 14.0F, 1.5F, &sdx, &sdy, &sdeg);
            shake_xform.offset_x = sdx;
            shake_xform.offset_y = sdy;
            shake_xform.rotation_z = sdeg * 0.017453292F; /* deg -> rad */
        }

        build_scene_shell(&shake_xform);

        nt_ui_end(g.ui);

        nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(g.ui, &target);
    }

    nt_gfx_end_pass();
    nt_gfx_end_segment();
    nt_gfx_end_frame();
#ifndef NT_PLATFORM_WEB
    if (can_render && s_dump_frame_path != NULL && !s_dump_frame_done) {
        if (s_dump_frame_after > 0U) {
            s_dump_frame_after--;
        } else {
            s_dump_frame_done = true;
            (void)dump_frame_png(s_dump_frame_path, g_nt_window.fb_width, g_nt_window.fb_height);
            if (s_exit_after_frame) {
                nt_app_quit();
            }
        }
    } else if (can_render && s_exit_after_frame && s_dump_frame_path == NULL) {
        nt_app_quit();
    }
#endif
    nt_window_swap_buffers();
}
// #endregion

// #region main + init
int main(int argc, char *argv[]) {
    int devapi_port = 0;
    const char *config_dir = "config";
    bool visual_qa = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--devapi") == 0 && i + 1 < argc) {
            devapi_port = (int)strtol(argv[i + 1], NULL, 10);
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_dir = argv[i + 1];
        } else if (strcmp(argv[i], "--visual-qa") == 0) {
            visual_qa = true;
#ifndef NT_PLATFORM_WEB
        } else if (strcmp(argv[i], "--dump-frame") == 0 && i + 1 < argc) {
            s_dump_frame_path = argv[i + 1];
        } else if (strcmp(argv[i], "--dump-frame-after") == 0 && i + 1 < argc) {
            const long frames = strtol(argv[i + 1], NULL, 10);
            s_dump_frame_after = frames > 0 ? (uint32_t)frames : 0U;
        } else if (strcmp(argv[i], "--exit-after-frame") == 0) {
            s_exit_after_frame = true;
#endif
        }
    }

    nt_engine_config_t config = {0};
    config.app_name = "turkic_jam";
    config.version = 1;
    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }

    g_nt_window.width = 1280;
    g_nt_window.height = 720;
    g_nt_window.title = "Песнь Тамги";
    nt_window_init();
    nt_input_init();

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx_desc);
    nt_gfx_register_global_block("Globals", 0);

    nt_http_init();
    nt_fs_init();
    nt_audio_init();
    tj_audio_assets_init(); /* kicks off async music/SFX loads (fs/http ready above) */
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_mem_scratch_init(TJ_SCRATCH_ARENA_SIZE);

    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);
    nt_atlas_init();

    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 2});

    nt_sprite_renderer_desc_t sr_desc = nt_sprite_renderer_desc_defaults();
    nt_sprite_renderer_init(&sr_desc);
    nt_text_renderer_init();

    nt_ui_module_init();
    const nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
    g.ui = nt_ui_create_context(s_ui_arena, sizeof s_ui_arena, &ui_desc);
    NT_ASSERT(g.ui != NULL && "turkic_jam: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    const nt_hash32_t pack_id = nt_hash32_str("turkic_jam");
    nt_resource_mount(pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(pack_id, NT_CDN_URL "/turkic_jam/turkic_jam.ntpack");
#else
    nt_resource_load_auto(pack_id, "assets/turkic_jam.ntpack");
#endif

    s_sprite_vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_TURKIC_JAM_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_TURKIC_JAM_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_TURKIC_JAM_FONT, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_sprite_vs,
        .fs = s_sprite_fs,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "turkic_jam_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_text_vs,
        .fs = s_text_fs,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "turkic_jam_text",
    });

    nt_ui_set_sprite_material(g.ui, s_sprite_material);
    nt_ui_set_text_material(g.ui, s_text_material);
    g.sprite_material = s_sprite_material; /* lets the map render directly via the sprite renderer. */
    tj_view_register_world(&g);            /* CUSTOM render handler draws the map world during the UI walk. */

    g.font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
        .measure_cache_size = 256,
    });

    nt_resource_set_activate_time_budget(0);

    /* Persistence + restore last language before the first frame. */
    rng_seed(0xC0FFEEU);
    save_init();
    tj_audio_load_volumes_from_save();
    i18n_set((i18n_lang_t)save_get_int("lang", LANG_RU));

    tj_config_load(config_dir);
    nt_log_info("turkic_jam: config '%s' -> %d tiles, %d heirs, %d cells", config_dir, g_config.tile_count, g_config.heir_count, g_config.path_cells);
    tj_aul_load();

    /* Debug/automation command bus (no-op unless NT_DEVAPI_ENABLED). */
    nt_devapi_init();
    nt_devapi_register_builtins();
    nt_devapi_set_ui_context(g.ui);
#if NT_DEVAPI_ENABLED
    nt_devapi_register("game.config", ep_game_config, NULL);
    nt_devapi_register("visual_qa.status", ep_visual_qa_status, NULL);
#ifndef NT_PLATFORM_WEB
    nt_devapi_register("visual_qa.dump", ep_dump_frame, NULL);
#endif
#endif

    /* Initial scene; its on_enter may register more endpoints (e.g. game.run),
     * so it must run AFTER nt_devapi_init() clears the registry. */
    /* The cinematic intro is the front door now; SCENE_MENU is deprecated and no longer
     * a boot target. Go straight into the run (heir-select only if multiple heirs exist);
     * visual_qa stays a debug override. */
    const scene_t *boot = (g_config.heir_count > 1) ? &SCENE_HEIR_SELECT : &SCENE_GAME;
    g.scene = visual_qa ? &SCENE_VISUAL_QA : boot;
    if (g.scene->on_enter) {
        g.scene->on_enter(&g);
    }

    /* Start the TCP server last — after endpoints + the first scene are ready. */
    if (devapi_port > 0) {
        nt_devapi_net_start((uint16_t)devapi_port);
    }

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_log_info("turkic_jam: starting (scene=%s)", g.scene->name);

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_devapi_net_stop();
    nt_devapi_shutdown();
    nt_audio_shutdown();
    nt_ui_destroy_context(g.ui);
    nt_ui_module_shutdown();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_font_destroy(g.font);
    nt_font_shutdown();
    nt_material_destroy(s_sprite_material);
    nt_material_destroy(s_text_material);
    nt_material_shutdown();
    nt_mem_scratch_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_destroy_buffer(s_frame_ubo);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
// #endregion
