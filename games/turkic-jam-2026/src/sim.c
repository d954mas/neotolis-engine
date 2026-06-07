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
static float dist2(float px, float py, float cx, float cy) {
    const float dx = px - cx;
    const float dy = py - cy;
    return (dx * dx) + (dy * dy);
}
static bool zin(const zone_t *z, int x, int y) { return x >= 0 && x < z->cols && y >= 0 && y < z->rows; }
static uint8_t *zocc(zone_t *z, int x, int y) { return &z->occ[(y * z->cols) + x]; }

static bool cell_on_road(const tj_run_t *r, int gx, int gy); /* defined below */

/* Chebyshev distance from (gx,gy) to the aul rect (0 = on/inside the aul). The
 * concentric bands key off this: 1..road_band = road band (no build), beyond = field. */
int tj_run_dist_to_aul(const tj_run_t *r, int gx, int gy) {
    int dx = 0;
    if (gx < r->aul_x0) {
        dx = r->aul_x0 - gx;
    } else if (gx >= r->aul_x0 + r->aul_w) {
        dx = gx - (r->aul_x0 + r->aul_w - 1);
    }
    int dy = 0;
    if (gy < r->aul_y0) {
        dy = r->aul_y0 - gy;
    } else if (gy >= r->aul_y0 + r->aul_h) {
        dy = gy - (r->aul_y0 + r->aul_h - 1);
    }
    return (dx > dy) ? dx : dy;
}

