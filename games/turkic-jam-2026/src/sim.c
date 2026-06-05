#include "sim.h"

#include <stdio.h>
#include <string.h>

#include "aul.h"
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

// #region loop generation (winding closed loop around the central aul)
#define TJ_ZONE_MAX 12
enum { OCC_EMPTY = 0, OCC_AUL = 1, OCC_ROAD = 2, OCC_SLOT = 3, OCC_GLOBAL = 4 };

typedef struct {
    int cols, rows;
    uint8_t occ[TJ_ZONE_MAX * TJ_ZONE_MAX];
} zone_t;

static int clampi(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}
static int sign_away(float v, float c) {
    if (v > c) {
        return 1;
    }
    if (v < c) {
        return -1;
    }
    return 0;
}
static float dist2(float px, float py, float cx, float cy) {
    const float dx = px - cx;
    const float dy = py - cy;
    return (dx * dx) + (dy * dy);
}
static bool zin(const zone_t *z, int x, int y) { return x >= 0 && x < z->cols && y >= 0 && y < z->rows; }
static uint8_t *zocc(zone_t *z, int x, int y) { return &z->occ[(y * z->cols) + x]; }

/* Clockwise perimeter of rect [x0..x1]x[y0..y1] into the run's path arrays. */
static int rect_loop(tj_run_t *r, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int x = x0; x < x1; x++) {
        r->path_gx[n] = (uint8_t)x;
        r->path_gy[n] = (uint8_t)y0;
        n++;
    }
    for (int y = y0; y < y1; y++) {
        r->path_gx[n] = (uint8_t)x1;
        r->path_gy[n] = (uint8_t)y;
        n++;
    }
    for (int x = x1; x > x0; x--) {
        r->path_gx[n] = (uint8_t)x;
        r->path_gy[n] = (uint8_t)y1;
        n++;
    }
    for (int y = y1; y > y0; y--) {
        r->path_gx[n] = (uint8_t)x0;
        r->path_gy[n] = (uint8_t)y;
        n++;
    }
    return n;
}

/* True if (x,y) has a road 4-neighbour other than the allowed anchor cell. */
static bool road_neighbor_other(const zone_t *z, int x, int y, int ax, int ay) {
    static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int k = 0; k < 4; k++) {
        const int xx = x + nb[k][0];
        const int yy = y + nb[k][1];
        if (!zin(z, xx, yy) || (xx == ax && yy == ay)) {
            continue;
        }
        if (z->occ[(yy * z->cols) + xx] == OCC_ROAD) {
            return true;
        }
    }
    return false;
}

/* Insert two cells right after path index i (between i and i+1). */
static void path_insert2(tj_run_t *r, int i, int q1x, int q1y, int q2x, int q2y) {
    for (int k = r->path_cells - 1; k > i; k--) {
        r->path_gx[k + 2] = r->path_gx[k];
        r->path_gy[k + 2] = r->path_gy[k];
    }
    r->path_gx[i + 1] = (uint8_t)q1x;
    r->path_gy[i + 1] = (uint8_t)q1y;
    r->path_gx[i + 2] = (uint8_t)q2x;
    r->path_gy[i + 2] = (uint8_t)q2y;
    r->path_cells += 2;
}

/* Push the loop edge (i -> i+1) outward by one cell, adding a 2-cell bend.
 * Outward = the side farther from the zone centre, so bends never cross the aul. */
static bool try_bump(zone_t *z, tj_run_t *r, int i) {
    if (r->path_cells <= 0) {
        return false;
    }
    const int j = (i + 1) % r->path_cells;
    const int ax = r->path_gx[i];
    const int ay = r->path_gy[i];
    const int bx = r->path_gx[j];
    const int by = r->path_gy[j];
    const int dx = bx - ax;
    const int dy = by - ay;
    const float cx = (float)(z->cols - 1) * 0.5F;
    const float cy = (float)(z->rows - 1) * 0.5F;
    const float mx = (float)(ax + bx) * 0.5F;
    const float my = (float)(ay + by) * 0.5F;
    int nx = -dy;
    int ny = dx;
    const float dpos = dist2(mx + (float)nx, my + (float)ny, cx, cy);
    const float dneg = dist2(mx - (float)nx, my - (float)ny, cx, cy);
    if (dneg > dpos) {
        nx = -nx;
        ny = -ny;
    }
    const int q1x = ax + nx;
    const int q1y = ay + ny;
    const int q2x = bx + nx;
    const int q2y = by + ny;
    if (!zin(z, q1x, q1y) || !zin(z, q2x, q2y)) {
        return false;
    }
    if (*zocc(z, q1x, q1y) != OCC_EMPTY || *zocc(z, q2x, q2y) != OCC_EMPTY) {
        return false;
    }
    if (road_neighbor_other(z, q1x, q1y, ax, ay) || road_neighbor_other(z, q2x, q2y, bx, by)) {
        return false; /* would touch another segment -> keep the loop simple */
    }
    path_insert2(r, i, q1x, q1y, q2x, q2y);
    *zocc(z, q1x, q1y) = OCC_ROAD;
    *zocc(z, q2x, q2y) = OCC_ROAD;
    return true;
}

