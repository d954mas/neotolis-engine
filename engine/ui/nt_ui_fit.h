#ifndef NT_UI_FIT_H
#define NT_UI_FIT_H

/* nt_ui_fit: pre-Clay auto-shrink helpers. Game calls fit_* BEFORE
 * CLAY_TEXT to pick the largest font_size in [min, max] that fits.
 * Never grows past max (iOS/UGUI convention). */

#include <stdint.h>

#include "ui/nt_ui.h" /* nt_ui_context_t */

/* Single-line width fit. Returns size_min if even min overflows (caller
 * decides ellipsize/scroll/accept). letter_tracking matches Clay's letterSpacing. */
uint16_t nt_ui_fit_width(nt_ui_context_t *ctx, uint16_t font_id, const char *text, float container_w, uint16_t size_min, uint16_t size_max, float letter_tracking);

/* Word-wrap box fit. Wraps on whitespace (Clay CLAY_TEXT_WRAP_WORDS); line_height=0
 * uses font's natural metrics. Binary search ~log2(range), measure-cached. */
uint16_t nt_ui_fit_box(nt_ui_context_t *ctx, uint16_t font_id, const char *text, float container_w, float container_h, uint16_t size_min, uint16_t size_max, float letter_tracking,
                       uint16_t line_height);

#endif /* NT_UI_FIT_H */
