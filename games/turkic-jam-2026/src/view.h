#ifndef TJ_VIEW_H
#define TJ_VIEW_H

/* Render systems. Each reads state (run / config / journal) and emits draw
 * calls — no game logic here. This is the swap seam: debug shapes now, real
 * sprites + animations later, without touching sim or scenes. */

#include "game.h"
#include "sim.h"

void tj_view_hud(game_ctx_t *g, const tj_run_t *run); /* circle + resources */
void tj_view_map(game_ctx_t *g, tj_run_t *run);       /* aul + road + clickable slots + hero */
void tj_view_journal(game_ctx_t *g, int max_lines);   /* event log (letопись) */

#endif /* TJ_VIEW_H */
