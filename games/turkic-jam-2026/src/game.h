#ifndef TJ_GAME_H
#define TJ_GAME_H

/* Shared game context + constants. Passed to every scene; holds the UI context
 * and bound render resources so scenes never touch engine init directly. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"
#include "material/nt_material.h"
#include "resource/nt_resource.h"

#include "juice.h"
#include "scene.h"

/* Clay userData layer tags (debug-walker layer sort). */
#define TJ_LAYER_IMG 1
#define TJ_LAYER_TEXT 2

/* Logical design resolution (16:9). Content scales to fit any framebuffer. */
#define TJ_REF_W 1280.0F
#define TJ_REF_H 720.0F

typedef struct nt_ui_context nt_ui_context_t;

typedef struct game_ctx {
    nt_ui_context_t *ui;
    nt_font_t font;
    nt_resource_t atlas;
    nt_material_t sprite_material; /* shared with the UI; used to draw the map world directly. */
    uint32_t white_region;
    uint32_t ground_sand_base;
    uint32_t decor_dune;
    uint32_t decor_stones;
    uint32_t decor_dry_grass;
    uint32_t decor_tracks;
    uint32_t decor_bones;
    uint32_t decor_cracks;
    uint32_t road_straight_ns;
    uint32_t road_straight_ew;
    uint32_t road_corner_ne;
    uint32_t road_corner_es;
    uint32_t road_corner_sw;
    uint32_t road_corner_wn;
    uint32_t road_entry_aul;
    uint32_t road_current_highlight;
    uint32_t buffer_edge_stones;
    uint32_t buffer_packed_sand;
    uint32_t buffer_stakes;
    uint32_t buffer_cart_marks;
    uint32_t aul_ground_2x2;
    uint32_t aul_yurt_small_01;
    uint32_t aul_yurt_small_02;
    uint32_t aul_fire_01;
    uint32_t aul_tamga_post_01;
    uint32_t aul_stage_01_camp;
    uint32_t aul_stage_02_settlement;
    uint32_t aul_stage_03_village;
    uint32_t aul_stage_04_fortified_aul;
    uint32_t aul_stage_05_steppe_capital;
    uint32_t tile_saxaul;
    uint32_t tile_oasis;
    uint32_t tile_yurt;
    uint32_t tile_tamga_stone;
    uint32_t tile_wolf_track;
    uint32_t tile_mirage;
    uint32_t tile_storm;
    uint32_t tile_last_tamga;
    uint32_t tile_well_01;
    uint32_t tile_watchtower_01;
    uint32_t tile_pack_01;
    uint32_t tile_small_camp_01;
    uint32_t tile_clan_camp_01;
    uint32_t tile_hunting_trail_01;
    uint32_t tile_vision_01;
    uint32_t tile_false_path_01;
    uint32_t tile_buried_spring_01;
    uint32_t hero_wayfarer_idle_s;
    uint32_t hero_wayfarer_walk_s;
    uint32_t hero_wayfarer_walk_e;
    uint32_t hero_wayfarer_walk_n;
    uint32_t hero_wayfarer_walk_w;
    uint32_t hero_wayfarer_panel;
    uint32_t hero_body_panel;
    uint32_t hero_mind_panel;
    uint32_t hero_spirit_panel;
    uint32_t ui_panel_felt_dark_96;
    uint32_t ui_panel_felt_light_96;
    uint32_t ui_card_playable_96x128;
    uint32_t ui_card_selected_96x128;
    uint32_t ui_slot_equipment_64;
    uint32_t ui_chip_resource_64;
    uint32_t ui_tooltip_dark_64;
    uint32_t ui_card_back_96x128;
    uint32_t ui_button_dark_64;
    uint32_t ui_finger_pointer_128; /* tutorial finger that points at the active control */
    uint32_t ui_valid_cell_overlay_128;
    uint32_t ui_invalid_cell_overlay_128;
    uint32_t ui_hover_cell_overlay_128;
    uint32_t card_badge_count_32;
    uint32_t card_placement_roadside_32;
    uint32_t card_placement_field_32;
    uint32_t card_placement_special_32;
    uint32_t card_art_saxaul_64;
    uint32_t card_art_yurt_64;
    uint32_t card_art_tamga_stone_64;
    uint32_t card_art_wolf_track_64;
    uint32_t card_art_oasis_64;
    uint32_t card_art_mirage_64;
    uint32_t card_art_storm_64;
    uint32_t card_art_last_tamga_64;
    uint32_t card_art_well_64;
    uint32_t card_art_watchtower_64;
    uint32_t equip_slot_weapon_01;
    uint32_t equip_slot_clothes_01;
    uint32_t equip_slot_tamga_01;
    uint32_t equip_slot_tool_01;
    uint32_t equip_weapon_staff_01;
    uint32_t equip_clothes_cloak_01;
    uint32_t equip_tamga_charm_01;
    uint32_t equip_tool_satchel_01;
    uint32_t icon_stamina_32;
    uint32_t icon_supplies_32;
    uint32_t icon_wisdom_32;
    uint32_t icon_glory_32;
    uint32_t icon_circle_32;
    uint32_t icon_day_32;
    uint32_t icon_body_32;
    uint32_t icon_mind_32;
    uint32_t icon_spirit_32;
    uint32_t icon_last_tamga_32;
    uint32_t icon_settings_32;
    uint32_t icon_speed_32;
    uint32_t icon_aul_upgrade_32;
    uint32_t icon_card_gain_32;
    uint32_t icon_deck_32;
    uint32_t icon_map_32;
    uint32_t icon_memory_32;
    uint32_t icon_warning_32;
    uint32_t fx_dust_step[4];
    uint32_t fx_tile_placed[4];
    uint32_t fx_tile_trigger[4];
    uint32_t fx_gain_popup[3];
    uint32_t fx_invalid_cell[2];
    float logical_w, logical_h;
    bool resources_ready;
    int score;
    int best;
    int chosen_heir;    /* archetype picked on the heir-select screen */
    const void *run;    /* &tj_run_t while in SCENE_GAME (else NULL); for the world sprite pass */
    float ptr_x, ptr_y; /* pointer in logical coords (set each frame); for map click->cell + hover */
    tj_shake_t shake;
    const scene_t *scene;
    const scene_t *prev; /* scene we transitioned from (lets Pause resume cleanly) */
    const scene_t *next; /* pending transition, applied at frame boundary */
} game_ctx_t;

/* Request a scene change; takes effect at the start of the next frame. */
void game_goto(game_ctx_t *g, const scene_t *next);

extern const scene_t SCENE_MENU;
extern const scene_t SCENE_HEIR_SELECT;
extern const scene_t SCENE_GAME;
extern const scene_t SCENE_VISUAL_QA;
extern const scene_t SCENE_SETTINGS;
extern const scene_t SCENE_GAMEOVER;
extern const scene_t SCENE_PAUSE;

#endif /* TJ_GAME_H */
