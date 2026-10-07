#ifndef NT_TEXT_RENDERER_H
#define NT_TEXT_RENDERER_H

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "material/nt_material.h"

/* ---- Compile-time limits ---- */

/* Caches distinct (program, render state) pairs across frames; live entries persist until reset.
 * Dead entries are removed on insertion. Exhaustion asserts without evicting live entries.
 * Raise capacity with -DNT_TEXT_RENDERER_MAX_PIPELINES=N. */
#ifndef NT_TEXT_RENDERER_MAX_PIPELINES
#define NT_TEXT_RENDERER_MAX_PIPELINES 8
#endif
_Static_assert(NT_TEXT_RENDERER_MAX_PIPELINES <= 65535, "NT_TEXT_RENDERER_MAX_PIPELINES overflows the uint16 cache counter");

/* Synthetic bold weight in em; labels and rich text use the same glyph variant. */
#define NT_TEXT_SYNTH_BOLD_WEIGHT 0.04F

/* Contracts: draws record into frame storage (set nt_gfx_desc_t.frame_capacity VERTEX and INDEX)
 * inside a pass; each draw call records one indexed draw, and gfx merges consecutive draws of one
 * font and material. Uniforms are program state: the renderer skips params it already wrote to a
 * program, so nothing else may write the uniforms of a program text materials use. */

/* Destroys cached pipelines and the vertex input and clears all state. Call it before
 * nt_gfx_shutdown or a gfx re-init: new pools reuse handle ids. */
void nt_text_renderer_shutdown(void);

/* Selects the material for this frame's draws: call it every frame before drawing (asserted).
 * Another set_material replaces it, so a caller selects its own before drawing.
 * Requires an assigned slug_text program, premultiplied-compatible blend and cull NONE.
 * slug_text.frag never discards; a depth-writing material uses slug_text_depth.frag, which discards empty
 * pixels so they do not occlude. Pair it with nt_text_renderer_set_glyph_depth_bias for overlapping glyphs.
 * A text material declares no textures: the font's curve texture is the renderer's own bind (asserted). */
void nt_text_renderer_set_material(nt_material_t mat);
void nt_text_renderer_set_font(nt_font_t font);

/* NULL or len=0 is a no-op; trailing partial UTF-8 codepoints are dropped without reading past utf8+len.
 * Unavailable font textures skip glyphs and decorations. letter_tracking/line_leading add px to natural
 * glyph/newline advances: 0 = natural, positive = looser, negative = tighter. */
void nt_text_renderer_draw_n(const char *utf8, size_t len, const float model[16], float size, uint32_t color, float letter_tracking, float line_leading);
void nt_text_renderer_draw(const char *utf8, const float model[16], float size, uint32_t color, float letter_tracking, float line_leading);

/* Per-glyph clip-space depth bias toward the near plane — the VS does gl_Position.z -= bias * w, NOT a
 * world/model-space +Z offset. With depth_write, coplanar glyph quads z-fight at overlapping AA fringes;
 * a small per-glyph bias separates them by draw order. Signed. 0 (default) = off. Persists until changed;
 * cleared by shutdown. */
void nt_text_renderer_set_glyph_depth_bias(float bias_per_glyph);

/* Synthetic-oblique shear for faux-italic: subsequent draws lean in text-local space (x += shear*y about
 * the baseline) so a family with no italic face can still slant. The shear is folded into the model on the
 * CPU per vertex. 0 (default) = upright. Sticky like the depth bias — set it back to 0 when done so it
 * does not leak onto unrelated text. */
void nt_text_renderer_set_oblique(float shear);

/* ---- Sticky decoration state ---- */
/* All five setters persist until reset_decoration or shutdown.
 * Non-finite inputs are rejected even with asserts disabled to protect offset/quantize math.
 * Call reset_decoration() so state does not leak. */

/* Synthetic weight in em units: subsequent fills emit an emboldened (positive) / thinned (negative)
 * glyph variant via the (codepoint, weight) glyph cache. 0 (default) = the font's natural weight. */
void nt_text_renderer_set_weight(float weight_em);

/* Outline/stroke: subsequent draws emit an extra pass grown by `width` em beyond the fill weight, in
 * `color`, behind the fill (painter order fill on top). width 0 (default) or color alpha 0 = no outline. */
void nt_text_renderer_set_outline(float width, uint32_t color);

/* Hard drop shadow: subsequent draws emit an extra pass offset by (dx,dy) em in `color` (px = d * size,
 * scales with the text), behind everything, reusing the outline/fill glyph variant (no new cache key).
 * `blur` is stored but UNUSED (hard shadow only). color alpha 0 (default) = no shadow. */
void nt_text_renderer_set_shadow(float dx, float dy, float blur, uint32_t color);

/* Underline / strikethrough: subsequent draws emit one continuous solid quad per line at the font's
 * scaled underline/strike metric. Sticky bools, cleared by reset_decoration. */
void nt_text_renderer_set_underline(bool enabled);
void nt_text_renderer_set_strikethrough(bool enabled);

/* One-shot clear of ALL decoration state (weight, outline, shadow, underline/strike) AND oblique — the
 * single call the UI runs after a decorated run so nothing leaks onto the next. */
void nt_text_renderer_reset_decoration(void);

// #region test_access
#ifdef NT_TEST_ACCESS
/* Count every entry into set_font (not only state changes). */
uint32_t nt_text_renderer_test_set_font_calls(void);
void nt_text_renderer_test_reset_call_counters(void);
/* Last model matrix passed to draw_n (captured even when font is empty / units_per_em=0).
 * Lets tests pin nt_ui's emit_text mat4 construction without needing a real font. */
const float *nt_text_renderer_test_last_model(void);
uint32_t nt_text_renderer_test_draw_n_calls(void);
float nt_text_renderer_test_glyph_depth_bias(void);
float nt_text_renderer_test_oblique(void);
/* Sticky decoration state accessors — pin the setter lifetime (reset clears). */
float nt_text_renderer_test_weight(void);
float nt_text_renderer_test_outline_width(void);
uint32_t nt_text_renderer_test_outline_color(void);
uint32_t nt_text_renderer_test_shadow_color(void);
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
/* Selected material id (0 = none); selection does not imply a pipeline is ready. */
uint32_t nt_text_renderer_test_material_id(void);
/* Occupied cache entries, including dead pipelines not yet removed by insertion/reset. */
uint16_t nt_text_renderer_test_pipeline_cache_count(void);
#endif
// #endregion

#endif /* NT_TEXT_RENDERER_H */
