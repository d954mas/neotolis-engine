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
    g_config.move_seconds_per_cell = 0.6F;
    g_config.start_in_game = 1;
    g_config.path_cells_growth = 1;
    g_config.path_cells_jitter = 1;
    g_config.map_zone_cols = 6;
    g_config.map_zone_rows = 6;
    g_config.map_aul_w = 2;
    g_config.map_aul_h = 2;
    g_config.map_bends_base = 4;
    g_config.map_bends_per_circle = 1;
    g_config.map_bends_jitter = 2;
    g_config.check_base_difficulty = 1;
    g_config.check_difficulty_per_circle = 1;
    g_config.check_fail_stamina_loss = 2;
    g_config.check_fail_reward_pct = 50;
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
        {"move_seconds_per_cell", NULL, &g_config.move_seconds_per_cell},
        {"start_in_game", &g_config.start_in_game, NULL},
        {"path_cells_growth", &g_config.path_cells_growth, NULL},
        {"path_cells_jitter", &g_config.path_cells_jitter, NULL},
        {"debug_random_desert", &g_config.debug_random_desert, NULL},
        {"map_zone_cols", &g_config.map_zone_cols, NULL},
        {"map_zone_rows", &g_config.map_zone_rows, NULL},
        {"map_aul_w", &g_config.map_aul_w, NULL},
        {"map_aul_h", &g_config.map_aul_h, NULL},
        {"map_bends_base", &g_config.map_bends_base, NULL},
        {"map_bends_per_circle", &g_config.map_bends_per_circle, NULL},
        {"map_bends_jitter", &g_config.map_bends_jitter, NULL},
        {"check_base_difficulty", &g_config.check_base_difficulty, NULL},
        {"check_difficulty_per_circle", &g_config.check_difficulty_per_circle, NULL},
        {"check_fail_stamina_loss", &g_config.check_fail_stamina_loss, NULL},
        {"check_fail_reward_pct", &g_config.check_fail_reward_pct, NULL},
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
    return true;
}

int tj_config_tile_index(const char *id) {
    for (int i = 0; i < g_config.tile_count; i++) {
        if (strcmp(g_config.tiles[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}