bool tj_run_cell_buildable(const tj_run_t *r, int gx, int gy) {
    if (gx < 0 || gx >= r->grid_cols || gy < 0 || gy >= r->grid_rows) {
        return false;
    }
    if (tj_run_dist_to_aul(r, gx, gy) <= g_config.map_road_band) {
        return false; /* aul + road band */
    }
    if (cell_on_road(r, gx, gy) || r->field_tile[(gy * r->grid_cols) + gx] >= 0) {
        return false; /* on road, or already built */
    }
    for (int i = 0; i < r->global_count && i < TJ_MAX_GLOBAL; i++) {
        if (r->global_gx[i] == gx && r->global_gy[i] == gy) {
            return false; /* a global landmark occupies it */
        }
    }
    return true;
}

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
    /* Keep the road inside the band around the aul; bends never reach the field. */
    if (tj_run_dist_to_aul(r, q1x, q1y) > g_config.map_road_band || tj_run_dist_to_aul(r, q2x, q2y) > g_config.map_road_band) {
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

/* The outer field (beyond the road band) is buildable. build_gx/gy is a capped debug
 * snapshot for devapi/tests; gameplay uses tj_run_cell_buildable directly. */
static void compute_build(zone_t *z, tj_run_t *r) {
    (void)z;
    r->build_count = 0;
    for (int y = 0; y < r->grid_rows; y++) {
        for (int x = 0; x < r->grid_cols; x++) {
            if (!tj_run_cell_buildable(r, x, y)) {
                continue;
            }
            if (r->build_count < TJ_MAX_BUILD) {
                r->build_gx[r->build_count] = (uint8_t)x;
                r->build_gy[r->build_count] = (uint8_t)y;
                r->build_count++;
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

/* Per-circle effect of the player's persistent field builds: Вода heals each circle, but
 * Жильё's припасы no longer tick here — they pay out once when the heir falls
 * (tj_run_field_supplies banked at death), so income doesn't compound into a fat per-circle
 * stream. */
static void apply_field(tj_run_t *r) {
    const int n = r->grid_cols * r->grid_rows;
    for (int i = 0; i < n && i < TJ_ZONE_CELLS; i++) {
        const int t = r->field_tile[i];
        if (t < 0 || t >= g_config.tile_count || g_config.tiles[t].stamina_restore <= 0) {
            continue;
        }
        r->stamina += g_config.tiles[t].stamina_restore; /* Вода: per-circle heal */
        if (r->stamina > r->stamina_max) {
            r->stamina = r->stamina_max;
        }
    }
}

/* Total припасы the placed buildings pay into the aul when the heir falls — a one-time
 * legacy payout (Жильё = savings), not a compounding per-circle stream. */
int tj_run_field_supplies(const tj_run_t *r) {
    int sum = 0;
    const int n = r->grid_cols * r->grid_rows;
    for (int i = 0; i < n && i < TJ_ZONE_CELLS; i++) {
        const int t = r->field_tile[i];
        if (t >= 0 && t < g_config.tile_count) {
            sum += g_config.tiles[t].supplies;
        }
    }
    return sum;
}

/* Fill this circle's road pool (road reshuffles); the field is persistent. */
/* Pick a random enemy (check-kind) tile for a fight cell; -1 if none defined. */
static int pick_enemy_tile(void) {
    int idx[TJ_MAX_TILES];
    int n = 0;
    for (int i = 0; i < g_config.tile_count; i++) {
        if (g_config.tiles[i].kind == TJ_TILE_CHECK && g_config.tiles[i].check != TJ_STAT_NONE) {
            idx[n++] = i;
        }
    }
    if (n == 0) {
        return -1;
    }
    return idx[rng_range_int(0, n - 1)];
}

/* Lay out this circle's road as a Capybara-Go rhythm: trail filler, a fight every
 * 3rd step, an event every 5th, an elite mid-lap, and a boss on the last cell. */
static void populate_road_rhythm(tj_run_t *r) {
    const int last = r->path_cells - 1;
    const int mid = r->path_cells / 2;
    for (int i = 0; i < r->path_cells; i++) {
        const int n = i + 1; /* 1-based step from the aul entry */
        tj_cell_role_t role;
        if (i == last) {
            role = TJ_CELL_BOSS;
        } else if (i == last - 1) {
            role = TJ_CELL_REST; /* breather + heal before the boss */
        } else if (i == mid) {
            role = TJ_CELL_ELITE;
        } else if (n % 3 == 0) {
            role = TJ_CELL_FIGHT;
        } else if (n % 5 == 0) {
            role = TJ_CELL_EVENT;
        } else {
            role = TJ_CELL_TRAIL;
        }
        r->cell_role[i] = (uint8_t)role;
        if (role == TJ_CELL_FIGHT || role == TJ_CELL_ELITE || role == TJ_CELL_BOSS) {
            r->tile_at[i] = pick_enemy_tile();
        }
    }
}

static void populate_circle(zone_t *z, tj_run_t *r) {
    for (int i = 0; i < TJ_MAX_PATH; i++) {
        r->tile_at[i] = -1;
        r->cell_role[i] = (uint8_t)TJ_CELL_TRAIL;
    }
    r->global_count = 0;
    if (g_config.debug_random_desert) {
        for (int i = 0; i < r->path_cells; i++) {
            r->tile_at[i] = (g_config.tile_count > 0) ? rng_range_int(0, g_config.tile_count - 1) : -1;
        }
        return;
    }
    populate_road_rhythm(r);
    /* Field-layer spawns still give per-circle income; the road is now rhythm-driven. */
    const int target = best_spawn_circle(r->circle);
    for (int s = 0; s < g_config.spawn_count; s++) {
        const tj_spawn_t *sp = &g_config.spawns[s];
        if (sp->circle != target || sp->tile_index < 0 || sp->count <= 0 || sp->layer != TJ_SPAWN_FIELD) {
            continue;
        }
        spawn_global(z, r, sp->tile_index, sp->count); /* auto desert income, per circle */
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
    populate_circle(&z, r); /* road events + auto desert income (marks occ) */
    compute_build(&z, r);   /* open field cells, after occ is fully marked */
}
// #endregion

// #region intro (FTUE: a new heir leaves the aul before the loop starts)
/* Advance the pre-loop intro: aul_exit -> road_entry -> walk. No loop tick here,
 * so the loading-settle dt never drifts the hero onto the loop. */
static void tick_intro(tj_run_t *r, float dt) {
    if (r->phase == TJ_PHASE_AUL_READY) {
        return; /* waiting at the campfire; the player sends the wayfarer off (tj_run_send_wayfarer) */
    }
    r->intro_t += dt;
    if (r->phase == TJ_PHASE_AUL_EXIT) {
        const float dur = (g_config.aul_exit_seconds > 0.0F) ? g_config.aul_exit_seconds : 2.0F;
        if (r->intro_t >= dur) {
            r->intro_t = 0.0F;
            r->phase = TJ_PHASE_ROAD_ENTRY;
            tj_journal_push(TJ_LOG_PLAIN, "Батыр выходит на дорогу.");
        }
        return;
    }
    const float dur = (g_config.road_entry_seconds > 0.0F) ? g_config.road_entry_seconds : 0.7F;
    if (r->intro_t >= dur) {
        r->intro_t = 0.0F;
        r->phase = TJ_PHASE_WALK;
    }
}

void tj_run_send_wayfarer(tj_run_t *r) {
    if (r->phase == TJ_PHASE_AUL_READY) {
        r->phase = TJ_PHASE_AUL_EXIT;
        r->intro_t = 0.0F;
    }
}
// #endregion

// #region field merge (Triple-Town: 3 connected same line+tier -> tier+1, cascades)
static int tile_of_line_tier(int line, int tier) {
    for (int i = 0; i < g_config.tile_count; i++) {
        if (g_config.tiles[i].line == line && g_config.tiles[i].tier == tier) {
            return i;
        }
    }
    return -1;
}

/* A random field card at the circle's drop-floor tier (1 -> 2 -> 3), falling back to
 * a lower tier if none of that tier exist. -1 if there are no field cards at all. */
static int pick_field_card(int circle) {
    int tier = 1;
    if (circle >= 7) {
        tier = 3;
    } else if (circle >= 4) {
        tier = 2;
    }
    for (; tier >= 1; tier--) {
        int idx[TJ_MAX_TILES];
        int n = 0;
        for (int i = 0; i < g_config.tile_count; i++) {
            if (g_config.tiles[i].line > 0 && g_config.tiles[i].tier == tier) {
                idx[n++] = i;
            }
        }
        if (n > 0) {
            return idx[rng_range_int(0, n - 1)];
        }
    }
    return -1;
}

/* Live combat bonuses = sum of placed buildings' boosts. Выносливость also raises max
 * HP and heals the gained amount. Idempotent: recompute from the whole field. */
static void recompute_field_bonuses(tj_run_t *r) {
    int f = 0;
    int s = 0;
    int v = 0;
    const int n = r->grid_cols * r->grid_rows;
    for (int i = 0; i < n && i < TJ_ZONE_CELLS; i++) {
        const int t = r->field_tile[i];
        if (t < 0 || t >= g_config.tile_count || g_config.tiles[t].boost_amount <= 0) {
            continue;
        }
        switch (g_config.tiles[t].boost_stat) {
        case TJ_STAT_BODY:
            f += g_config.tiles[t].boost_amount;
            break;
        case TJ_STAT_MIND:
            s += g_config.tiles[t].boost_amount;
            break;
        case TJ_STAT_SPIRIT:
            v += g_config.tiles[t].boost_amount;
            break;
        default:
            break;
        }
    }
    r->bonus_force = f;
    r->bonus_speed = s;
    const int new_max = r->base_max + (v * g_config.combat_vit_hp);
    if (new_max > r->stamina_max) {
        r->stamina += (new_max - r->stamina_max); /* new Выносливость heals the gained HP */
    }
    r->stamina_max = (new_max > 1) ? new_max : 1;
    if (r->stamina > r->stamina_max) {
        r->stamina = r->stamina_max;
    }
    r->bonus_vigor = v;
}

/* If 3+ connected (orthogonal) cells share line+tier, fuse 3 into one tier+1 here,
 * then cascade. Called after a card is placed/moved into (gx,gy). */
static void try_merge_at(tj_run_t *r, int gx, int gy) {
    const int cols = r->grid_cols;
    for (int iter = 0; iter < 16; iter++) {
        const int start = (gy * cols) + gx;
        if (start < 0 || start >= TJ_ZONE_CELLS) {
            return;
        }
        const int tile = r->field_tile[start];
        if (tile < 0 || tile >= g_config.tile_count) {
            return;
        }
        const int line = g_config.tiles[tile].line;
        const int tier = g_config.tiles[tile].tier;
        if (line == 0) {
            return;
        }
        int stackx[TJ_ZONE_CELLS];
        int stacky[TJ_ZONE_CELLS];
        int comp[TJ_ZONE_CELLS];
        bool seen[TJ_ZONE_CELLS];
        for (int i = 0; i < TJ_ZONE_CELLS; i++) {
            seen[i] = false;
        }
        int sn = 0;
        int cn = 0;
        stackx[sn] = gx;
        stacky[sn] = gy;
        sn++;
        seen[start] = true;
        static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        while (sn > 0) {
            sn--;
            const int x = stackx[sn];
            const int y = stacky[sn];
            comp[cn++] = (y * cols) + x;
            for (int k = 0; k < 4; k++) {
                const int xx = x + nb[k][0];
                const int yy = y + nb[k][1];
                if (xx < 0 || yy < 0 || xx >= cols || yy >= r->grid_rows) {
                    continue;
                }
                const int ni = (yy * cols) + xx;
                if (seen[ni]) {
                    continue;
                }
                const int nt = r->field_tile[ni];
                if (nt >= 0 && nt < g_config.tile_count && g_config.tiles[nt].line == line && g_config.tiles[nt].tier == tier) {
                    seen[ni] = true;
                    stackx[sn] = xx;
                    stacky[sn] = yy;
                    sn++;
                }
            }
        }
        if (cn < 3) {
            return;
        }
        const int up = tile_of_line_tier(line, tier + 1);
        if (up < 0) {
            return; /* already top tier */
        }
        int cleared = 1;
        r->field_tile[start] = -1;
        for (int c = 0; c < cn && cleared < 3; c++) {
            const int ci = comp[c];
            if (ci == start || r->field_tile[ci] < 0) {
                continue;
            }
            r->field_tile[ci] = -1;
            cleared++;
        }
        r->field_tile[start] = up;
        r->merges_done++;
        r->fx_cell = start; /* merge pop: bigger than a plain placement */
        r->fx_cell_t = 0.45F;
        r->fx_cell_mag = 0.60F;
        tj_journal_push(TJ_LOG_GOOD, "Мердж: %s", g_config.tiles[up].name);
        /* loop: the upgraded tile may complete another triple (cascade) */
    }
}

/* Non-mutating telegraph: if `tile` were placed at (gx,gy), how big is the connected
 * same-line+tier group (counting the placed tile)? Fills group[] (cell indices) up to
 * group_cap and sets *out_result to the tier+1 tile when the group would fuse (>=3 and a
 * higher tier exists), else -1. Returns 0 if the cell isn't buildable or tile has no line.
 * Mirrors try_merge_at's connectivity so the preview never lies. */
int tj_run_merge_preview(const tj_run_t *r, int tile, int gx, int gy, int *group, int group_cap, int *out_result) {
    if (out_result != NULL) {
        *out_result = -1;
    }
    if (tile < 0 || tile >= g_config.tile_count || g_config.tiles[tile].line <= 0) {
        return 0;
    }
    if (!tj_run_cell_buildable(r, gx, gy)) {
        return 0;
    }
    const int line = g_config.tiles[tile].line;
    const int tier = g_config.tiles[tile].tier;
    const int cols = r->grid_cols;
    const int start = (gy * cols) + gx;
    if (start < 0 || start >= TJ_ZONE_CELLS) {
        return 0;
    }
    int stackx[TJ_ZONE_CELLS];
    int stacky[TJ_ZONE_CELLS];
    bool seen[TJ_ZONE_CELLS];
    for (int i = 0; i < TJ_ZONE_CELLS; i++) {
        seen[i] = false;
    }
    int sn = 0;
    int cnt = 0;
    stackx[sn] = gx;
    stacky[sn] = gy;
    sn++;
    seen[start] = true;
    static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (sn > 0) {
        sn--;
        const int x = stackx[sn];
        const int y = stacky[sn];
        if (group != NULL && cnt < group_cap) {
            group[cnt] = (y * cols) + x;
        }
        cnt++;
        for (int k = 0; k < 4; k++) {
            const int xx = x + nb[k][0];
            const int yy = y + nb[k][1];
            if (xx < 0 || yy < 0 || xx >= cols || yy >= r->grid_rows) {
                continue;
            }
            const int ni = (yy * cols) + xx;
            if (seen[ni]) {
                continue;
            }
            const int nt = r->field_tile[ni];
            if (nt >= 0 && nt < g_config.tile_count && g_config.tiles[nt].line == line && g_config.tiles[nt].tier == tier) {
                seen[ni] = true;
                stackx[sn] = xx;
                stacky[sn] = yy;
                sn++;
            }
        }
    }
    if (cnt >= 3 && out_result != NULL) {
        *out_result = tj_config_tile_upgrade(tile);
    }
    return cnt;
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
    r->body += g_aul.up_force; /* aul meta upgrades (permanent, between runs) */
    r->mind += g_aul.up_speed;
    r->spirit += g_aul.up_vigor;
    r->stamina += r->spirit * g_config.combat_vit_hp; /* Выносливость -> max HP */
    r->stamina_max = r->stamina;
    r->base_max = r->stamina;
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
        log_event("run_start", &(tj_log_ctx_t){.hero = "Первый батыр"});
    } else {
        log_event("new_heir", NULL);
    }
    gen_loop(r); /* generates the loop and populates this circle (may log global effects) */
    r->hand_count = 0;
    r->merges_done = 0;
    r->forced_pull_tile = -1;
    r->fx_cell = -1;
    r->fx_cell_t = 0.0F;
    r->fx_cell_mag = 0.0F;
    r->pouch = g_config.pouch_start; /* start with a pouch of cards to pull */
    recompute_field_bonuses(r);      /* empty field at start: bonuses 0, but keep state consistent */
    r->tamga_cell = -1;
    if (g_aul.tamga_pending && r->path_cells > 0) {
        r->tamga_cell = g_aul.tamga_cell % r->path_cells; /* wrap a prior loop's cell into this loop */
        r->tamga_wisdom = g_aul.tamga_wisdom;
        r->tamga_glory = g_aul.tamga_glory;
        log_event("tamga_spawn", NULL);
    }
    (void)snprintf(r->last_event, sizeof r->last_event, "%s", "Выход из аула");
}

// #region auto-combat (hero pauses on an enemy cell; deterministic ATB by speed)
static int hero_damage(const tj_run_t *r) { return g_config.combat_dmg_base + r->body + r->bonus_force; }

static float hero_interval(const tj_run_t *r) {
    const float b = (g_config.combat_atk_base > 0.0F) ? g_config.combat_atk_base : 0.55F;
    const float iv = b / (1.0F + ((float)(r->mind + r->bonus_speed) * g_config.combat_spd_mul));
    return (iv < 0.3F) ? 0.3F : iv; /* floor: keep blows visible even at high speed */
}

static int hero_defense(const tj_run_t *r) { return (r->spirit + r->bonus_vigor) * g_config.combat_vit_def; }

/* Grant a tile's rewards (combat-win loot / safe-tile gain), tracking the HP high-water. */
static void grant_rewards(tj_run_t *r, const tj_tile_def_t *t) {
    r->supplies += t->supplies;
    r->wisdom += t->wisdom;
    r->glory += t->glory;
    r->stamina += t->stamina_restore;
    if (r->stamina > r->stamina_max) {
        r->stamina_max = r->stamina;
    }
}

/* Enter auto-battle vs the enemy tile on this cell; the walk pauses until it ends.
 * Elite/boss roles scale enemy HP/atk by config percent. */
static void start_combat(tj_run_t *r, int tile, tj_cell_role_t role) {
    const tj_tile_def_t *t = &g_config.tiles[tile];
    int diff = t->diff_base + (t->diff_per_circle * r->circle);
    if (diff < 1) {
        diff = 1;
    }
    int hp_pct = 100;
    int atk_pct = 100;
    const char *rolelbl = "Бой";
    if (role == TJ_CELL_ELITE) {
        hp_pct = g_config.elite_hp_pct;
        atk_pct = g_config.elite_atk_pct;
        rolelbl = "Элита";
    } else if (role == TJ_CELL_BOSS) {
        hp_pct = g_config.boss_hp_pct;
        atk_pct = g_config.boss_atk_pct;
        rolelbl = "Босс";
    }
    /* Super-linear late ramp: base scales with diff (linear in circle), then a
     * per-circle %-bonus on top. Gentle early, biting by the late circles. */
    const int circle_pct = 100 + (g_config.enemy_scale_per_circle_pct * r->circle);
    int hp = (g_config.enemy_hp_base + (g_config.enemy_hp_per_diff * diff)) * hp_pct / 100 * circle_pct / 100;
    int atk = (g_config.enemy_atk_base + (g_config.enemy_atk_per_diff * diff)) * atk_pct / 100 * circle_pct / 100;
    float interval = (g_config.enemy_atk_interval > 0.0F) ? g_config.enemy_atk_interval : 0.85F;
    /* Archetype (elite/boss): Толстяк=much HP, Шустрый=fast+frail, Лютый=big hit.
     * Each punishes a dumped stat; boss rotates by circle so the player can learn it. */
    const char *archlbl = "";
    if (role == TJ_CELL_ELITE || role == TJ_CELL_BOSS) {
        const bool finalboss = (role == TJ_CELL_BOSS && r->circle >= g_config.laps_to_win);
        const int arch = (role == TJ_CELL_BOSS) ? ((r->circle - 1) % 3) : rng_range_int(0, 2);
        if (finalboss) {
            hp = hp * g_config.arch_fat_hp_pct / 100;
            atk = atk * g_config.arch_fierce_atk_pct / 100;
            archlbl = "Хранитель Кольца";
        } else if (arch == 0) {
            hp = hp * g_config.arch_fat_hp_pct / 100;
            archlbl = "Толстяк";
        } else if (arch == 1) {
            interval = interval * (float)g_config.arch_fast_interval_pct / 100.0F;
            hp = hp * g_config.arch_fast_hp_pct / 100;
            archlbl = "Шустрый";
        } else {
            atk = atk * g_config.arch_fierce_atk_pct / 100;
            archlbl = "Лютый";
        }
    }
    if (hp < 1) {
        hp = 1;
    }
    r->in_combat = true;
    r->combat_tile = tile;
    r->combat_enemy_max = hp;
    r->combat_enemy_hp = hp;
    r->combat_enemy_atk = atk;
    r->combat_enemy_interval = (interval > 0.05F) ? interval : 0.05F;
    r->hero_atk_t = 0.0F;
    r->enemy_atk_t = 0.0F;
    if (archlbl[0] != '\0') {
        (void)snprintf(r->combat_label, sizeof r->combat_label, "%s-%s", rolelbl, archlbl);
    } else {
        (void)snprintf(r->combat_label, sizeof r->combat_label, "%s: %s", rolelbl, t->name);
    }
    tj_journal_push((role == TJ_CELL_BOSS) ? TJ_LOG_BIG : TJ_LOG_BAD, "%s (%s)", r->combat_label, t->name);
}

/* Enemy down: pause on a victory celebration. Loot is snapshot for the readout now but
 * only applied when the celebration ends (combat_win_tick) — the reward lands "after a beat". */
static void enter_combat_win(tj_run_t *r) {
    const tj_tile_def_t *t = &g_config.tiles[r->combat_tile];
    r->in_combat = false;
    r->combat_win = true;
    r->combat_win_t = (g_config.combat_win_seconds > 0.1F) ? g_config.combat_win_seconds : 1.2F;
    r->win_sup = t->supplies;
    r->win_wis = t->wisdom;
    r->win_glory = t->glory;
    r->win_sta = t->stamina_restore;
    tj_journal_push(TJ_LOG_GOOD, "%s повержен", t->name);
}

/* Celebration timer: when it elapses the loot lands (+ a card in the pouch) and the walk resumes. */
static void combat_win_tick(tj_run_t *r, float dt) {
    r->combat_win_t -= dt;
    if (r->fx_enemy_t > 0.0F) {
        r->fx_enemy_t -= dt; /* let the killing-blow number finish floating */
    }
    if (r->combat_win_t <= 0.0F) {
        grant_rewards(r, &g_config.tiles[r->combat_tile]);
        r->pouch += 1; /* combat drops a card into the pouch (pull it from the hand bar) */
        r->combat_win = false;
        r->combat_tile = -1;
    }
}

/* Hero strikes first each frame; whoever's ATB timer fills lands a hit. Deterministic. */
static void combat_tick(tj_run_t *r, float dt) {
    const float hi = hero_interval(r);
    const float ei = (r->combat_enemy_interval > 0.05F) ? r->combat_enemy_interval : 0.85F;
    r->hero_atk_t += dt;
    r->enemy_atk_t += dt;
    if (r->fx_hero_t > 0.0F) {
        r->fx_hero_t -= dt;
    }
    if (r->fx_enemy_t > 0.0F) {
        r->fx_enemy_t -= dt;
    }
    for (int guard = 0; r->hero_atk_t >= hi && r->combat_enemy_hp > 0 && guard < 64; guard++) {
        r->hero_atk_t -= hi;
        const int d = hero_damage(r);
        r->combat_enemy_hp -= d;
        r->fx_enemy_dmg = d;
        r->fx_enemy_t = 0.6F;
    }
    if (r->combat_enemy_hp <= 0) {
        enter_combat_win(r);
        return;
    }
    for (int guard = 0; r->enemy_atk_t >= ei && r->alive && guard < 64; guard++) {
        r->enemy_atk_t -= ei;
        int dmg = r->combat_enemy_atk - hero_defense(r);
        if (dmg < 0) {
            dmg = 0;
        }
        r->stamina -= dmg;
        r->fx_hero_dmg = dmg;
        r->fx_hero_t = 0.6F;
        if (r->stamina <= 0) {
            r->stamina = 0;
            r->alive = false;
            r->in_combat = false;
            log_event("death", NULL);
            return;
        }
    }
}
// #endregion

/* Dice event: multiplicative check on a stat. Roll 1 = fail, max = pass; else
 * effective = stat*(1 + coeff*roll) vs a per-circle DC. Result is computed up front
 * but revealed over event_reveal_seconds (animated die); the walk pauses meanwhile. */
static void start_event(tj_run_t *r) {
    const int die = (g_config.event_die > 1) ? g_config.event_die : 10;
    const int pick = rng_range_int(0, 2);
    tj_stat_t st = TJ_STAT_BODY;
    const char *ename = "Завал";
    const char *sname = "Сабля";
    int bonus = r->bonus_force;
    if (pick == 1) {
        st = TJ_STAT_MIND;
        ename = "Погоня";
        sname = "Конь";
        bonus = r->bonus_speed;
    } else if (pick == 2) {
        st = TJ_STAT_SPIRIT;
        ename = "Буря";
        sname = "Оберег";
        bonus = r->bonus_vigor;
    }
    const int stat = tj_hero_stat(r, st) + bonus; /* effective stat (base + buildings) */
    const int roll = rng_range_int(1, die);
    const int dc = g_config.event_dc_base + (g_config.event_dc_per_circle * r->circle);
    bool pass;
    if (roll <= 1) {
        pass = false; /* natural 1 always fails */
    } else if (roll >= die) {
        pass = true; /* natural max always passes */
    } else {
        const float eff = (float)stat * (1.0F + (g_config.event_dice_coeff * (float)roll));
        pass = (eff >= (float)dc);
    }
    r->in_event = true;
    r->event_t = 0.0F;
    r->ev_die = die;
    r->ev_kind = pick;
    r->ev_roll = roll;
    r->ev_stat = stat;
    r->ev_dc = dc;
    r->ev_pass = pass;
    r->ev_gain = g_config.event_pass_supplies + r->circle;
    (void)snprintf(r->ev_name, sizeof r->ev_name, "%s", ename);
    (void)snprintf(r->ev_statname, sizeof r->ev_statname, "%s", sname);
}

/* Apply the event outcome once the reveal animation has played out. */
static void event_finish(tj_run_t *r) {
    r->in_event = false;
    if (r->ev_pass) {
        r->supplies += r->ev_gain;
        tj_journal_push(TJ_LOG_GOOD, "%s: %s d%d->%d — успех (+%d)", r->ev_name, r->ev_statname, r->ev_die, r->ev_roll, r->ev_gain);
    } else {
        r->stamina -= g_config.event_fail_hp;
        tj_journal_push(TJ_LOG_BAD, "%s: %s d%d->%d — провал (-%d ХП)", r->ev_name, r->ev_statname, r->ev_die, r->ev_roll, g_config.event_fail_hp);
        if (r->stamina <= 0) {
            r->stamina = 0;
            r->alive = false;
            log_event("death", NULL);
        }
    }
}

static void event_tick(tj_run_t *r, float dt) {
    r->event_t += dt;
    const float dur = (g_config.event_reveal_seconds > 0.1F) ? g_config.event_reveal_seconds : 1.5F;
    const float hold = 2.5F; /* keep success/fail readable after the wheel lands (view clamps reveal f to 1) */
    if (r->event_t >= dur + hold) {
        event_finish(r);
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
    const tj_cell_role_t role = (tj_cell_role_t)r->cell_role[r->cell];
    const int enemy = r->tile_at[r->cell];
    if (role == TJ_CELL_FIGHT || role == TJ_CELL_ELITE || role == TJ_CELL_BOSS) {
        if (enemy >= 0 && enemy < g_config.tile_count) {
            start_combat(r, enemy, role); /* walk pauses until the fight resolves */
        } else {
            r->supplies += 1; /* no enemy defined -> minor pickup, never dead air */
        }
    } else if (role == TJ_CELL_EVENT) {
        start_event(r); /* animated dice reveal; walk pauses until it resolves */
    } else if (role == TJ_CELL_REST) {
        int heal = r->stamina_max * g_config.rest_heal_pct / 100;
        if (heal < 1) {
            heal = 1;
        }
        r->stamina += heal;
        if (r->stamina > r->stamina_max) {
            r->stamina = r->stamina_max;
        }
        tj_journal_push(TJ_LOG_GOOD, "Привал: +%d ХП (впереди босс)", heal);
    } else {
        r->supplies += 1; /* trail: silent micro-pickup, no dead air */
    }
}

/* Per-circle reward: top up the pouch. The player pulls cards from the hand bar. */
static void push_pack(tj_run_t *r) {
    if (g_config.tile_count <= 0) {
        return;
    }
    r->pouch += g_config.pouch_per_circle;
    tj_journal_push(TJ_LOG_GOOD, "Дар за круг: +%d в мешочек", g_config.pouch_per_circle);
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
    const int t = r->pack_offer[0][idx];
    if (r->hand_count < TJ_MAX_HAND) {
        r->hand_cards[r->hand_count++] = t;
    }
    for (int p = 1; p < r->packs; p++) { /* pop the front pack */
        for (int k = 0; k < 3; k++) {
            r->pack_offer[p - 1][k] = r->pack_offer[p][k];
        }
    }
    r->packs--;
    r->pack_open = (r->packs > 0); /* keep the chooser up if more packs queued */
    if (t >= 0 && t < g_config.tile_count) {
        log_event("card_gain", &(tj_log_ctx_t){.tile = g_config.tiles[t].name});
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
        if (r->stamina > r->stamina_max) {
            r->stamina_max = r->stamina;
        }
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
    if (r->in_combat) {
        combat_tick(r, dt); /* walk is paused while fighting */
        return;
    }
    if (r->combat_win) {
        combat_win_tick(r, dt); /* victory celebration; walk paused until the loot lands */
        return;
    }
    if (r->in_event) {
        event_tick(r, dt); /* walk is paused during the dice reveal */
        return;
    }
    r->move_t += dt;
    int guard = 0;
    while (r->move_t >= per && r->alive && !r->won && !r->in_combat && !r->in_event && guard < TJ_MAX_PATH) {
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

bool tj_run_place_card(tj_run_t *r, int hand_idx, int gx, int gy) {
    if (hand_idx < 0 || hand_idx >= r->hand_count) {
        return false;
    }
    const int tile = r->hand_cards[hand_idx];
    if (tile < 0 || tile >= g_config.tile_count) {
        return false;
    }
    if (!tj_run_cell_buildable(r, gx, gy)) {
        return false; /* aul / road band / road / occupied -> no build */
    }
    const int idx = (gy * r->grid_cols) + gx;
    r->field_tile[idx] = tile;
    r->fx_cell = idx; /* placement pop (a merge below overrides with a bigger one) */
    r->fx_cell_t = 0.30F;
    r->fx_cell_mag = 0.35F;
    log_event("card_placed", &(tj_log_ctx_t){.tile = g_config.tiles[tile].name});
    for (int k = hand_idx; k < r->hand_count - 1; k++) {
        r->hand_cards[k] = r->hand_cards[k + 1]; /* remove from the fan */
    }
    r->hand_count--;
    try_merge_at(r, gx, gy);    /* 3 connected same line+tier -> fuse + cascade */
    recompute_field_bonuses(r); /* live combat stats reflect the new board */
    return true;
}

bool tj_run_place_field(tj_run_t *r, int gx, int gy) { return tj_run_place_card(r, 0, gx, gy); /* devapi/legacy: place the first held card */ }

bool tj_run_pickup_field(tj_run_t *r, int gx, int gy) {
    if (r->hand_count >= TJ_MAX_HAND) {
        return false; /* fan full: place a card first */
    }
    if (gx < 0 || gx >= r->grid_cols || gy < 0 || gy >= r->grid_rows) {
        return false;
    }
    const int idx = (gy * r->grid_cols) + gx;
    if (idx < 0 || idx >= TJ_ZONE_CELLS || r->field_tile[idx] < 0) {
        return false; /* nothing to lift */
    }
    const int t = r->field_tile[idx];
    r->field_tile[idx] = -1;
    r->hand_cards[r->hand_count++] = t;
    recompute_field_bonuses(r);
    tj_journal_push(TJ_LOG_PLAIN, "Поднял: %s", g_config.tiles[t].name);
    return true;
}

void tj_run_pull_pouch(tj_run_t *r) {
    if (r->pouch <= 0 || r->hand_count >= TJ_MAX_HAND) {
        return; /* nothing to pull, or the fan is full */
    }
    int c = (r->forced_pull_tile >= 0) ? r->forced_pull_tile : pick_field_card(r->circle); /* FTUE forces a known tile */
    if (c < 0) {
        return;
    }
    r->hand_cards[r->hand_count++] = c;
    r->pouch--;
    log_event("card_gain", &(tj_log_ctx_t){.tile = g_config.tiles[c].name});
}
