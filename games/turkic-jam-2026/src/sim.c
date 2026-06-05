#include "sim.h"

#include <stdio.h>
#include <string.h>

#include "journal.h"
#include "rng.h"

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

/* Per-circle, per-heir variety: deterministic seed, growing length + a fresh
 * tile layout each circle (player-placed tiles will replace the auto layout). */
static void roll_circle(tj_run_t *r) {
    rng_seed((uint32_t)((r->heir_index * 7919) + (r->circle * 104729) + 1));
    int len = g_config.path_cells + (g_config.path_cells_growth * (r->circle - 1));
    if (r->circle > 1 && g_config.path_cells_jitter > 0) {
        len += rng_range_int(0, g_config.path_cells_jitter);
    }
    if (len < 3) {
        len = 3;
    }
    if (len > TJ_MAX_PATH) {
        len = TJ_MAX_PATH;
    }
    r->path_cells = len;
    for (int i = 0; i < TJ_MAX_PATH; i++) {
        r->tile_at[i] = (i < len && g_config.tile_count > 0) ? rng_range_int(0, g_config.tile_count - 1) : -1;
    }
}

void tj_run_start(tj_run_t *r, int heir_index) {
    memset(r, 0, sizeof *r);
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
    roll_circle(r);
    (void)snprintf(r->last_event, sizeof r->last_event, "%s", "Выход из аула");
    tj_journal_clear();
    tj_journal_push(TJ_LOG_BIG, "Новый наследник выходит из аула.");
}

static const char *stat_name(tj_stat_t s) {
    switch (s) {
    case TJ_STAT_BODY:
        return "Тело";
    case TJ_STAT_MIND:
        return "Ум";
    case TJ_STAT_SPIRIT:
        return "Дух";
    default:
        return "";
    }
}

static void resolve_cell(tj_run_t *r) {
    int idx = (r->cell >= 0 && r->cell < TJ_MAX_PATH) ? r->tile_at[r->cell] : -1;
    if (idx < 0 || idx >= g_config.tile_count) {
        tj_journal_push(TJ_LOG_PLAIN, "Пустая клетка");
        return;
    }
    const tj_tile_def_t *t = &g_config.tiles[idx];
    int supplies = t->supplies;
    int wisdom = t->wisdom;
    int glory = t->glory;
    int stam = t->stamina_restore - t->stamina_cost;
    bool failed = false;
    char check[56] = "";

    if (t->kind == TJ_TILE_CHECK && t->check != TJ_STAT_NONE) {
        int diff = g_config.check_base_difficulty + (g_config.check_difficulty_per_circle * r->circle);
        int stat = tj_hero_stat(r, t->check);
        if (stat >= diff) {
            (void)snprintf(check, sizeof check, "  (%s %d>=%d, успех)", stat_name(t->check), stat, diff);
        } else {
            failed = true;
            supplies = supplies * g_config.check_fail_reward_pct / 100;
            wisdom = wisdom * g_config.check_fail_reward_pct / 100;
            glory = glory * g_config.check_fail_reward_pct / 100;
            stam -= g_config.check_fail_stamina_loss;
            (void)snprintf(check, sizeof check, "  (%s %d<%d, провал)", stat_name(t->check), stat, diff);
        }
    }

    r->supplies += supplies;
    r->wisdom += wisdom;
    r->glory += glory;
    r->stamina += stam;

    tj_log_kind_t kind = TJ_LOG_PLAIN;
    if (failed) {
        kind = TJ_LOG_BAD;
    } else if (supplies || wisdom || glory || t->stamina_restore) {
        kind = TJ_LOG_GOOD;
    }
    tj_journal_push(kind, "%s%s  [з%+d м%+d с%+d Силы%+d]", t->name, check, supplies, wisdom, glory, stam);

    if (r->stamina <= 0) {
        r->stamina = 0;
        r->alive = false;
        tj_journal_push(TJ_LOG_BIG, "Силы иссякли. Путь окончен.");
    }
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
        if (r->cell >= r->path_cells) {
            r->cell = 0;
            r->circle++;
            if (r->circle > g_config.laps_to_win) {
                r->won = true;
                tj_journal_push(TJ_LOG_BIG, "Кольцо разорвано! Род свободен.");
                return;
            }
            tj_journal_push(TJ_LOG_BIG, "Круг %d пройден.", r->circle - 1);
            roll_circle(r);
        }
        resolve_cell(r);
    }
}
