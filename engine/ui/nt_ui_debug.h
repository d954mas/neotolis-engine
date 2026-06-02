#ifndef NT_UI_DEBUG_H
#define NT_UI_DEBUG_H

/* Hit-zone debug overlay. Per-frame recording of every padded hit-test query
 * (id, padded bbox, accum-transform snapshot, state flags) + a drawing helper.
 * Recording OFF by default; zero overhead in production. Call draw_hit_zones
 * AFTER nt_ui_walk with the same target. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h" /* nt_font_t (typed handle) */
#include "ui/nt_ui.h"     /* nt_ui_target_t (Y-flip + viewport for emit parity with walker) */

typedef struct nt_ui_context nt_ui_context_t;

/* Filter for nt_ui_debug_draw_hit_zones. */
typedef enum {
    NT_UI_DEBUG_HIT_OFF = 0,      /* draw nothing */
    NT_UI_DEBUG_HIT_HOVER = 1,    /* only zones the primary pointer is currently over */
    NT_UI_DEBUG_HIT_CAPTURED = 2, /* only zones currently captured by a pointer */
    NT_UI_DEBUG_HIT_ALL = 3,      /* every recorded zone */
} nt_ui_debug_hit_mode_t;

/* EXPERIMENTAL: API may change in v1.9.
 *
 * Toggle per-frame zone recording. Default off; takes effect immediately. */
void nt_ui_debug_set_recording(nt_ui_context_t *ctx, bool on);
bool nt_ui_debug_get_recording(const nt_ui_context_t *ctx);

/* Recorded zone count for this frame (cleared each nt_ui_begin). */
uint32_t nt_ui_debug_get_zone_count(const nt_ui_context_t *ctx);

/* Record-only push for a DISABLED widget that skipped the hit-test. Zone
 * carries NT_UI_DEBUG_FLAG_DISABLED for overlay surfacing. First-frame Clay
 * miss → no zone recorded (not an assert). pad_lrtb NULL = zero padding. */
void nt_ui_debug_record_disabled_zone(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]);

/* Draw all recorded zones with the walker's Y-flip applied. `target` MUST
 * match the nt_ui_walk target. label_size 0 skips labels. Silent skip when
 * ctx has no materials/atlas bound or mode == OFF. */
void nt_ui_debug_draw_hit_zones(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_ui_debug_hit_mode_t mode, nt_font_t font, float label_size);

#endif /* NT_UI_DEBUG_H */