/* One build slot per road cell: the outward-most empty neighbour, used once. */
static void compute_slots(zone_t *z, tj_run_t *r) {
    const float cx = (float)(z->cols - 1) * 0.5F;
    const float cy = (float)(z->rows - 1) * 0.5F;
    for (int i = 0; i < r->path_cells; i++) {
        const int gx = r->path_gx[i];
        const int gy = r->path_gy[i];
        const int ox = sign_away((float)gx, cx);
        const int oy = sign_away((float)gy, cy);
        const int cand[4][2] = {{ox, 0}, {0, oy}, {-ox, 0}, {0, -oy}};
        r->slot_gx[i] = TJ_NO_SLOT;
        r->slot_gy[i] = TJ_NO_SLOT;
        for (int k = 0; k < 4; k++) {
            if (cand[k][0] == 0 && cand[k][1] == 0) {
                continue;
            }
            const int sx = gx + cand[k][0];
            const int sy = gy + cand[k][1];
            if (zin(z, sx, sy) && *zocc(z, sx, sy) == OCC_EMPTY) {
                *zocc(z, sx, sy) = OCC_SLOT; /* reserve so two cells never share a slot */
                r->slot_gx[i] = (uint8_t)sx;
                r->slot_gy[i] = (uint8_t)sy;
                break;
            }
        }
    }
}

// #region per-circle population (events on the road, functional objects in the field)
/* Largest pool circle <= this circle (so the GDD need not define every circle). */
static int best_spawn_circle(int circle) {
    int best = -1;
    for (int s = 0; s < g_config.spawn_count; s++) {
        const int c = g_config.spawns[s].circle;
        if (c <= circle && c > best) {
            best = c;
        }
    }
    return best;
}

/* Road event (scope on_enter): a random empty road cell. */
static void spawn_road(tj_run_t *r, int tile, int count) {
    for (int c = 0; c < count; c++) {
        for (int attempt = 0; attempt < 40; attempt++) {
            const int i = rng_range_int(0, r->path_cells - 1);
            if (r->tile_at[i] < 0) {
                r->tile_at[i] = tile;
                break;
            }
        }
    }
}

/* Field object (scope adjacent): a free build-slot beside the road, fires on pass. */
static void spawn_field_adjacent(tj_run_t *r, int tile, int count) {
    for (int c = 0; c < count; c++) {
        for (int attempt = 0; attempt < 40; attempt++) {
            const int i = rng_range_int(0, r->path_cells - 1);
            if (r->slot_gx[i] != TJ_NO_SLOT && r->roadside[i] < 0) {
                r->roadside[i] = tile;
                break;
            }
        }
    }
}

/* Field object (scope global): a free desert cell; effect is applied per circle. */
static void spawn_global(zone_t *z, tj_run_t *r, int tile, int count) {
    for (int c = 0; c < count && r->global_count < TJ_MAX_GLOBAL; c++) {
        uint8_t fx[TJ_ZONE_MAX * TJ_ZONE_MAX];
        uint8_t fy[TJ_ZONE_MAX * TJ_ZONE_MAX];
        int fn = 0;
        for (int y = 0; y < z->rows; y++) {
            for (int x = 0; x < z->cols; x++) {
                if (*zocc(z, x, y) == OCC_EMPTY) {
                    fx[fn] = (uint8_t)x;
                    fy[fn] = (uint8_t)y;
                    fn++;
                }
            }
        }
        if (fn == 0) {
            return; /* no open desert left */
        }
        const int pick = rng_range_int(0, fn - 1);
        *zocc(z, fx[pick], fy[pick]) = OCC_GLOBAL;
        const int g = r->global_count++;
        r->global_tile[g] = tile;
        r->global_gx[g] = fx[pick];
        r->global_gy[g] = fy[pick];
    }
}

/* Global object effect: passive resources once per circle (no cost, no check). */
static void apply_global(tj_run_t *r, int idx) {
    const tj_tile_def_t *t = &g_config.tiles[idx];
    r->supplies += t->supplies;
    r->wisdom += t->wisdom;
    r->glory += t->glory;
    r->stamina += t->stamina_restore;
    tj_journal_push(TJ_LOG_GOOD, "%s питает род [з%+d м%+d с%+d]", t->name, t->supplies, t->wisdom, t->glory);
}

