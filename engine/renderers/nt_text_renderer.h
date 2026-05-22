#ifndef NT_TEXT_RENDERER_H
#define NT_TEXT_RENDERER_H

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "material/nt_material.h"

/* ---- Compile-time limits ---- */

#ifndef NT_TEXT_RENDERER_MAX_GLYPHS
#define NT_TEXT_RENDERER_MAX_GLYPHS 4096
#endif

#define NT_TEXT_RENDERER_MAX_VERTICES (NT_TEXT_RENDERER_MAX_GLYPHS * 4)
#define NT_TEXT_RENDERER_MAX_INDICES (NT_TEXT_RENDERER_MAX_GLYPHS * 6)

/* uint16 index buffer: base = glyph_index * 4, must not overflow */
_Static_assert(NT_TEXT_RENDERER_MAX_GLYPHS <= 16383, "NT_TEXT_RENDERER_MAX_GLYPHS > 16383 overflows uint16 index buffer");

void nt_text_renderer_init(void);
void nt_text_renderer_shutdown(void);
void nt_text_renderer_restore_gpu(void);

/* Both setters auto-flush staging on change. */
void nt_text_renderer_set_material(nt_material_t mat);
void nt_text_renderer_set_font(nt_font_t font);

/* NULL/len=0 → no-op. UTF-8 cut at `len` boundary → trailing partial
 * codepoint dropped (no over-read past utf8+len).
 *
 * letter_tracking: EXTRA px between glyphs (additive, NOT absolute).
 *   0 = font's natural advance. Positive = loose, negative = tight.
 * line_leading:    EXTRA px between lines on \n (additive, NOT absolute).
 *   0 = font's natural line advance. Positive = loose, negative = tight. */
void nt_text_renderer_draw_n(const char *utf8, size_t len, const float model[16], float size, const float color[4], float letter_tracking, float line_leading);
void nt_text_renderer_draw(const char *utf8, const float model[16], float size, const float color[4], float letter_tracking, float line_leading);

void nt_text_renderer_flush(void);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_text_renderer_test_vertex_count(void);
uint32_t nt_text_renderer_test_glyph_count(void);
const void *nt_text_renderer_test_vertices(void);
bool nt_text_renderer_test_initialized(void);
/* Count every entry into the setter (not only state changes) — lets tests
 * prove nt_stats_draw calls them unconditionally each frame. */
uint32_t nt_text_renderer_test_set_material_calls(void);
uint32_t nt_text_renderer_test_set_font_calls(void);
void nt_text_renderer_test_reset_call_counters(void);
#endif
// #endregion

#endif /* NT_TEXT_RENDERER_H */
