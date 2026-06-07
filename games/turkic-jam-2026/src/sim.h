#ifndef TJ_SIM_H
#define TJ_SIM_H

/* Run simulation: one heir's life. Hero auto-walks the ring; each cell's tile
 * fires (rewards + stat check); Силы to 0 = death. All numbers from g_config. */

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define TJ_MAX_PATH 48  /* road loop cells; band road is small but allow bend headroom */
#define TJ_MAX_GLOBAL 8 /* passive (global-scope) desert objects active per circle */
#define TJ_MAX_PACKS 4  /* unopened reward packs that can queue up (hero never waits) */
#define TJ_ZONE_MAX 20  /* max play-zone dimension in cells (aul + 2*(road band + field band)) */
#define TJ_ZONE_CELLS (TJ_ZONE_MAX * TJ_ZONE_MAX)
#define TJ_MAX_BUILD 96 /* capped debug list; real buildability is a distance predicate */
#define TJ_MAX_HAND 16  /* cards held in the fan at once */
#define TJ_NO_SLOT 0xFF /* slot_g* sentinel: this road cell has no build slot */

/* Run phase: a new heir waits at the campfire, is sent off, leaves the aul, steps
 * onto the road, then loops. AUL_READY is the player-gated launch (FTUE intro). */
typedef enum {
    TJ_PHASE_AUL_READY = 0, /* hero waits at the campfire; player presses "Отправить путника" to begin */
    TJ_PHASE_AUL_EXIT,      /* walking from the aul out to the road (no loop tick) */
    TJ_PHASE_ROAD_ENTRY,    /* standing on the first road cell */
    TJ_PHASE_WALK,          /* normal auto-walk around the loop */
} tj_phase_t;

/* Per road-cell rhythm role (Capybara-Go cadence): no dead cells. */
typedef enum {
    TJ_CELL_TRAIL = 0, /* minor pickup, no dead air */
    TJ_CELL_FIGHT,     /* regular enemy auto-battle */
    TJ_CELL_EVENT,     /* dice event (stub reward for now) */
    TJ_CELL_ELITE,     /* mid-lap tougher enemy */
    TJ_CELL_BOSS,      /* last cell of the lap */
    TJ_CELL_REST,      /* pre-boss heal */
} tj_cell_role_t;

