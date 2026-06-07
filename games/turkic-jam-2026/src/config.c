#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

tj_config_t g_config;

// #region text helpers
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
    return s;
}

static int split_pipe(char *line, char **out, int max) {
    int n = 0;
    out[n++] = line;
    for (char *p = line; *p; p++) {
        if (*p == '|' && n < max) {
            *p = '\0';
            out[n++] = p + 1;
        }
    }
    for (int i = 0; i < n; i++) {
        out[i] = trim(out[i]);
    }
    return n;
}

static int to_int(const char *s) { return (int)strtol(s, NULL, 10); }

static tj_tile_kind_t kind_from(const char *s) {
    if (strcmp(s, "check") == 0) {
        return TJ_TILE_CHECK;
    }
    if (strcmp(s, "support") == 0) {
        return TJ_TILE_SUPPORT;
    }
    return TJ_TILE_SAFE;
}

static tj_stat_t stat_from(const char *s) {
    if (strcmp(s, "body") == 0) {
        return TJ_STAT_BODY;
    }
    if (strcmp(s, "mind") == 0) {
        return TJ_STAT_MIND;
    }
    if (strcmp(s, "spirit") == 0) {
        return TJ_STAT_SPIRIT;
    }
    return TJ_STAT_NONE;
}

static tj_placement_t placement_from(const char *s) {
    if (strcmp(s, "road") == 0) {
        return TJ_PLACE_ROAD;
    }
    if (strcmp(s, "field") == 0) {
        return TJ_PLACE_FIELD;
    }
    return TJ_PLACE_ROADSIDE;
}

static tj_perk_t perk_from(const char *s) {
    if (strcmp(s, "stamina_per_circle") == 0) {
        return TJ_PERK_STAMINA_PER_CIRCLE;
    }
    if (strcmp(s, "supplies_per_circle") == 0) {
        return TJ_PERK_SUPPLIES_PER_CIRCLE;
    }
    if (strcmp(s, "wisdom_per_circle") == 0) {
        return TJ_PERK_WISDOM_PER_CIRCLE;
    }
    if (strcmp(s, "glory_per_circle") == 0) {
        return TJ_PERK_GLORY_PER_CIRCLE;
    }
    return TJ_PERK_NONE;
}

static tj_spawn_layer_t layer_from(const char *s) {
    if (strcmp(s, "field") == 0) {
        return TJ_SPAWN_FIELD;
    }
    return TJ_SPAWN_ROAD;
}

static tj_scope_t scope_from(const char *s, tj_spawn_layer_t layer) {
    if (strcmp(s, "on_enter") == 0) {
        return TJ_SCOPE_ON_ENTER;
    }
    if (strcmp(s, "adjacent") == 0) {
        return TJ_SCOPE_ADJACENT;
    }
    if (strcmp(s, "global") == 0) {
        return TJ_SCOPE_GLOBAL;
    }
    /* Default by layer: road events fire on entry, field objects by adjacency. */
    return (layer == TJ_SPAWN_FIELD) ? TJ_SCOPE_ADJACENT : TJ_SCOPE_ON_ENTER;
}
// #endregion

