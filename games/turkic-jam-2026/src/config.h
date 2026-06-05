#ifndef TJ_CONFIG_H
#define TJ_CONFIG_H

/* Runtime balance config for "Песнь Тамги". All gameplay numbers live in text
 * files under config/ (.ini + .tsv) authored by the GDD side; the core
 * hardcodes nothing from balance. See config/CONFIG.md for the contract. */

#include <stdbool.h>

#define TJ_MAX_TILES 32
#define TJ_MAX_HEIRS 8
#define TJ_ID_LEN 24
#define TJ_NAME_LEN 48

typedef enum {
    TJ_TILE_SAFE = 0,
    TJ_TILE_SUPPORT,
    TJ_TILE_CHECK,
} tj_tile_kind_t;

typedef enum {
    TJ_STAT_NONE = 0,
    TJ_STAT_BODY,
    TJ_STAT_MIND,
    TJ_STAT_SPIRIT,
} tj_stat_t;

/* Where a tile/card may be placed (Loop Hero model). MVP: all player cards are
 * roadside; road = scripted/debug only; field reserved for post-MVP. */
typedef enum {
    TJ_PLACE_ROADSIDE = 0,
    TJ_PLACE_ROAD,
    TJ_PLACE_FIELD,
} tj_placement_t;

typedef struct {
    char id[TJ_ID_LEN];
    char name[TJ_NAME_LEN];
    tj_tile_kind_t kind;
    tj_stat_t check;
    tj_placement_t placement;
    int diff_base, diff_per_circle;
    int supplies, wisdom, glory;
    int stamina_cost, stamina_restore;
} tj_tile_def_t;

typedef struct {
    char id[TJ_ID_LEN];
    char name[TJ_NAME_LEN];
    int body, mind, spirit, stamina_bonus;
} tj_heir_def_t;

typedef struct {
    int path_cells, laps_to_win, start_stamina;
    float move_seconds_per_cell;
    int start_in_game;       /* 1 = boot straight into the run, 0 = main menu */
    int path_cells_growth;   /* DEPRECATED (loop length now from map_* below) */
    int path_cells_jitter;   /* DEPRECATED (loop length now from map_* below) */
    int debug_random_desert; /* 1 = auto-fill cells with random tiles (debug); 0 = empty (player places) */
    /* Map/loop geometry: aul reserved in the centre, a winding closed loop of
     * road cells around it, varied per circle via random outward bends. */
    int map_zone_cols, map_zone_rows; /* play-zone size in cells */
    int map_aul_w, map_aul_h;         /* central aul rect (cells) */
    int map_bends_base;               /* random bends on circle 1 */
    int map_bends_per_circle;         /* extra bends per circle */
    int map_bends_jitter;             /* random 0..jitter extra bends */
    int check_base_difficulty, check_difficulty_per_circle, check_fail_stamina_loss, check_fail_reward_pct;
    int tamga_wisdom_base, tamga_wisdom_per_circle, tamga_wisdom_slot_div, tamga_glory_div, tamga_max_active;
    int death_keep_supplies_pct, death_keep_wisdom_pct, death_keep_glory_pct;
    tj_tile_def_t tiles[TJ_MAX_TILES];
    int tile_count;
    tj_heir_def_t heirs[TJ_MAX_HEIRS];
    int heir_count;
} tj_config_t;

extern tj_config_t g_config;

/* Fill defaults, then overlay balance.ini + heirs.tsv + tiles.tsv from dir.
 * Missing files keep defaults. Returns true if the directory was usable. */
bool tj_config_load(const char *dir);

int tj_config_tile_index(const char *id); /* -1 if absent */

#endif /* TJ_CONFIG_H */