typedef struct {
    int heir_index;
    tj_phase_t phase;
    float intro_t; /* seconds elapsed in the current intro phase */
    int body, mind, spirit;
    int stamina;
    int circle;     /* 1-based */
    int cell;       /* 0-based, current cell on the loop */
    int day;        /* 1-based: each cell-to-cell step is a day of travel */
    int path_cells; /* cells in this circle's loop (varies per circle/heir) */
    float move_t;
    float storm_t; /* sandstorm transition timer: hides the new circle's path reshuffle */
    /* Reward packs (idle-friendly): a circle grants a pack; the hero keeps walking.
     * The player opens a pack and picks 1 of 3 cards whenever they like. */
    int packs;                       /* unopened packs queued */
    int pack_offer[TJ_MAX_PACKS][3]; /* 3 tile choices per queued pack */
    bool pack_open;                  /* the front pack's chooser is open */
    int supplies, wisdom, glory;
    int tile_at[TJ_MAX_PATH];       /* enemy tile on cell (-1 none); reshuffled per circle */
    uint8_t cell_role[TJ_MAX_PATH]; /* tj_cell_role_t per cell: trail/fight/event/elite/boss */
    /* Persistent player builds in the desert (gy*grid_cols+gx; -1 empty). The field
     * survives circles; only the road re-routes around it each circle. */
    int field_tile[TJ_ZONE_CELLS];
    int hand_cards[TJ_MAX_HAND]; /* the fan of held cards; drag one onto a field cell */
    int hand_count;
    int pouch;            /* pending cards to pull (the "мешочек"): click it to draw into the fan */
    int merges_done;      /* lifetime merges this run (FTUE gate + juice triggers) */
    int forced_pull_tile; /* FTUE: if >=0, the pouch draws this tile id instead of a random one */
    int fx_cell;          /* field cell index with an active place/merge pop (-1 none) */
    float fx_cell_t;      /* pop timer (counts down) */
    float fx_cell_mag;    /* pop magnitude (place small, merge big) */
    bool alive;
    bool won;
    /* Auto-combat: hero pauses on an enemy road cell and trades blows (ATB by speed). */
    bool in_combat;
    int combat_tile; /* tile index of the enemy being fought */
    int combat_enemy_hp, combat_enemy_max, combat_enemy_atk;
    float combat_enemy_interval;   /* enemy attack interval (archetype-dependent) */
    char combat_label[40];         /* "Босс-Толстяк" etc. for the fight readout */
    int fx_hero_dmg, fx_enemy_dmg; /* last damage to each side (floating numbers) */
    float fx_hero_t, fx_enemy_t;   /* fade timers for the damage numbers */
    /* Timed dice-event reveal (animated; walk paused). Result is precomputed. */
    bool in_event;
    float event_t;
    int ev_die, ev_roll, ev_stat, ev_dc, ev_gain;
    bool ev_pass;
    char ev_name[24], ev_statname[24];
    float death_t;                             /* run-over overlay animation timer (advanced by the scene) */
    float hero_atk_t, enemy_atk_t;             /* per-side attack timers */
    int stamina_max;                           /* current max HP (base + Выносливость buildings) */
    int base_max;                              /* HP max without field buildings */
    int bonus_force, bonus_speed, bonus_vigor; /* live combat bonuses from placed field buildings */
    /* Loop geometry as data (sim owns it, view only renders). A winding closed
     * loop of cells around the central aul; varies per circle/heir. */
    int grid_cols, grid_rows;         /* play-zone dimensions in cells */
    int aul_x0, aul_y0, aul_w, aul_h; /* reserved aul rect (cells), enclosed by the loop */
    uint8_t path_gx[TJ_MAX_PATH];     /* loop cell x in walk order */
    uint8_t path_gy[TJ_MAX_PATH];     /* loop cell y in walk order */
    /* Buildable field cells this circle: the open desert away from the road (a
     * 1-cell no-build buffer separates them from the winding road). */
    uint8_t build_gx[TJ_MAX_BUILD];
    uint8_t build_gy[TJ_MAX_BUILD];
    int build_count;
    /* Passive (global-scope) desert objects: applied once per circle, drawn in field. */
    int global_tile[TJ_MAX_GLOBAL];
    uint8_t global_gx[TJ_MAX_GLOBAL];
    uint8_t global_gy[TJ_MAX_GLOBAL];
    int global_count;
    int tamga_cell;                /* loop cell holding an ancestor's Last Tamga (-1 = none) */
    int tamga_wisdom, tamga_glory; /* reward when the hero reaches tamga_cell */
    char last_event[96];
} tj_run_t;

void tj_run_start(tj_run_t *r, int heir_index);
/* Send the waiting hero out of the aul (AUL_READY -> AUL_EXIT). The FTUE intro and
 * the aul "Отправить путника" button call this; no-op if not waiting. */
void tj_run_send_wayfarer(tj_run_t *r);
void tj_run_place_tile(tj_run_t *r, int cell, int tile_index);
/* Place the held card into the field cell (gx,gy). Persists across circles.
 * Returns false if hand empty, out of zone, on road/aul, or the cell is taken. */
bool tj_run_place_field(tj_run_t *r, int gx, int gy);
/* Place hand card `hand_idx` into the field cell (gx,gy); removes it from the fan,
 * runs merge, recomputes bonuses. Returns false if invalid index or cell. */
bool tj_run_place_card(tj_run_t *r, int hand_idx, int gx, int gy);
/* Move support: lift a placed building back into an empty hand (then re-place it
 * elsewhere to line up a merge). Returns false if hand is full or the cell is empty. */
bool tj_run_pickup_field(tj_run_t *r, int gx, int gy);
/* Pull one card from the pouch into an empty hand (drop-floor tier by circle). */
void tj_run_pull_pouch(tj_run_t *r);
/* Chebyshev distance from (gx,gy) to the aul rect (0 = on/inside the aul). Defines
 * the concentric bands: 1..road_band = road band (no build), beyond = field. */
int tj_run_dist_to_aul(const tj_run_t *r, int gx, int gy);
/* True if a card may be placed here: in the field band (dist > road_band), not on
 * the road, and empty. Single source of truth for placement + build hints. */
bool tj_run_cell_buildable(const tj_run_t *r, int gx, int gy);
/* Open the front reward pack (shows its 3 cards). No-op if no packs queued. */
void tj_run_open_pack(tj_run_t *r);
/* Take card `idx` (0..2) from the opened pack into hand; pops the pack. */
void tj_run_choose_card(tj_run_t *r, int idx);
void tj_run_tick(tj_run_t *r, float dt);
int tj_hero_stat(const tj_run_t *r, tj_stat_t s);

#endif /* TJ_SIM_H */
