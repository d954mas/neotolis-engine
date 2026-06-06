#include "aul.h"

#include <stddef.h>

#include "config.h"
#include "save.h"

tj_aul_t g_aul;

void tj_aul_load(void) {
    g_aul.supplies = save_get_int("aul_supplies", 0);
    g_aul.wisdom = save_get_int("aul_wisdom", 0);
    g_aul.glory = save_get_int("aul_glory", 0);
    g_aul.deaths = save_get_int("aul_deaths", 0);
    g_aul.up_force = save_get_int("aul_up_force", 0);
    g_aul.up_speed = save_get_int("aul_up_speed", 0);
    g_aul.up_vigor = save_get_int("aul_up_vigor", 0);
    g_aul.up_keep = save_get_int("aul_up_keep", 0);
    g_aul.tamga_pending = save_get_int("tamga_pending", 0);
    g_aul.tamga_cell = save_get_int("tamga_cell", 0);
    g_aul.tamga_wisdom = save_get_int("tamga_wisdom", 0);
    g_aul.tamga_glory = save_get_int("tamga_glory", 0);
}

void tj_tamga_spawn(int cell, int circle) {
    int wisdom = g_config.tamga_wisdom_base + (g_config.tamga_wisdom_per_circle * circle);
    if (g_config.tamga_wisdom_slot_div > 0) {
        wisdom += cell / g_config.tamga_wisdom_slot_div;
    }
    const int glory = (g_config.tamga_glory_div > 0) ? (wisdom / g_config.tamga_glory_div) : 0;
    g_aul.tamga_pending = 1;
    g_aul.tamga_cell = cell;
    g_aul.tamga_wisdom = wisdom;
    g_aul.tamga_glory = glory;
    save_set_int("tamga_pending", 1);
    save_set_int("tamga_cell", cell);
    save_set_int("tamga_wisdom", wisdom);
    save_set_int("tamga_glory", glory);
    save_flush();
}

void tj_tamga_clear(void) {
    g_aul.tamga_pending = 0;
    save_set_int("tamga_pending", 0);
    save_flush();
}

static int *aul_track(int track) {
    switch (track) {
    case 0:
        return &g_aul.up_force;
    case 1:
        return &g_aul.up_speed;
    case 2:
        return &g_aul.up_vigor;
    case 3:
        return &g_aul.up_keep;
    default:
        return NULL;
    }
}

static const char *aul_track_key(int track) {
    switch (track) {
    case 0:
        return "aul_up_force";
    case 1:
        return "aul_up_speed";
    case 2:
        return "aul_up_vigor";
    case 3:
        return "aul_up_keep";
    default:
        return NULL;
    }
}

int tj_aul_upgrade_cost(int track) {
    const int *p = aul_track(track);
    return p ? (8 * (*p + 1)) : 0; /* cost rises each level */
}

bool tj_aul_upgrade(int track) {
    int *p = aul_track(track);
    if (!p) {
        return false;
    }
    const int cost = tj_aul_upgrade_cost(track);
    if (g_aul.supplies < cost) {
        return false;
    }
    g_aul.supplies -= cost;
    (*p)++;
    save_set_int("aul_supplies", g_aul.supplies);
    save_set_int(aul_track_key(track), *p);
    save_flush();
    return true;
}

void tj_aul_add_from_run(int run_supplies, int run_wisdom, int run_glory) {
    int keep = g_config.death_keep_supplies_pct + (g_aul.up_keep * 10); /* Наследие: +10%/lvl */
    if (keep > 100) {
        keep = 100;
    }
    g_aul.supplies += run_supplies * keep / 100;
    g_aul.wisdom += run_wisdom * g_config.death_keep_wisdom_pct / 100;
    g_aul.glory += run_glory * g_config.death_keep_glory_pct / 100;
    g_aul.deaths += 1;
    save_set_int("aul_supplies", g_aul.supplies);
    save_set_int("aul_wisdom", g_aul.wisdom);
    save_set_int("aul_glory", g_aul.glory);
    save_set_int("aul_deaths", g_aul.deaths);
    save_flush();
}