// #region defaults
static void set_defaults(void) {
    g_config = (tj_config_t){0};
    g_config.path_cells = 12;
    g_config.laps_to_win = 10;
    g_config.start_stamina = 10;
    g_config.pouch_start = 3;
    g_config.pouch_per_circle = 3;
    g_config.move_seconds_per_cell = 0.6F;
    g_config.aul_exit_seconds = 2.0F;
    g_config.road_entry_seconds = 0.7F;
    g_config.storm_seconds = 1.3F;
    g_config.start_in_game = 1;
    g_config.path_cells_growth = 1;
    g_config.path_cells_jitter = 1;
    g_config.map_zone_cols = 18;
    g_config.map_zone_rows = 18;
    g_config.map_aul_w = 2;
    g_config.map_aul_h = 2;
    g_config.map_road_band = 2;
    g_config.map_bends_base = 4;
    g_config.map_bends_per_circle = 1;
    g_config.map_bends_jitter = 2;
    g_config.check_base_difficulty = 1;
    g_config.check_difficulty_per_circle = 1;
    g_config.check_fail_stamina_loss = 2;
    g_config.check_fail_reward_pct = 50;
    g_config.combat_dmg_base = 1;
    g_config.combat_atk_base = 0.55F;
    g_config.combat_spd_mul = 0.35F;
    g_config.combat_vit_hp = 2;
    g_config.combat_vit_def = 1;
    g_config.combat_win_seconds = 1.2F;
    g_config.enemy_atk_interval = 0.85F;
    g_config.enemy_hp_base = 3;
    g_config.enemy_hp_per_diff = 2;
    g_config.enemy_atk_base = 1;
    g_config.enemy_atk_per_diff = 1;
    g_config.enemy_scale_per_circle_pct = 12;
    g_config.elite_hp_pct = 180;
    g_config.elite_atk_pct = 140;
    g_config.boss_hp_pct = 300;
    g_config.boss_atk_pct = 180;
    g_config.event_die = 10;
    g_config.event_dice_coeff = 0.25F;
    g_config.event_dc_base = 4;
    g_config.event_dc_per_circle = 2;
    g_config.event_pass_supplies = 3;
    g_config.event_fail_hp = 2;
    g_config.event_reveal_seconds = 1.5F;
    g_config.arch_fat_hp_pct = 200;
    g_config.arch_fast_interval_pct = 50;
    g_config.arch_fast_hp_pct = 70;
    g_config.arch_fierce_atk_pct = 200;
    g_config.rest_heal_pct = 25;
    g_config.tamga_wisdom_base = 3;
    g_config.tamga_wisdom_per_circle = 3;
    g_config.tamga_wisdom_slot_div = 3;
    g_config.tamga_glory_div = 3;
    g_config.tamga_max_active = 3;
    g_config.death_keep_supplies_pct = 50;
    g_config.death_keep_wisdom_pct = 100;
    g_config.death_keep_glory_pct = 50;
}
// #endregion

// #region balance.ini
typedef struct {
    const char *key;
    int *ip;
    float *fp;
} ini_field_t;

