#ifndef NT_UI_DEBUG_H
#define NT_UI_DEBUG_H

/* Phase 56 ext: hit-zone debug overlay. Per-frame recording of every padded
 * hit-test query (id, padded layout-space bbox, accum-transform snapshot,
 * state flags), plus a drawing helper that emits a semi-transparent quad
 * for the padded zone + outline of the exact visual bbox + a small id label.
 *
 * Zero overhead in production: recording is OFF by default and gated by
 * ctx->debug_recording. Game opts in via nt_ui_debug_set_recording(ctx,true).
 *
 * Drawing is decoupled from recording. The game calls
 * nt_ui_debug_draw_hit_zones(ctx, mode) AFTER nt_ui_walk (same pattern as
 * nt_stats_draw): the engine emits sprite/text commands via the currently
 * bound sprite+text materials and the ctx's atlas white region.
 *
 * Use case: validates the transform-aware hit-test (D-56-07) and the
 * touch-target padding (Phase 56 ext). Filter modes let the developer focus
 * on the currently-interacted widget without screen clutter. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h" /* nt_font_t (typed handle) */

typedef struct nt_ui_context nt_ui_context_t;

/* Filter for nt_ui_debug_draw_hit_zones. */
typedef enum {
    NT_UI_DEBUG_HIT_OFF = 0,      /* draw nothing */
    NT_UI_DEBUG_HIT_HOVER = 1,    /* only zones the primary pointer is currently over */
    NT_UI_DEBUG_HIT_CAPTURED = 2, /* only zones currently captured by a pointer */
    NT_UI_DEBUG_HIT_ALL = 3,      /* every recorded zone */
} nt_ui_debug_hit_mode_t;

/* Toggle per-frame zone recording. Default off (zero overhead).
 * Takes effect immediately; queries between this call and the next
 * nt_ui_begin reset are recorded if true at the time of the query. */
void nt_ui_debug_set_recording(nt_ui_context_t *ctx, bool on);
bool nt_ui_debug_get_recording(const nt_ui_context_t *ctx);

/* Recorded zone count for this frame (cleared each nt_ui_begin). */
uint32_t nt_ui_debug_get_zone_count(const nt_ui_context_t *ctx);

/* Drawing helper. Must be called between begin_pass and end_pass with the
 * sprite + text materials already bound on the ctx (same contract as
 * nt_stats_draw). Emits:
 *   - semi-transparent FILLED polygon for the padded hit zone (color by state)
 *   - thin OUTLINE of the exact visual (unpadded) bbox
 *   - a short text label "id=0xHH state=..." near one corner
 * If the ctx has no sprite/text material or atlas bound, this returns silently.
 * font is the font handle to draw labels with; size in px; pass 0 to skip
 * labels (just rects).
 *
 * Mode = OFF returns without emitting. Drawing zero zones is a no-op.
 * Drawing at-cap is silently saturated (no assertion). */
void nt_ui_debug_draw_hit_zones(nt_ui_context_t *ctx, nt_ui_debug_hit_mode_t mode, nt_font_t font, float label_size);

#endif /* NT_UI_DEBUG_H */
