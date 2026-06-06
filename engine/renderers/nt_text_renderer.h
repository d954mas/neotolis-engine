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

/* Default for the slug_text `u_alpha_cutoff.x` param: discards only fully-empty glyph-quad pixels.
 * World-space text that writes depth should raise this (~0.5) so AA edges don't write depth and halo. */
#define NT_TEXT_ALPHA_CUTOFF_DEFAULT (1.0F / 255.0F)

void nt_text_renderer_init(void);
void nt_text_renderer_shutdown(void);
void nt_text_renderer_restore_gpu(void);

/* The bound material uses the slug_text vs/fs, ALPHA blend, cull NONE. The alpha-clip is OPT-IN:
 * the renderer binds only the params the material declares (it never injects a default), so declaring
 *     .params[0] = {.name = "u_alpha_cutoff", .value = {NT_TEXT_ALPHA_CUTOFF_DEFAULT}}, .param_count = 1,
 * enables the frag's `discard coverage < u_alpha_cutoff.x`. Omit it and the uniform stays 0 (GL-spec
 * default for an unset uniform) → no discard. Omitting is a valid choice, not an error; depth-writing
 * world-space text that wants clean AA edges should declare it. Both setters auto-flush on change. */
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

/* Per-glyph clip-space depth bias toward the near plane — the VS does gl_Position.z -= bias * w, NOT a
 * world/model-space +Z offset. With depth_write, coplanar glyph quads z-fight at overlapping AA fringes;
 * a small per-glyph bias separates them by draw order. Signed. 0 (default) = off. Persists until changed
 * (kept across restore_gpu, cleared on cold init/shutdown). */
void nt_text_renderer_set_glyph_depth_bias(float bias_per_glyph);

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
/* Last model matrix passed to draw_n (captured even when font is empty / units_per_em=0).
 * Lets tests pin nt_ui's emit_text mat4 construction without needing a real font. */
const float *nt_text_renderer_test_last_model(void);
uint32_t nt_text_renderer_test_draw_n_calls(void);
float nt_text_renderer_test_glyph_depth_bias(void);
#endif
// #endregion

#endif /* NT_TEXT_RENDERER_H */
