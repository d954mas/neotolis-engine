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

// #region event log (templated lines from log.tsv; GDD owns the wording)
typedef struct {
    const char *tile;
    const char *stat;
    const char *hero;
    int diff, supplies, wisdom, glory, stamina, circle;
} tj_log_ctx_t;

static tj_log_kind_t tone_to_kind(const char *tone) {
    if (strcmp(tone, "gain") == 0 || strcmp(tone, "success") == 0 || strcmp(tone, "card") == 0) {
        return TJ_LOG_GOOD;
    }
    if (strcmp(tone, "danger") == 0) {
        return TJ_LOG_BAD;
    }
    if (strcmp(tone, "circle") == 0 || strcmp(tone, "death") == 0 || strcmp(tone, "memory") == 0) {
        return TJ_LOG_BIG;
    }
    return TJ_LOG_PLAIN;
}

static const char *ph_value(const char *name, const tj_log_ctx_t *c, char *num, size_t numcap) {
    if (strcmp(name, "tile") == 0) {
        return c->tile ? c->tile : "";
    }
    if (strcmp(name, "stat") == 0) {
        return c->stat ? c->stat : "";
    }
    if (strcmp(name, "hero") == 0) {
        return c->hero ? c->hero : "";
    }
    int v = 0;
    if (strcmp(name, "supplies") == 0) {
        v = c->supplies;
    } else if (strcmp(name, "wisdom") == 0) {
        v = c->wisdom;
    } else if (strcmp(name, "glory") == 0) {
        v = c->glory;
    } else if (strcmp(name, "stamina") == 0) {
        v = c->stamina;
    } else if (strcmp(name, "circle") == 0) {
        v = c->circle;
    } else if (strcmp(name, "diff") == 0) {
        v = c->diff;
    } else {
        return "";
    }
    (void)snprintf(num, numcap, "%d", v);
    return num;
}

/* Expand {placeholders} in `tmpl` into `out` using ctx values (raw-byte copy, UTF-8 safe). */
static void log_subst(char *out, size_t cap, const char *tmpl, const tj_log_ctx_t *c) {
    size_t o = 0;
    size_t i = 0;
    while (tmpl[i] != '\0' && o + 1 < cap) {
        if (tmpl[i] != '{') {
            out[o++] = tmpl[i++];
            continue;
        }
        char name[16];
        size_t n = 0;
        size_t j = i + 1;
        while (tmpl[j] != '\0' && tmpl[j] != '}' && n + 1 < sizeof name) {
            name[n++] = tmpl[j++];
        }
        name[n] = '\0';
        if (tmpl[j] == '}') {
            j++;
        }
        char num[16];
        const char *rep = ph_value(name, c, num, sizeof num);
        for (size_t k = 0; rep[k] != '\0' && o + 1 < cap; k++) {
            out[o++] = rep[k];
        }
        i = j;
    }
    out[o] = '\0';
}

/* Push a log line from log.tsv by event id, filling placeholders from ctx. */
static void log_event(const char *id, const tj_log_ctx_t *ctx) {
    const tj_log_event_t *e = tj_config_log_event(id);
    if (e == NULL) {
        return; /* GDD has not defined this event yet */
    }
    const tj_log_ctx_t empty = {0};
    char buf[TJ_JOURNAL_LINE];
    log_subst(buf, sizeof buf, e->tmpl, ctx ? ctx : &empty);
    tj_journal_push(tone_to_kind(e->tone), "%s", buf);
}
// #endregion

// #region loop generation (winding closed loop around the central aul)
enum { OCC_EMPTY = 0, OCC_AUL = 1, OCC_ROAD = 2, OCC_SLOT = 3, OCC_GLOBAL = 4, OCC_FIELD = 5 };

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

