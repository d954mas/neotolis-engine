#ifndef TJ_SIM_H
#define TJ_SIM_H

/* Run simulation: one heir's life. Hero auto-walks the ring; each cell's tile
 * fires (rewards + stat check); Силы to 0 = death. All numbers from g_config. */

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define TJ_MAX_PATH 32
#define TJ_NO_SLOT 0xFF /* slot_g* sentinel: this road cell has no build slot */

typedef struct {
    int heir_index;
    int body, mind, spirit;
    int stamina;
    int circle;     /* 1-based */
    int cell;       /* 0-based, current cell on the loop */
    int path_cells; /* cells in this circle's loop (varies per circle/heir) */
    float move_t;
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
    char last_event[96];
} tj_run_t;

void tj_run_start(tj_run_t *r, int heir_index);
void tj_run_place_tile(tj_run_t *r, int cell, int tile_index);
/* Place the held card (hand) into the roadside slot of `slot`. Returns false if
 * out of range, hand empty, or the slot is taken. */
bool tj_run_place_roadside(tj_run_t *r, int slot);
void tj_run_tick(tj_run_t *r, float dt);
int tj_hero_stat(const tj_run_t *r, tj_stat_t s);

#endif /* TJ_SIM_H */
