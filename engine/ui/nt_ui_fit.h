#ifndef NT_UI_FIT_H
#define NT_UI_FIT_H

/* nt_ui_fit: pre-Clay auto-shrink helpers for text inside known-dimension
 * containers. Game calls fit_* BEFORE declaring CLAY_TEXT (or nt_ui_label)
 * to compute the largest font_size from [size_min, size_max] at which the
 * text fits the container. Returns size_max when the text already fits;
 * never grows beyond size_max (matches iOS/UGUI auto-shrink convention). */

#include <stdint.h>

#include "ui/nt_ui.h" /* nt_ui_context_t */

/* Single-line width fit. Returns the largest size in [size_min, size_max]
 * at which the measured text width stays <= container_w.
 *
 * If even size_min overflows, returns size_min (text will visibly clip --
 * caller's choice whether to ellipsize, scroll, or accept overflow).
 *
 * letter_tracking is the per-glyph extra spacing applied at every size
 * (matches nt_ui_label_style_t.letter_tracking + Clay's letterSpacing). */
uint16_t nt_ui_fit_width(nt_ui_context_t *ctx, uint16_t font_id, const char *text, float container_w, uint16_t size_min, uint16_t size_max, float letter_tracking);

/* Wrap-box fit. Returns the largest size in [size_min, size_max] at which
 * the text, word-wrapped to container_w, fits within container_h.
 *
 * Wraps on whitespace (spaces, tabs, newlines), matching Clay's default
 * CLAY_TEXT_WRAP_WORDS. line_height in CSS-like units; 0 = use font's
 * natural ascent+descent+linegap.
 *
 * Performs binary search over [size_min, size_max] (~log2(range)
 * iterations), each iteration does word-by-word measure with the font's
 * measure cache. For typical 20-word text at range 14-44: ~120 measure
 * calls, mostly cache hits after first iter. */
uint16_t nt_ui_fit_box(nt_ui_context_t *ctx, uint16_t font_id, const char *text, float container_w, float container_h, uint16_t size_min, uint16_t size_max, float letter_tracking,
                       uint16_t line_height);

#endif /* NT_UI_FIT_H */