/* Build slot per road cell: TWO cells outward (the 1-cell gap is a no-build buffer)
 * so the player builds FARTHER from the aul, never right against the road. */
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
            const int bx = gx + cand[k][0]; /* buffer cell (1 out, no-build) */
            const int by = gy + cand[k][1];
            const int sx = gx + (2 * cand[k][0]); /* build cell (2 out) */
            const int sy = gy + (2 * cand[k][1]);
            const bool clear = zin(z, bx, by) && *zocc(z, bx, by) == OCC_EMPTY && zin(z, sx, sy) && *zocc(z, sx, sy) == OCC_EMPTY && !road_neighbor_other(z, sx, sy, -1, -1);
            if (clear) {
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

/* A field tile's effect: passive resources once per circle (no cost, no check). */
static void apply_tile_income(tj_run_t *r, int idx) {
    const tj_tile_def_t *t = &g_config.tiles[idx];
    r->supplies += t->supplies;
    r->wisdom += t->wisdom;
    r->glory += t->glory;
    r->stamina += t->stamina_restore;
    log_event("resource_gain", &(tj_log_ctx_t){.supplies = t->supplies, .wisdom = t->wisdom, .glory = t->glory});
}

/* Apply the player's persistent field builds (global income) for this circle. */
static void apply_field(tj_run_t *r) {
    const int n = r->grid_cols * r->grid_rows;
    for (int i = 0; i < n && i < TJ_ZONE_CELLS; i++) {
        if (r->field_tile[i] >= 0 && r->field_tile[i] < g_config.tile_count) {
            apply_tile_income(r, r->field_tile[i]);
        }
    }
}

/* Fill this circle's road pool (road reshuffles); the field is persistent. */
static void populate_circle(zone_t *z, tj_run_t *r) {
    for (int i = 0; i < TJ_MAX_PATH; i++) {
        r->tile_at[i] = -1;
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
        } else {
            spawn_global(z, r, sp->tile_index, sp->count); /* auto desert income, per circle */
        }
    }
    for (int g = 0; g < r->global_count; g++) {
        apply_tile_income(r, r->global_tile[g]);
    }
    apply_field(r); /* player's persistent builds pay out too */
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
    /* Persistent player builds block the road: it re-routes around them each circle. */
    for (int y = 0; y < z.rows; y++) {
        for (int x = 0; x < z.cols; x++) {
            if (r->field_tile[(y * z.cols) + x] >= 0) {
                *zocc(&z, x, y) = OCC_FIELD;
            }
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
/* Advance the pre-loop intro: aul_exit -> road_entry -> walk. No loop tick here,
 * so the loading-settle dt never drifts the hero onto the loop. */
static void tick_intro(tj_run_t *r, float dt) {
    r->intro_t += dt;
    if (r->phase == TJ_PHASE_AUL_EXIT) {
        const float dur = (g_config.aul_exit_seconds > 0.0F) ? g_config.aul_exit_seconds : 2.0F;
        if (r->intro_t >= dur) {
            r->intro_t = 0.0F;
            r->phase = TJ_PHASE_ROAD_ENTRY;
            tj_journal_push(TJ_LOG_PLAIN, "Путник вступает на кольцевую дорогу.");
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
    r->storm_t = 0.0F;
    for (int i = 0; i < TJ_ZONE_CELLS; i++) {
        r->field_tile[i] = -1; /* empty field (memset 0 would read as tile 0) */
    }
    tj_journal_clear();
    /* FTUE: a heir is read as a generic "путник", not a personal name (GDD). */
    if (g_aul.deaths == 0) {
        log_event("run_start", &(tj_log_ctx_t){.hero = "Первый путник"});
    } else {
        log_event("new_heir", NULL);
    }
    gen_loop(r);                              /* generates the loop and populates this circle (may log global effects) */
    r->hand = tj_config_tile_index("saxaul"); /* FTUE: start holding a guaranteed Saxaul card (GDD: small, common) */
    r->tamga_cell = -1;
    if (g_aul.tamga_pending && r->path_cells > 0) {
        r->tamga_cell = g_aul.tamga_cell % r->path_cells; /* wrap a prior loop's cell into this loop */
        r->tamga_wisdom = g_aul.tamga_wisdom;
        r->tamga_glory = g_aul.tamga_glory;
        log_event("tamga_spawn", NULL);
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
    const bool is_check = (t->kind == TJ_TILE_CHECK && t->check != TJ_STAT_NONE);
    bool failed = false;

    if (is_check) {
        const int diff = g_config.check_base_difficulty + (g_config.check_difficulty_per_circle * r->circle);
        if (tj_hero_stat(r, t->check) < diff) {
            failed = true;
            supplies = supplies * g_config.check_fail_reward_pct / 100;
            wisdom = wisdom * g_config.check_fail_reward_pct / 100;
            glory = glory * g_config.check_fail_reward_pct / 100;
            stam -= g_config.check_fail_stamina_loss;
        }
    }

    r->supplies += supplies;
    r->wisdom += wisdom;
    r->glory += glory;
    r->stamina += stam;

    const tj_log_ctx_t ctx = {.tile = t->name, .stat = stat_name(t->check), .supplies = supplies, .wisdom = wisdom, .glory = glory, .stamina = stam};
    if (is_check) {
        log_event(failed ? "check_fail" : "check_success", &ctx);
    } else {
        log_event("tile_safe", &ctx);
    }

    if (r->stamina <= 0) {
        r->stamina = 0;
        r->alive = false;
        log_event("death", NULL);
    }
}

static void resolve_cell(tj_run_t *r) {
    if (r->cell < 0 || r->cell >= TJ_MAX_PATH) {
        return;
    }
    if (r->cell == r->tamga_cell) {
        r->wisdom += r->tamga_wisdom;
        r->glory += r->tamga_glory;
        log_event("tamga_pickup", &(tj_log_ctx_t){.wisdom = r->tamga_wisdom, .glory = r->tamga_glory});
        r->tamga_cell = -1;
        tj_tamga_clear();
    }
    const int road = r->tile_at[r->cell];
    if (road >= 0 && road < g_config.tile_count) {
        apply_tile(r, road);
    } else {
        tj_journal_push(TJ_LOG_PLAIN, "Пустая клетка");
    }
}

/* Grant a reward pack (3 preferably-distinct cards). The hero does NOT stop —
 * the player opens the pack and picks from the hand bar whenever they like. */
static void push_pack(tj_run_t *r) {
    if (g_config.tile_count <= 0 || r->packs >= TJ_MAX_PACKS) {
        return;
    }
    int *offer = r->pack_offer[r->packs];
    for (int i = 0; i < 3; i++) {
        int t = rng_range_int(0, g_config.tile_count - 1);
        for (int tries = 0; tries < 16 && ((i > 0 && t == offer[0]) || (i > 1 && t == offer[1])); tries++) {
            t = rng_range_int(0, g_config.tile_count - 1);
        }
        offer[i] = t;
    }
    r->packs++;
    r->pack_open = true; /* offer the choice over the map (does not pause the run) */
    tj_journal_push(TJ_LOG_GOOD, "Дар за круг — выбери карту над аулом.");
}

void tj_run_open_pack(tj_run_t *r) {
    if (r->packs > 0) {
        r->pack_open = true;
    }
}

void tj_run_choose_card(tj_run_t *r, int idx) {
    if (!r->pack_open || r->packs <= 0 || idx < 0 || idx > 2) {
        return;
    }
    r->hand = r->pack_offer[0][idx];
    for (int p = 1; p < r->packs; p++) { /* pop the front pack */
        for (int k = 0; k < 3; k++) {
            r->pack_offer[p - 1][k] = r->pack_offer[p][k];
        }
    }
    r->packs--;
    r->pack_open = (r->packs > 0); /* keep the chooser up if more packs queued */
    if (r->hand >= 0 && r->hand < g_config.tile_count) {
        log_event("card_gain", &(tj_log_ctx_t){.tile = g_config.tiles[r->hand].name});
    }
}

/* Heir perk: a flat per-circle passive (sidegrade). Applied each completed circle. */
static void apply_perk(tj_run_t *r) {
    if (r->heir_index < 0 || r->heir_index >= g_config.heir_count) {
        return;
    }
    const tj_heir_def_t *h = &g_config.heirs[r->heir_index];
    const int v = h->perk_value;
    switch (h->perk) {
    case TJ_PERK_STAMINA_PER_CIRCLE:
        r->stamina += v;
        break;
    case TJ_PERK_SUPPLIES_PER_CIRCLE:
        r->supplies += v;
        break;
    case TJ_PERK_WISDOM_PER_CIRCLE:
        r->wisdom += v;
        break;
    case TJ_PERK_GLORY_PER_CIRCLE:
        r->glory += v;
        break;
    default:
        break;
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
    if (r->storm_t > 0.0F) {
        r->storm_t -= dt; /* veil over the path reshuffle, fades on its own */
    }
    if (r->phase != TJ_PHASE_WALK) {
        tick_intro(r, dt); /* still leaving the aul: don't tick the loop yet */
        return;
    }
    r->move_t += dt;
    int guard = 0;
    while (r->move_t >= per && r->alive && !r->won && guard < TJ_MAX_PATH) {
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
            log_event("lap_complete", &(tj_log_ctx_t){.circle = r->circle - 1});
            r->storm_t = (g_config.storm_seconds > 0.0F) ? g_config.storm_seconds : 1.3F; /* veil the reshuffle */
            gen_loop(r);
            push_pack(r);  /* grant a reward pack; the hero keeps walking */
            apply_perk(r); /* heir's per-circle passive */
        }
        resolve_cell(r);
    }
}

static bool cell_on_road(const tj_run_t *r, int gx, int gy) {
    for (int i = 0; i < r->path_cells && i < TJ_MAX_PATH; i++) {
        if (r->path_gx[i] == gx && r->path_gy[i] == gy) {
            return true;
        }
    }
    return false;
}

bool tj_run_place_field(tj_run_t *r, int gx, int gy) {
    if (r->hand < 0 || r->hand >= g_config.tile_count) {
        return false;
    }
    if (gx < 0 || gx >= r->grid_cols || gy < 0 || gy >= r->grid_rows) {
        return false;
    }
    const bool on_aul = (gx >= r->aul_x0 && gx < r->aul_x0 + r->aul_w && gy >= r->aul_y0 && gy < r->aul_y0 + r->aul_h);
    if (on_aul || cell_on_road(r, gx, gy)) {
        return false;
    }
    const int idx = (gy * r->grid_cols) + gx;
    if (idx < 0 || idx >= TJ_ZONE_CELLS || r->field_tile[idx] >= 0) {
        return false; /* out of range or already built */
    }
    r->field_tile[idx] = r->hand;
    log_event("card_placed", &(tj_log_ctx_t){.tile = g_config.tiles[r->hand].name});
    r->hand = -1;
    return true;
}
