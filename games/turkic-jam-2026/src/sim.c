#include "sim.h"

#include <stdio.h>
#include <string.h>

int tj_hero_stat(const tj_run_t *r, tj_stat_t s) {
    switch (s) {
    case TJ_STAT_BODY:
        return r->body;
    case TJ_STAT_MIND:
        return r->mind;
    case TJ_STAT_SPIRIT:
        return r->spirit;
    default:
        return 0;
    }
}

void tj_run_place_tile(tj_run_t *r, int cell, int tile_index) {
    if (cell >= 0 && cell < TJ_MAX_PATH) {
        r->tile_at[cell] = tile_index;
    }
}

void tj_run_start(tj_run_t *r, int heir_index) {
    memset(r, 0, sizeof *r);
    for (int i = 0; i < TJ_MAX_PATH; i++) {
        r->tile_at[i] = -1;
    }
    if (heir_index < 0 || heir_index >= g_config.heir_count) {
        heir_index = 0;
    }
    r->heir_index = heir_index;
    r->stamina = g_config.start_stamina;
    if (g_config.heir_count > 0) {
        const tj_heir_def_t *h = &g_config.heirs[heir_index];
        r->body = h->body;
        r->mind = h->mind;
        r->spirit = h->spirit;
        r->stamina += h->stamina_bonus;
    }
    r->circle = 1;
    r->alive = true;
    (void)snprintf(r->last_event, sizeof r->last_event, "%s", "Выход из аула");
}

static void resolve_cell(tj_run_t *r) {
    int idx = (r->cell >= 0 && r->cell < TJ_MAX_PATH) ? r->tile_at[r->cell] : -1;
    if (idx < 0 || idx >= g_config.tile_count) {
        (void)snprintf(r->last_event, sizeof r->last_event, "%s", "Пустая клетка");
        return;
    }
    const tj_tile_def_t *t = &g_config.tiles[idx];
    int supplies = t->supplies;
    int wisdom = t->wisdom;
    int glory = t->glory;
    bool failed = false;

    if (t->kind == TJ_TILE_CHECK && t->check != TJ_STAT_NONE) {
        int diff = g_config.check_base_difficulty + (g_config.check_difficulty_per_circle * r->circle);
        if (tj_hero_stat(r, t->check) < diff) {
            failed = true;
            supplies = supplies * g_config.check_fail_reward_pct / 100;
            wisdom = wisdom * g_config.check_fail_reward_pct / 100;
            glory = glory * g_config.check_fail_reward_pct / 100;
            r->stamina -= g_config.check_fail_stamina_loss;
        }
    }

    r->supplies += supplies;
    r->wisdom += wisdom;
    r->glory += glory;
    r->stamina -= t->stamina_cost;
    r->stamina += t->stamina_restore;

    if (r->stamina <= 0) {
        r->stamina = 0;
        r->alive = false;
        (void)snprintf(r->last_event, sizeof r->last_event, "%s — Силы иссякли", t->name);
        return;
    }
    (void)snprintf(r->last_event, sizeof r->last_event, "%s%s", t->name, failed ? " (провал)" : "");
}

void tj_run_tick(tj_run_t *r, float dt) {
    if (!r->alive || r->won) {
        return;
    }
    float per = g_config.move_seconds_per_cell;
    if (per <= 0.0F) {
        per = 0.5F;
    }
    r->move_t += dt;
    int guard = 0;
    while (r->move_t >= per && r->alive && !r->won && guard < TJ_MAX_PATH) {
        r->move_t -= per;
        guard++;
        r->cell++;
        if (r->cell >= g_config.path_cells) {
            r->cell = 0;
            r->circle++;
            if (r->circle > g_config.laps_to_win) {
                r->won = true;
                (void)snprintf(r->last_event, sizeof r->last_event, "%s", "Кольцо разорвано!");
                return;
            }
        }
        resolve_cell(r);
    }
}