/* Fill this circle's pool: road events + field objects, from spawns.tsv. */
static void populate_circle(zone_t *z, tj_run_t *r) {
    for (int i = 0; i < TJ_MAX_PATH; i++) {
        r->tile_at[i] = -1;
        r->roadside[i] = -1;
    }
    r->global_count = 0;
    if (g_config.debug_random_desert) {
        for (int i = 0; i < r->path_cells; i++) {
            r->tile_at[i] = (g_config.tile_count > 0) ? rng_range_int(0, g_config.tile_count - 1) : -1;
        }
        return;
    }
    const int target = best_spawn_circle(r->circle);
    for (int s = 0; s < g_config.spawn_count; s++) {
        const tj_spawn_t *sp = &g_config.spawns[s];
        if (sp->circle != target || sp->tile_index < 0 || sp->count <= 0) {
            continue;
        }
        if (sp->layer == TJ_SPAWN_ROAD) {
            spawn_road(r, sp->tile_index, sp->count);
        } else if (sp->scope == TJ_SCOPE_GLOBAL) {
            spawn_global(z, r, sp->tile_index, sp->count);
        } else {
            spawn_field_adjacent(r, sp->tile_index, sp->count);
        }
    }
    for (int g = 0; g < r->global_count; g++) {
        apply_global(r, r->global_tile[g]);
    }
}
// #endregion

/* Per-circle, per-heir variety: deterministic seed, a tight base ring around the
 * aul, then random outward bends -> a different winding loop each circle. */
static void gen_loop(tj_run_t *r) {
    rng_seed((uint32_t)((r->heir_index * 7919) + (r->circle * 104729) + 1));
    zone_t z = {0};
    z.cols = clampi(g_config.map_zone_cols, 4, TJ_ZONE_MAX);
    z.rows = clampi(g_config.map_zone_rows, 4, TJ_ZONE_MAX);
    const int aw = clampi(g_config.map_aul_w, 1, z.cols - 2);
    const int ah = clampi(g_config.map_aul_h, 1, z.rows - 2);
    const int ax0 = (z.cols - aw) / 2;
    const int ay0 = (z.rows - ah) / 2;
    r->grid_cols = z.cols;
    r->grid_rows = z.rows;
    r->aul_x0 = ax0;
    r->aul_y0 = ay0;
    r->aul_w = aw;
    r->aul_h = ah;
    for (int y = ay0; y < ay0 + ah; y++) {
        for (int x = ax0; x < ax0 + aw; x++) {
            *zocc(&z, x, y) = OCC_AUL;
        }
    }
    r->path_cells = rect_loop(r, ax0 - 1, ay0 - 1, ax0 + aw, ay0 + ah);
    for (int i = 0; i < r->path_cells; i++) {
        *zocc(&z, r->path_gx[i], r->path_gy[i]) = OCC_ROAD;
    }
    int bends = g_config.map_bends_base + (g_config.map_bends_per_circle * (r->circle - 1));
    if (g_config.map_bends_jitter > 0) {
        bends += rng_range_int(0, g_config.map_bends_jitter);
    }
    for (int attempt = 0, done = 0; done < bends && attempt < 400 && r->path_cells + 2 <= TJ_MAX_PATH; attempt++) {
        if (try_bump(&z, r, rng_range_int(0, r->path_cells - 1))) {
            done++;
        }
    }
    compute_slots(&z, r);
    populate_circle(&z, r); /* road events + field objects from this circle's pool */
}
// #endregion

// #region intro (FTUE: a new heir leaves the aul before the loop starts)
static const char *heir_name(const tj_run_t *r) {
    if (r->heir_index >= 0 && r->heir_index < g_config.heir_count) {
        return g_config.heirs[r->heir_index].name;
    }
    return "Наследник";
}

/* Advance the pre-loop intro: aul_exit -> road_entry -> walk. No loop tick here,
 * so the loading-settle dt never drifts the hero onto the loop. */
static void tick_intro(tj_run_t *r, float dt) {
    r->intro_t += dt;
    if (r->phase == TJ_PHASE_AUL_EXIT) {
        const float dur = (g_config.aul_exit_seconds > 0.0F) ? g_config.aul_exit_seconds : 2.0F;
        if (r->intro_t >= dur) {
            r->intro_t = 0.0F;
            r->phase = TJ_PHASE_ROAD_ENTRY;
            tj_journal_push(TJ_LOG_PLAIN, "%s вступает на кольцевую дорогу.", heir_name(r));
        }
        return;
    }
    const float dur = (g_config.road_entry_seconds > 0.0F) ? g_config.road_entry_seconds : 0.7F;
    if (r->intro_t >= dur) {
        r->intro_t = 0.0F;
        r->phase = TJ_PHASE_WALK;
    }
}
// #endregion

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
    r->day = 1;
    r->alive = true;
    r->phase = TJ_PHASE_AUL_EXIT;
    r->intro_t = 0.0F;
    tj_journal_clear();
    tj_journal_push(TJ_LOG_BIG, "%s выходит из стойбища. Костёр остаётся за спиной.", heir_name(r));
    gen_loop(r);                              /* generates the loop and populates this circle (may log global effects) */
    r->hand = tj_config_tile_index("saxaul"); /* FTUE: start holding a guaranteed Saxaul card (GDD: small, common) */
    r->tamga_cell = -1;
    if (g_aul.tamga_pending && r->path_cells > 0) {
        r->tamga_cell = g_aul.tamga_cell % r->path_cells; /* wrap a prior loop's cell into this loop */
        r->tamga_wisdom = g_aul.tamga_wisdom;
        r->tamga_glory = g_aul.tamga_glory;
        tj_journal_push(TJ_LOG_BIG, "На песке проступает Последняя Тамга предка.");
    }
    (void)snprintf(r->last_event, sizeof r->last_event, "%s", "Выход из аула");
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

