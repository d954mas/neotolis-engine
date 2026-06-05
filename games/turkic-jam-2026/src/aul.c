#include "aul.h"

#include "config.h"
#include "save.h"

tj_aul_t g_aul;

void tj_aul_load(void) {
    g_aul.supplies = save_get_int("aul_supplies", 0);
    g_aul.wisdom = save_get_int("aul_wisdom", 0);
    g_aul.glory = save_get_int("aul_glory", 0);
    g_aul.deaths = save_get_int("aul_deaths", 0);
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

void tj_aul_add_from_run(int run_supplies, int run_wisdom, int run_glory) {
    g_aul.supplies += run_supplies * g_config.death_keep_supplies_pct / 100;
    g_aul.wisdom += run_wisdom * g_config.death_keep_wisdom_pct / 100;
    g_aul.glory += run_glory * g_config.death_keep_glory_pct / 100;
    g_aul.deaths += 1;
    save_set_int("aul_supplies", g_aul.supplies);
    save_set_int("aul_wisdom", g_aul.wisdom);
    save_set_int("aul_glory", g_aul.glory);
    save_set_int("aul_deaths", g_aul.deaths);
    save_flush();
}
