#ifndef TJ_CONFIG_H
#define TJ_CONFIG_H

/* Runtime balance config for "Кочевая петля". All gameplay numbers live in text
 * files under config/ (.ini + .tsv) authored by the GDD side; the core
 * hardcodes nothing from balance. See config/CONFIG.md for the contract. */

#include <stdbool.h>

#define TJ_MAX_TILES 32
#define TJ_MAX_HEIRS 8
#define TJ_MAX_SPAWNS 64
#define TJ_MAX_LOG_EVENTS 24
#define TJ_ID_LEN 24
#define TJ_NAME_LEN 48
#define TJ_LOG_TMPL_LEN 160

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
    int line, tier;       /* merge line (0 = none/enemy), tier (1..); 3 connected same line+tier -> tier+1 */
    tj_stat_t boost_stat; /* live combat stat a placed building adds (TJ_STAT_NONE = income only) */
    int boost_amount;     /* live boost per building */
} tj_tile_def_t;

/* Heir perk: a flat per-circle passive (sidegrade, not power creep). Balance-safe
 * and easy to extend; the stat spread (body/mind/spirit) is the main difference. */
typedef enum {
    TJ_PERK_NONE = 0,
    TJ_PERK_STAMINA_PER_CIRCLE, /* +value Силы each circle */
    TJ_PERK_SUPPLIES_PER_CIRCLE,
    TJ_PERK_WISDOM_PER_CIRCLE,
    TJ_PERK_GLORY_PER_CIRCLE,
} tj_perk_t;

typedef struct {
    char id[TJ_ID_LEN];
    char name[TJ_NAME_LEN];
    int body, mind, spirit, stamina_bonus;
    tj_perk_t perk;
    int perk_value;
} tj_heir_def_t;

/* Where a spawned object sits, and how its effect fires (Loop Hero is not only
 * road-adjacency: objects can be on the road, beside it, or passive/global). */
typedef enum {
    TJ_SPAWN_ROAD = 0, /* on a road cell */
    TJ_SPAWN_FIELD,    /* in a desert cell beside/around the road */
} tj_spawn_layer_t;

typedef enum {
    TJ_SCOPE_ON_ENTER = 0, /* fires when the hero steps on the cell (road) */
    TJ_SCOPE_ADJACENT,     /* fires when the hero passes the adjacent road cell */
    TJ_SCOPE_GLOBAL,       /* passive: applies once per circle, position-independent */
} tj_scope_t;

/* One per-circle spawn-pool row (spawns.tsv): place `count` of `tile` on `layer`
 * this circle, firing by `scope`. Pools differ per circle; counts do not scale. */
typedef struct {
    int circle;
    tj_spawn_layer_t layer;
    tj_scope_t scope;
    int tile_index; /* resolved from tile id at load (-1 = unknown, skipped) */
    int count;
} tj_spawn_t;

/* Event-log line template (log.tsv): event_id | tone | template. Placeholders
 * {tile}{stat}{hero}{supplies}{wisdom}{glory}{stamina}{circle}{diff} are filled
 * at push time. Lets the GDD own the run narrative without code edits. */
typedef struct {
    char id[TJ_ID_LEN];
    char tone[12];
    char tmpl[TJ_LOG_TMPL_LEN];
} tj_log_event_t;

