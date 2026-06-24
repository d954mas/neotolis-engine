#ifndef NT_DEBUG_OVERLAY_H
#define NT_DEBUG_OVERLAY_H

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "material/nt_material.h"

/* Display config only; perf data lives in nt_metrics. `reserved` keeps the struct non-empty. */
typedef struct {
    uint16_t reserved;
} nt_debug_overlay_desc_t;

static inline nt_debug_overlay_desc_t nt_debug_overlay_desc_defaults(void) { return (nt_debug_overlay_desc_t){0}; }

/* ---- Lifecycle ---- */

nt_result_t nt_debug_overlay_init(const nt_debug_overlay_desc_t *desc);
void nt_debug_overlay_shutdown(void);

/* ---- Format multi-line stats string ----
 * Returns bytes written (excluding trailing NUL); truncates snprintf-style if `size` is
 * too small; buf stays NUL-terminated. */
uint32_t nt_debug_overlay_format_lines(char *buf, uint32_t size);

/* ---- Convenience: format + draw via nt_text_renderer ---- */
void nt_debug_overlay_draw(nt_material_t material, nt_font_t font, const float model[16], float size, const float color[4]);

#endif /* NT_DEBUG_OVERLAY_H */
