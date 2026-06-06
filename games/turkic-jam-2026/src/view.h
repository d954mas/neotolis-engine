#ifndef TJ_VIEW_H
#define TJ_VIEW_H

/* Render systems. Each reads state (run / config / journal) and emits draw
 * calls — no game logic here. This is the swap seam: debug shapes now, real
 * sprites + animations later, without touching sim or scenes. */

#include "game.h"
#include "sim.h"

/* Full-screen game layout zones (Loop Hero / Capybara-style frame). */
void tj_view_top_hud(game_ctx_t *g, const tj_run_t *run);         /* top bar: resources, circle, Силы */
void tj_view_log(game_ctx_t *g, int max_lines);                   /* left chat/combat log panel */
void tj_view_register_world(game_ctx_t *g);                       /* register the CUSTOM render handler that draws the map world (call once, after UI ctx) */
void tj_view_map(game_ctx_t *g, tj_run_t *run);                   /* center: viewport custom element + on-top overlays (Clay) */
void tj_view_world_pan(float dx, float dy);                       /* pan the map camera (world px), clamped */
bool tj_view_world_cell_at(float lx, float ly, int *gx, int *gy); /* logical point -> grid cell; false if outside */
void tj_view_pack_overlay(game_ctx_t *g, tj_run_t *run);          /* reward-chooser modal; call at scene root, above the clipped map */
void tj_view_hero_panel(game_ctx_t *g, const tj_run_t *run);      /* right: doll, stats, current cell */
void tj_view_card_hand(game_ctx_t *g, tj_run_t *run);             /* bottom: held card + reward pack chooser */

#endif /* TJ_VIEW_H */
