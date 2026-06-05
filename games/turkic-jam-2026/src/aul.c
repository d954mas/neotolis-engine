#include "aul.h"

#include "config.h"
#include "save.h"

tj_aul_t g_aul;

void tj_aul_load(void) {
    g_aul.supplies = save_get_int("aul_supplies", 0);
    g_aul.wisdom = save_get_int("aul_wisdom", 0);
    g_aul.glory = save_get_int("aul_glory", 0);
    g_aul.deaths = save_get_int("aul_deaths", 0);
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
