#ifndef TJ_SIM_H
#define TJ_SIM_H

/* Run simulation: one heir's life. Hero auto-walks the ring; each cell's tile
 * fires (rewards + stat check); Силы to 0 = death. All numbers from g_config. */

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define TJ_MAX_PATH 32
#define TJ_MAX_GLOBAL 8 /* passive (global-scope) desert objects active per circle */
#define TJ_NO_SLOT 0xFF /* slot_g* sentinel: this road cell has no build slot */

/* Run phase: a new heir first leaves the aul, steps onto the road, then loops. */
typedef enum {
    TJ_PHASE_AUL_EXIT = 0, /* walking from the aul out to the road (no loop tick) */
    TJ_PHASE_ROAD_ENTRY,   /* standing on the first road cell */
    TJ_PHASE_WALK,         /* normal auto-walk around the loop */
} tj_phase_t;

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
    bool choosing; /* end-of-circle: paused, player picks 1 of 3 cards */
    int choice[3]; /* offered tile indices while choosing */
    int supplies, wisdom, glory;
    int tile_at[TJ_MAX_PATH];  /* road tile on cell (-1 empty); MVP: scripted/debug only */
    int roadside[TJ_MAX_PATH]; /* player-placed roadside tile affecting cell (-1 empty) */
    int hand;                  /* tile the player holds, ready to place (-1 = none) */
    bool alive;
    bool won;
    /* Loop geometry as data (sim owns it, view only renders). A winding closed
     * loop of cells around the central aul; varies per circle/heir. */
    int grid_cols, grid_rows;         /* play-zone dimensions in cells */
    int aul_x0, aul_y0, aul_w, aul_h; /* reserved aul rect (cells), enclosed by the loop */
    uint8_t path_gx[TJ_MAX_PATH];     /* loop cell x in walk order */
    uint8_t path_gy[TJ_MAX_PATH];     /* loop cell y in walk order */
    uint8_t slot_gx[TJ_MAX_PATH];     /* build-slot cell x for road cell i (TJ_NO_SLOT = none) */
    uint8_t slot_gy[TJ_MAX_PATH];     /* build-slot cell y for road cell i (TJ_NO_SLOT = none) */
    /* Passive (global-scope) desert objects: applied once per circle, drawn in field. */
    int global_tile[TJ_MAX_GLOBAL];
    uint8_t global_gx[TJ_MAX_GLOBAL];
    uint8_t global_gy[TJ_MAX_GLOBAL];
    int global_count;
    char last_event[96];
} tj_run_t;

void tj_run_start(tj_run_t *r, int heir_index);
void tj_run_place_tile(tj_run_t *r, int cell, int tile_index);
/* Place the held card (hand) into the roadside slot of `slot`. Returns false if
 * out of range, hand empty, or the slot is taken. */
bool tj_run_place_roadside(tj_run_t *r, int slot);
/* End-of-circle choice: take offered card `idx` (0..2) into hand, resume the run. */
void tj_run_choose_card(tj_run_t *r, int idx);
void tj_run_tick(tj_run_t *r, float dt);
int tj_hero_stat(const tj_run_t *r, tj_stat_t s);

#endif /* TJ_SIM_H */