static void apply_tile(tj_run_t *r, int idx) {
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

static void resolve_cell(tj_run_t *r) {
    if (r->cell < 0 || r->cell >= TJ_MAX_PATH) {
        return;
    }
    if (r->cell == r->tamga_cell) {
        r->wisdom += r->tamga_wisdom;
        r->glory += r->tamga_glory;
        tj_journal_push(TJ_LOG_BIG, "Подобрана Последняя Тамга: +%d мудрости, +%d славы.", r->tamga_wisdom, r->tamga_glory);
        r->tamga_cell = -1;
        tj_tamga_clear();
    }
    const int road = r->tile_at[r->cell];
    const int side = r->roadside[r->cell];
    const bool has_road = (road >= 0 && road < g_config.tile_count);
    const bool has_side = (side >= 0 && side < g_config.tile_count);
    if (!has_road && !has_side) {
        tj_journal_push(TJ_LOG_PLAIN, "Пустая клетка");
        return;
    }
    if (has_road) {
        apply_tile(r, road);
    }
    if (has_side && r->alive) {
        apply_tile(r, side);
    }
}

/* Offer 3 (preferably distinct) cards and pause for the player's pick. */
static void offer_cards(tj_run_t *r) {
    if (g_config.tile_count <= 0) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        int t = rng_range_int(0, g_config.tile_count - 1);
        for (int tries = 0; tries < 16 && ((i > 0 && t == r->choice[0]) || (i > 1 && t == r->choice[1])); tries++) {
            t = rng_range_int(0, g_config.tile_count - 1);
        }
        r->choice[i] = t;
    }
    r->choosing = true;
    tj_journal_push(TJ_LOG_BIG, "Конец круга: выбери дар.");
}

void tj_run_choose_card(tj_run_t *r, int idx) {
    if (!r->choosing || idx < 0 || idx > 2) {
        return;
    }
    r->hand = r->choice[idx];
    r->choosing = false;
    if (r->hand >= 0 && r->hand < g_config.tile_count) {
        tj_journal_push(TJ_LOG_GOOD, "Взята карта: %s", g_config.tiles[r->hand].name);
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
    if (dt > 0.1F) {
        dt = 0.1F; /* max frame time: a slow load/hitch frame can't lurch the hero, no spiral */
    }
    if (r->phase != TJ_PHASE_WALK) {
        tick_intro(r, dt); /* still leaving the aul: don't tick the loop yet */
        return;
    }
    if (r->choosing) {
        return; /* paused on the end-of-circle card choice */
    }
    r->move_t += dt;
    int guard = 0;
    while (r->move_t >= per && r->alive && !r->won && !r->choosing && guard < TJ_MAX_PATH) {
        r->move_t -= per;
        guard++;
        r->cell++;
        r->day++; /* each cell-to-cell step is a day of travel */
        if (r->cell >= r->path_cells) {
            r->cell = 0;
            r->circle++;
            if (r->circle > g_config.laps_to_win) {
                r->won = true;
                tj_journal_push(TJ_LOG_BIG, "Кольцо разорвано! Род свободен.");
                return;
            }
            tj_journal_push(TJ_LOG_BIG, "Круг %d пройден.", r->circle - 1);
            gen_loop(r);
            offer_cards(r); /* end of circle: pause for a 1-of-3 card choice */
        }
        resolve_cell(r);
    }
}

bool tj_run_place_roadside(tj_run_t *r, int slot) {
    if (slot < 0 || slot >= r->path_cells) {
        return false;
    }
    if (r->hand < 0 || r->hand >= g_config.tile_count) {
        return false;
    }
    if (r->roadside[slot] >= 0) {
        return false; /* slot already taken */
    }
    r->roadside[slot] = r->hand;
    tj_journal_push(TJ_LOG_GOOD, "Поставлен тайл: %s (слот %d)", g_config.tiles[r->hand].name, slot);
    r->hand = -1;
    return true;
}
