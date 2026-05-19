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

/* ---- Lifecycle ---- */

void nt_text_renderer_init(void);
void nt_text_renderer_shutdown(void);
void nt_text_renderer_restore_gpu(void);

/* ---- State setters (auto-flush on change) ---- */

void nt_text_renderer_set_material(nt_material_t mat);
void nt_text_renderer_set_font(nt_font_t font);

/* ---- Draw (adds text to CPU staging buffer, per-draw mat4 pre-transform) ---- */

/* Length-aware variant of nt_text_renderer_draw — accepts non-NUL-terminated
 * buffers (Clay_StringSlice contract). Iterates exactly `len` bytes. No
 * per-call scratch allocation — input string is iterated in place.
 *
 * Edge cases:
 *   - len == 0 or utf8 == NULL → no-op
 *   - UTF-8 multibyte cut by `len` → incomplete trailing codepoint dropped
 *     via NT_UTF8_REJECT recovery; no over-read past utf8 + len.
 *
 * The NUL-terminated nt_text_renderer_draw wraps this with strlen(utf8). */
void nt_text_renderer_draw_n(const char *utf8, size_t len, const float model[16], float size, const float color[4]);

void nt_text_renderer_draw(const char *utf8, const float model[16], float size, const float color[4]);

/* ---- Flush (upload staging buffer + single draw call per flush) ---- */

void nt_text_renderer_flush(void);

/* ---- Test accessors (test builds only) ---- */

#ifdef NT_TEXT_RENDERER_TEST_ACCESS
uint32_t nt_text_renderer_test_vertex_count(void);
uint32_t nt_text_renderer_test_glyph_count(void);
const void *nt_text_renderer_test_vertices(void);
bool nt_text_renderer_test_initialized(void);
/* Counts every entry into set_material / set_font (NOT only state changes).
 * Used by nt_stats Pitfall 9 (Issue 2) test to verify nt_stats_draw always
 * calls both setters explicitly even when the state matches the previous
 * frame, defeating the early-out. */
uint32_t nt_text_renderer_test_set_material_calls(void);
uint32_t nt_text_renderer_test_set_font_calls(void);
void nt_text_renderer_test_reset_call_counters(void);
#endif

#endif /* NT_TEXT_RENDERER_H */