static void apply_ini(const char *k, const char *v) {
    const ini_field_t fields[] = {
        {"path_cells", &g_config.path_cells, NULL},
        {"laps_to_win", &g_config.laps_to_win, NULL},
        {"start_stamina", &g_config.start_stamina, NULL},
        {"pouch_start", &g_config.pouch_start, NULL},
        {"pouch_per_circle", &g_config.pouch_per_circle, NULL},
        {"move_seconds_per_cell", NULL, &g_config.move_seconds_per_cell},
        {"aul_exit_seconds", NULL, &g_config.aul_exit_seconds},
        {"road_entry_seconds", NULL, &g_config.road_entry_seconds},
        {"storm_seconds", NULL, &g_config.storm_seconds},
        {"start_in_game", &g_config.start_in_game, NULL},
        {"path_cells_growth", &g_config.path_cells_growth, NULL},
        {"path_cells_jitter", &g_config.path_cells_jitter, NULL},
        {"debug_random_desert", &g_config.debug_random_desert, NULL},
        {"map_zone_cols", &g_config.map_zone_cols, NULL},
        {"map_zone_rows", &g_config.map_zone_rows, NULL},
        {"map_aul_w", &g_config.map_aul_w, NULL},
        {"map_aul_h", &g_config.map_aul_h, NULL},
        {"map_road_band", &g_config.map_road_band, NULL},
        {"map_bends_base", &g_config.map_bends_base, NULL},
        {"map_bends_per_circle", &g_config.map_bends_per_circle, NULL},
        {"map_bends_jitter", &g_config.map_bends_jitter, NULL},
        {"check_base_difficulty", &g_config.check_base_difficulty, NULL},
        {"check_difficulty_per_circle", &g_config.check_difficulty_per_circle, NULL},
        {"check_fail_stamina_loss", &g_config.check_fail_stamina_loss, NULL},
        {"check_fail_reward_pct", &g_config.check_fail_reward_pct, NULL},
        {"combat_dmg_base", &g_config.combat_dmg_base, NULL},
        {"combat_atk_base", NULL, &g_config.combat_atk_base},
        {"combat_spd_mul", NULL, &g_config.combat_spd_mul},
        {"combat_vit_hp", &g_config.combat_vit_hp, NULL},
        {"combat_vit_def", &g_config.combat_vit_def, NULL},
        {"combat_win_seconds", NULL, &g_config.combat_win_seconds},
        {"enemy_atk_interval", NULL, &g_config.enemy_atk_interval},
        {"enemy_hp_base", &g_config.enemy_hp_base, NULL},
        {"enemy_hp_per_diff", &g_config.enemy_hp_per_diff, NULL},
        {"enemy_atk_base", &g_config.enemy_atk_base, NULL},
        {"enemy_atk_per_diff", &g_config.enemy_atk_per_diff, NULL},
        {"enemy_scale_per_circle_pct", &g_config.enemy_scale_per_circle_pct, NULL},
        {"elite_hp_pct", &g_config.elite_hp_pct, NULL},
        {"elite_atk_pct", &g_config.elite_atk_pct, NULL},
        {"boss_hp_pct", &g_config.boss_hp_pct, NULL},
        {"boss_atk_pct", &g_config.boss_atk_pct, NULL},
        {"event_die", &g_config.event_die, NULL},
        {"event_dice_coeff", NULL, &g_config.event_dice_coeff},
        {"event_dc_base", &g_config.event_dc_base, NULL},
        {"event_dc_per_circle", &g_config.event_dc_per_circle, NULL},
        {"event_pass_supplies", &g_config.event_pass_supplies, NULL},
        {"event_fail_hp", &g_config.event_fail_hp, NULL},
        {"event_reveal_seconds", NULL, &g_config.event_reveal_seconds},
        {"arch_fat_hp_pct", &g_config.arch_fat_hp_pct, NULL},
        {"arch_fast_interval_pct", &g_config.arch_fast_interval_pct, NULL},
        {"arch_fast_hp_pct", &g_config.arch_fast_hp_pct, NULL},
        {"arch_fierce_atk_pct", &g_config.arch_fierce_atk_pct, NULL},
        {"rest_heal_pct", &g_config.rest_heal_pct, NULL},
        {"tamga_wisdom_base", &g_config.tamga_wisdom_base, NULL},
        {"tamga_wisdom_per_circle", &g_config.tamga_wisdom_per_circle, NULL},
        {"tamga_wisdom_slot_div", &g_config.tamga_wisdom_slot_div, NULL},
        {"tamga_glory_div", &g_config.tamga_glory_div, NULL},
        {"tamga_max_active", &g_config.tamga_max_active, NULL},
        {"death_keep_supplies_pct", &g_config.death_keep_supplies_pct, NULL},
        {"death_keep_wisdom_pct", &g_config.death_keep_wisdom_pct, NULL},
        {"death_keep_glory_pct", &g_config.death_keep_glory_pct, NULL},
    };
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        if (strcmp(k, fields[i].key) == 0) {
            if (fields[i].ip) {
                *fields[i].ip = to_int(v);
            } else {
                *fields[i].fp = (float)strtod(v, NULL);
            }
            return;
        }
    }
}

static void parse_ini(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *t = trim(line);
        if (*t == '\0' || *t == '#') {
            continue;
        }
        char *eq = strchr(t, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        apply_ini(trim(t), trim(eq + 1));
    }
    (void)fclose(f);
}
// #endregion

// #region tables
static void parse_heirs(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *t = trim(line);
        if (*t == '\0' || *t == '#') {
            continue;
        }
        char *fld[12];
        int n = split_pipe(t, fld, 12);
        if (n < 6 || g_config.heir_count >= TJ_MAX_HEIRS) {
            continue;
        }
        tj_heir_def_t *d = &g_config.heirs[g_config.heir_count++];
        (void)snprintf(d->id, sizeof d->id, "%s", fld[0]);
        (void)snprintf(d->name, sizeof d->name, "%s", fld[1]);
        d->body = to_int(fld[2]);
        d->mind = to_int(fld[3]);
        d->spirit = to_int(fld[4]);
        d->stamina_bonus = to_int(fld[5]);
        d->perk = (n >= 7) ? perk_from(fld[6]) : TJ_PERK_NONE;
        d->perk_value = (n >= 8) ? to_int(fld[7]) : 0;
    }
    (void)fclose(f);
}

