#ifndef NT_DEBUG_OVERLAY_H
#define NT_DEBUG_OVERLAY_H

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "material/nt_material.h"

/* ---- Lifecycle ---- */

void nt_debug_overlay_init(void);
void nt_debug_overlay_shutdown(void);

/* ---- Format multi-line stats string ----
 * Returns bytes written (excluding trailing NUL); truncates snprintf-style if `size` is
 * too small; buf stays NUL-terminated. */
uint32_t nt_debug_overlay_format_lines(char *buf, uint32_t size);

/* ---- Convenience: format + draw via nt_text_renderer ---- */
void nt_debug_overlay_draw(nt_material_t material, nt_font_t font, const float model[16], float size, uint32_t color);

#endif /* NT_DEBUG_OVERLAY_H */
