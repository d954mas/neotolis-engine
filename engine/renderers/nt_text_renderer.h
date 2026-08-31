#ifndef NT_TEXT_RENDERER_H
#define NT_TEXT_RENDERER_H

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "material/nt_material.h"

/* ---- Compile-time limits ---- */

#ifndef NT_TEXT_RENDERER_MAX_GLYPHS
#define NT_TEXT_RENDERER_MAX_GLYPHS 4096
#endif

/* One entry per (program, render state) the frame draws through. UI text
 * switches between a context default and per-style overrides, so a single slot
 * would rebuild a VAO on every switch. */
/* One entry per distinct (program, render state) pair the frame draws through, so
 * blend/depth/cull variants multiply with programs rather than replacing them.
 * Raise it with -DNT_TEXT_RENDERER_MAX_PIPELINES=N; exhaustion asserts, it does
 * not evict. */
#ifndef NT_TEXT_RENDERER_MAX_PIPELINES
#define NT_TEXT_RENDERER_MAX_PIPELINES 8
#endif
/* pipeline_count is uint16_t, as in sprite and mesh. */
_Static_assert(NT_TEXT_RENDERER_MAX_PIPELINES <= 65535, "NT_TEXT_RENDERER_MAX_PIPELINES overflows the uint16 cache counter");

#define NT_TEXT_RENDERER_MAX_VERTICES (NT_TEXT_RENDERER_MAX_GLYPHS * 4)
#define NT_TEXT_RENDERER_MAX_INDICES (NT_TEXT_RENDERER_MAX_GLYPHS * 6)

/* uint16 index buffer: base = glyph_index * 4, must not overflow */
_Static_assert(NT_TEXT_RENDERER_MAX_GLYPHS <= 16383, "NT_TEXT_RENDERER_MAX_GLYPHS > 16383 overflows uint16 index buffer");

/* Default for the slug_text `u_alpha_cutoff.x` param: discards only fully-empty glyph-quad pixels.
 * Good even for depth-writing world text: pair it with a per-glyph depth bias
 * (nt_text_renderer_set_glyph_depth_bias) to separate overlapping glyphs — raising the cutoff would
 * harden AA edges without removing a real halo. */
#define NT_TEXT_ALPHA_CUTOFF_DEFAULT (1.0F / 255.0F)

void nt_text_renderer_init(void);
void nt_text_renderer_shutdown(void);
/* Full reset: drops every queued draw command and every cached pipeline, then
 * rebuilds the GPU-side buffers. */
void nt_text_renderer_restore_gpu(void);

/* Material must use the slug_text vs/fs, a blend compatible with its premultiplied output, and cull NONE. u_alpha_cutoff is an opt-in param:
 * declare it (value = NT_TEXT_ALPHA_CUTOFF_DEFAULT) to enable the frag's coverage discard. Declare it on
 * EVERY material sharing one program, or on none: uniforms live on the program, so a material that omits
 * it inherits whatever the last material on that program wrote (docs/spec/render/architecture.md). Both
 * setters auto-flush staging on change. */
void nt_text_renderer_set_material(nt_material_t mat);
void nt_text_renderer_set_font(nt_font_t font);

/* NULL/len=0 → no-op. UTF-8 cut at `len` boundary → trailing partial
 * codepoint dropped (no over-read past utf8+len).
 * Unavailable font textures skip glyphs and decorations.
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

/* Synthetic-oblique shear for faux-italic: subsequent draws lean in text-local space (x += shear*y about
 * the baseline) so a family with no italic face can still slant. The shear is folded into the model on the
 * CPU per vertex, so it costs no flush and mixes freely within one batch. 0 (default) = upright. Sticky like
 * the depth bias (kept across restore_gpu, cleared on cold init/shutdown) — set it back to 0 when done so it
 * does not leak onto unrelated text. */
void nt_text_renderer_set_oblique(float shear);

/* ---- Sticky decoration state ---- */
/* All five setters mirror set_oblique's lifetime: kept across restore_gpu, cleared on cold init/shutdown,
 * no flush (CPU emit). Float args use a REAL if (!isfinite) guard, NOT an assert — NT_ASSERT is a no-op
 * in shipping and a NaN would poison the offset/quantize math. Call reset_decoration() so state does not leak. */