static void parse_tiles(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *t = trim(line);
        if (*t == '\0' || *t == '#') {
            continue;
        }
        char *fld[16];
        int n = split_pipe(t, fld, 16);
        if (n < 11 || g_config.tile_count >= TJ_MAX_TILES) {
            continue;
        }
        tj_tile_def_t *d = &g_config.tiles[g_config.tile_count++];
        (void)snprintf(d->id, sizeof d->id, "%s", fld[0]);
        (void)snprintf(d->name, sizeof d->name, "%s", fld[1]);
        d->kind = kind_from(fld[2]);
        d->check = stat_from(fld[3]);
        d->diff_base = to_int(fld[4]);
        d->diff_per_circle = to_int(fld[5]);
        d->supplies = to_int(fld[6]);
        d->wisdom = to_int(fld[7]);
        d->glory = to_int(fld[8]);
        d->stamina_cost = to_int(fld[9]);
        d->stamina_restore = to_int(fld[10]);
        d->placement = (n >= 12) ? placement_from(fld[11]) : TJ_PLACE_ROADSIDE;
        d->line = (n >= 13) ? to_int(fld[12]) : 0;
        d->tier = (n >= 14) ? to_int(fld[13]) : 0;
        d->boost_stat = (n >= 15) ? stat_from(fld[14]) : TJ_STAT_NONE;
        d->boost_amount = (n >= 16) ? to_int(fld[15]) : 0;
    }
    (void)fclose(f);
}

/* spawns.tsv: per-circle pool. circle | layer | tile_id | count | scope(optional) */
static void parse_spawns(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *t = trim(line);
        if (*t == '\0' || *t == '#') {
            continue;
        }
        char *fld[8];
        int n = split_pipe(t, fld, 8);
        if (n < 4 || g_config.spawn_count >= TJ_MAX_SPAWNS) {
            continue;
        }
        tj_spawn_t *s = &g_config.spawns[g_config.spawn_count++];
        s->circle = to_int(fld[0]);
        s->layer = layer_from(fld[1]);
        s->tile_index = tj_config_tile_index(fld[2]);
        s->count = to_int(fld[3]);
        s->scope = (n >= 5) ? scope_from(fld[4], s->layer) : scope_from("", s->layer);
    }
    (void)fclose(f);
}
// #endregion

/* log.tsv: event_id | tone | template (template may contain spaces/{placeholders}). */
static void parse_log_events(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *t = trim(line);
        if (*t == '\0' || *t == '#') {
            continue;
        }
        char *fld[4];
        int n = split_pipe(t, fld, 4);
        if (n < 3 || g_config.log_event_count >= TJ_MAX_LOG_EVENTS) {
            continue;
        }
        tj_log_event_t *e = &g_config.log_events[g_config.log_event_count++];
        (void)snprintf(e->id, sizeof e->id, "%s", fld[0]);
        (void)snprintf(e->tone, sizeof e->tone, "%s", fld[1]);
        (void)snprintf(e->tmpl, sizeof e->tmpl, "%s", fld[2]);
    }
    (void)fclose(f);
}

bool tj_config_load(const char *dir) {
    set_defaults();
    char path[512];
    (void)snprintf(path, sizeof path, "%s/balance.ini", dir);
    parse_ini(path);
    (void)snprintf(path, sizeof path, "%s/heirs.tsv", dir);
    parse_heirs(path);
    (void)snprintf(path, sizeof path, "%s/tiles.tsv", dir);
    parse_tiles(path);
    (void)snprintf(path, sizeof path, "%s/spawns.tsv", dir);
    parse_spawns(path); /* after tiles: spawn rows resolve tile ids to indices */
    (void)snprintf(path, sizeof path, "%s/log.tsv", dir);
    parse_log_events(path);
    return true;
}

const tj_log_event_t *tj_config_log_event(const char *id) {
    for (int i = 0; i < g_config.log_event_count; i++) {
        if (strcmp(g_config.log_events[i].id, id) == 0) {
            return &g_config.log_events[i];
        }
    }
    return NULL;
}

int tj_config_tile_index(const char *id) {
    for (int i = 0; i < g_config.tile_count; i++) {
        if (strcmp(g_config.tiles[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}
