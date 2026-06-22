#ifndef NT_DEBUG_OVERLAY_H
#define NT_DEBUG_OVERLAY_H

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "material/nt_material.h"

/* Display config only. The overlay is a pure consumer of nt_metrics — fps/cpu/gpu/draws + user
   counters all live in nt_metrics now, so the overlay stores no window/capacity of its own. The desc
   is kept (init still takes one) but carries no live knobs after the inversion; `reserved` exists only
   to keep the struct non-empty for future config. */
typedef struct {
    uint16_t reserved;
} nt_debug_overlay_desc_t;

static inline nt_debug_overlay_desc_t nt_debug_overlay_desc_defaults(void) { return (nt_debug_overlay_desc_t){0}; }

/* ---- Lifecycle ---- */

nt_result_t nt_debug_overlay_init(const nt_debug_overlay_desc_t *desc);
void nt_debug_overlay_shutdown(void);

/* ---- Format multi-line stats string ----
 * Builds its text from nt_metrics: nt_metrics_fps(), the last frame's cpu/gpu/draws via
 * nt_metrics_last(), and the user counters via nt_metrics_user_count()/_user_get() (int
 * counters printed exactly from the uint64 value, floats with decimals).
 * Returns bytes written (not including trailing NUL). buf is NUL-terminated.
 * If `size` is too small, output is truncated (snprintf-style) and the
 * returned value is the bytes actually written.
 */
uint32_t nt_debug_overlay_format_lines(char *buf, uint32_t size);

/* ---- Convenience: format + draw via nt_text_renderer ----
 * Explicitly calls nt_text_renderer_set_material AND nt_text_renderer_set_font
 * before draw to defeat the change-detection early-out so the overlay always
 * binds correctly regardless of prior frame state. */
void nt_debug_overlay_draw(nt_material_t material, nt_font_t font, const float model[16], float size, const float color[4]);

#endif /* NT_DEBUG_OVERLAY_H */