/* Synthetic weight in em units: subsequent fills emit an emboldened (positive) / thinned (negative)
 * glyph variant via the (codepoint, weight) glyph cache. 0 (default) = the font's natural weight. */
void nt_text_renderer_set_weight(float weight_em);

/* Outline/stroke: subsequent draws emit an extra pass grown by `width` em beyond the fill weight, in
 * `color`, behind the fill (painter order fill on top). width 0 (default) = no outline. */
void nt_text_renderer_set_outline(float width, const float color[4]);

/* Hard drop shadow: subsequent draws emit an extra pass offset by (dx,dy) em in `color` (px = d * size,
 * scales with the text), behind everything, reusing the outline/fill glyph variant (no new cache key).
 * `blur` is stored but UNUSED (hard shadow only). color alpha 0 (default) = no shadow. */
void nt_text_renderer_set_shadow(float dx, float dy, float blur, const float color[4]);

/* Underline / strikethrough: subsequent draws emit one continuous solid quad per line at the font's
 * scaled underline/strike metric. Sticky bools, cleared by reset_decoration. */
void nt_text_renderer_set_underline(bool enabled);
void nt_text_renderer_set_strikethrough(bool enabled);

/* One-shot clear of ALL decoration state (weight, outline, shadow, underline/strike) AND oblique — the
 * single call the UI runs after a decorated run so nothing leaks onto the next. */
void nt_text_renderer_reset_decoration(void);

void nt_text_renderer_flush(void);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_text_renderer_test_vertex_count(void);
uint32_t nt_text_renderer_test_glyph_count(void);
const void *nt_text_renderer_test_vertices(void);
bool nt_text_renderer_test_initialized(void);
/* Count every entry into the setter (not only state changes) — lets tests
 * prove nt_debug_overlay_draw calls them unconditionally each frame. */
uint32_t nt_text_renderer_test_set_material_calls(void);
uint32_t nt_text_renderer_test_set_font_calls(void);
void nt_text_renderer_test_reset_call_counters(void);
/* Flushes that issued a real draw (empty no-op flushes excluded). Reset by reset_call_counters. */
uint32_t nt_text_renderer_test_nonempty_flush_calls(void);
/* Last model matrix passed to draw_n (captured even when font is empty / units_per_em=0).
 * Lets tests pin nt_ui's emit_text mat4 construction without needing a real font. */
const float *nt_text_renderer_test_last_model(void);
uint32_t nt_text_renderer_test_draw_n_calls(void);
float nt_text_renderer_test_glyph_depth_bias(void);
float nt_text_renderer_test_oblique(void);
/* Sticky decoration state accessors — pin the setter lifetime (persist across restore, reset clears). */
float nt_text_renderer_test_weight(void);
float nt_text_renderer_test_outline_width(void);
float nt_text_renderer_test_outline_color_a(void);
float nt_text_renderer_test_shadow_color_a(void);
float nt_text_renderer_test_shadow_dx(void);
bool nt_text_renderer_test_underline(void);
/* Largest oblique observed at a draw_n entry since the last reset_call_counters — pins the
 * SYNTH_ITALIC -> set_oblique wiring through the emit path (stub font emits no glyphs). */
float nt_text_renderer_test_max_oblique(void);
/* Largest weight/outline width and whether underline/strike were observed at a draw_n entry since the
 * last reset_call_counters — pin the SYNTH_BOLD/outline/shadow/underline wiring through the emit path
 * (the UI resets decoration right after the run, so a plain sticky read post-walk sees 0). */
float nt_text_renderer_test_max_weight(void);
float nt_text_renderer_test_max_outline_width(void);
bool nt_text_renderer_test_saw_underline(void);
bool nt_text_renderer_test_saw_strike(void);
/* Currently bound material id (0 = none) — lets tests prove a dispatch path bound a pipeline. */
uint32_t nt_text_renderer_test_material_id(void);
/* Live entries in the pipeline cache — pins the key's identity (one entry per
 * program + render state, not per material). */
uint16_t nt_text_renderer_test_pipeline_cache_count(void);
#endif
// #endregion

#endif /* NT_TEXT_RENDERER_H */
