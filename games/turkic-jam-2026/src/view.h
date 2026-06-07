#ifndef TJ_VIEW_H
#define TJ_VIEW_H

/* Render systems. Each reads state (run / config / journal) and emits draw
 * calls — no game logic here. This is the swap seam: debug shapes now, real
 * sprites + animations later, without touching sim or scenes. */

#include "game.h"
#include "sim.h"

/* Full-screen game layout zones (Loop Hero / Capybara-style frame). */
void tj_view_top_hud(game_ctx_t *g, const tj_run_t *run); /* top bar: resources, circle, Силы */
void tj_view_log(game_ctx_t *g, int max_lines);           /* left chat/combat log panel */
void tj_view_register_world(game_ctx_t *g);
void tj_view_set_ftue_gap(int gx, int gy);     /* FTUE: highlight the merge-gap cell (-1,-1 clears) */
void tj_view_focus_ftue_merge(int gx, int gy); /* FTUE: pan the camera to frame the merge trio comfortably */ /* register the CUSTOM render handler that draws the map world (call once, after UI ctx) */
void tj_view_map(game_ctx_t *g, tj_run_t *run);                                                    /* center: viewport custom element + on-top overlays (Clay) */
void tj_view_world_pan(float dx, float dy);                                                        /* pan the map camera (world px), clamped */
bool tj_view_world_cell_at(float lx, float ly, int *gx, int *gy);                                  /* logical point -> grid cell; false if outside */
void tj_view_pack_overlay(game_ctx_t *g, tj_run_t *run);                                           /* reward-chooser modal; call at scene root, above the clipped map */
void tj_view_action_overlay(game_ctx_t *g, const tj_run_t *run);                                   /* centered dice-event window (combat now lives in the hero panel) */
void tj_view_battle_tick(game_ctx_t *g, const tj_run_t *run, float dt);                            /* advance the combat-stage particles/shake; spawn sparks on hits + a victory burst */
void tj_view_hero_panel(game_ctx_t *g, const tj_run_t *run);                                       /* right: hero name, battle stage (hero vs enemy), stats, current cell */
bool tj_view_death_panel(game_ctx_t *g, const tj_run_t *run);                                      /* run-over step 1: verdict + sand line + result (true once acknowledged) */
bool tj_view_aul_panel(game_ctx_t *g, const tj_run_t *run);                                        /* run-over step 2: aul supplies + upgrades (true on "Отправить путника") */
void tj_view_death_overlay(game_ctx_t *g, const tj_run_t *run);                                    /* DEPRECATED fullscreen run-over veil (no longer called; run-over lives in the panel) */
void tj_view_card_hand(game_ctx_t *g, tj_run_t *run, int drag_idx, bool tutorial);                 /* bottom: pouch + fan of cards (drag_idx drawn at cursor); tutorial hides the stock hint */
int tj_view_hand_index_at(const game_ctx_t *g, const tj_run_t *run, float lx, float ly);           /* hand card under a logical point, or -1 */
void tj_view_drag_overlay(game_ctx_t *g, const tj_run_t *run, int drag_idx);                       /* dragged card + dotted targeting arrow + merge "+N" badge */
void tj_view_field_badges(game_ctx_t *g, const tj_run_t *run);                                     /* per-building level numbers + max-tier crowns (Clay, over the map) */
void tj_view_field_tooltip(game_ctx_t *g, const tj_run_t *run);                                    /* hover tooltip: building name + level + effect (when not dragging) */
bool tj_view_help_button(game_ctx_t *g);                                                           /* "?" button top-right; true on click */
bool tj_view_help_modal(game_ctx_t *g);                                                            /* how-to modal; true when closed */
int tj_view_ftue_overlay(game_ctx_t *g, const tj_run_t *run, int step, float t);                   /* first-run tutorial; 1=advance 2=skip 0=none */
#define TJ_REVEAL_SECONDS 1.6F                                                                     /* dawn-reveal duration; button cues + tooltip appear after it */
void tj_view_intro_black(game_ctx_t *g, float t);                                                  /* first-run black-screen open UI: lines + tap-to-continue + finger (bg/sand are sprites) */
bool tj_view_launch_panel(game_ctx_t *g, const tj_run_t *run, float t);                            /* first-run right panel: send the wayfarer (finger + glow); true on press */
void tj_view_intro_banner(game_ctx_t *g, const char *text);                                        /* centered on-theme intro line (tooltip / walkout hint) */
bool tj_view_settings_button(game_ctx_t *g);                                                       /* gear button top-right; true on click */
bool tj_view_settings_modal(game_ctx_t *g, float dt);                                              /* settings modal (volumes, language, reset); true when closed */

#endif /* TJ_VIEW_H */