typedef struct {
    int path_cells, laps_to_win, start_stamina;
    int pouch_start, pouch_per_circle; /* cards in the starting pouch, and added each circle */
    float move_seconds_per_cell;
    float aul_exit_seconds;   /* FTUE intro: hero walks out of the aul to the road */
    float road_entry_seconds; /* FTUE intro: hero stands on the first road cell */
    float storm_seconds;      /* sandstorm that veils the path reshuffle between circles */
    int start_in_game;        /* 1 = boot straight into the run, 0 = main menu */
    int path_cells_growth;    /* DEPRECATED (loop length now from map_* below) */
    int path_cells_jitter;    /* DEPRECATED (loop length now from map_* below) */
    int debug_random_desert;  /* 1 = auto-fill cells with random tiles (debug); 0 = empty (player places) */
    /* Map/loop geometry: aul reserved in the centre, a winding closed loop of
     * road cells around it, varied per circle via random outward bends. */
    int map_zone_cols, map_zone_rows; /* play-zone size in cells */
    int map_aul_w, map_aul_h;         /* central aul rect (cells) */
    int map_road_band;                /* road lives within this many cells of the aul (no-build band) */
    int map_bends_base;               /* random bends on circle 1 */
    int map_bends_per_circle;         /* extra bends per circle */
    int map_bends_jitter;             /* random 0..jitter extra bends */
    int check_base_difficulty, check_difficulty_per_circle, check_fail_stamina_loss, check_fail_reward_pct;
    /* Auto-combat derived params. Сила=body, Скорость=mind, Выносливость=spirit. */
    int combat_dmg_base;                                        /* hero hit damage = combat_dmg_base + Сила */
    float combat_atk_base;                                      /* hero attack interval (s) at 0 Скорость */
    float combat_spd_mul;                                       /* interval = combat_atk_base / (1 + Скорость*combat_spd_mul) */
    int combat_vit_hp;                                          /* max HP bonus = Выносливость*combat_vit_hp */
    int combat_vit_def;                                         /* flat defense = Выносливость*combat_vit_def */
    float combat_win_seconds;                                   /* victory celebration before loot is applied */
    float enemy_atk_interval;                                   /* enemy attack interval (s) */
    int enemy_hp_base, enemy_hp_per_diff;                       /* enemy HP = base + per_diff*tile_diff */
    int enemy_atk_base, enemy_atk_per_diff;                     /* enemy hit dmg = base + per_diff*tile_diff */
    int enemy_scale_per_circle_pct;                             /* +% per circle on enemy HP+atk (super-linear late ramp) */
    int elite_hp_pct, elite_atk_pct, boss_hp_pct, boss_atk_pct; /* enemy scaling for elite/boss cells */
    /* Dice events: roll 1=fail, max=pass; else effective = stat*(1+coeff*roll) vs DC. */
    int event_die;
    float event_dice_coeff;
    int event_dc_base, event_dc_per_circle;
    int event_pass_supplies, event_fail_hp;
    float event_reveal_seconds; /* animated dice reveal duration */
    /* Boss/elite archetypes: Толстяк(hp), Шустрый(fast+frail), Лютый(atk). */
    int arch_fat_hp_pct, arch_fast_interval_pct, arch_fast_hp_pct, arch_fierce_atk_pct;
    int rest_heal_pct; /* heal at the pre-boss rest cell, % of max HP */
    int tamga_wisdom_base, tamga_wisdom_per_circle, tamga_wisdom_slot_div, tamga_glory_div, tamga_max_active;
    int death_keep_supplies_pct, death_keep_wisdom_pct, death_keep_glory_pct;
    tj_tile_def_t tiles[TJ_MAX_TILES];
    int tile_count;
    tj_heir_def_t heirs[TJ_MAX_HEIRS];
    int heir_count;
    tj_spawn_t spawns[TJ_MAX_SPAWNS];
    int spawn_count;
    tj_log_event_t log_events[TJ_MAX_LOG_EVENTS];
    int log_event_count;
} tj_config_t;

extern tj_config_t g_config;

/* Fill defaults, then overlay balance.ini + heirs.tsv + tiles.tsv from dir.
 * Missing files keep defaults. Returns true if the directory was usable. */
bool tj_config_load(const char *dir);

int tj_config_tile_index(const char *id);                  /* -1 if absent */
int tj_config_tile_upgrade(int tile_index);                /* tile of same line, tier+1; -1 if top tier / non-merge */
const tj_log_event_t *tj_config_log_event(const char *id); /* NULL if absent */

#endif /* TJ_CONFIG_H */
