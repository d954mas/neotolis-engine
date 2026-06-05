#ifndef TJ_SIM_H
#define TJ_SIM_H

/* Run simulation: one heir's life. Hero auto-walks the ring; each cell's tile
 * fires (rewards + stat check); Силы to 0 = death. All numbers from g_config. */

#include <stdbool.h>

#include "config.h"

#define TJ_MAX_PATH 32

typedef struct {
    int heir_index;
    int body, mind, spirit;
    int stamina;
    int circle;     /* 1-based */
    int cell;       /* 0-based, current cell on the ring */
    int path_cells; /* cells in this circle's ring (varies per circle/heir) */
    float move_t;
    int supplies, wisdom, glory;
    int tile_at[TJ_MAX_PATH];  /* road tile on cell (-1 empty); MVP: scripted/debug only */
    int roadside[TJ_MAX_PATH]; /* player-placed roadside tile affecting cell (-1 empty) */
    int hand;                  /* tile the player holds, ready to place (-1 = none) */
    bool alive;
    bool won;
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
