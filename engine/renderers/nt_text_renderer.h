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

/* Everything that shapes one draw's vertices. Zero-initialized = plain text: every decoration off.
 * Every float must be finite. */
typedef struct {
    nt_font_t font;
    float size;            /* px per em */
    uint32_t color;        /* RGBA8 */
    float letter_tracking; /* px added to each glyph advance: 0 = natural, negative = tighter */
    float line_leading;    /* px added to each newline advance */
    /* Synthetic weight in em: fills use an emboldened (positive) or thinned (negative) glyph variant.
     * Weight and outline need NT_FONT_EMBOLDEN_ENABLED=ON unless 0. */
    float weight_em;
    float outline_w;        /* outline pass grown by this em beyond the fill weight, behind the fill */
    uint32_t outline_color; /* alpha 0 = no outline */
    float shadow_dx;        /* hard drop shadow offset in em (px = d * size), behind everything */
    float shadow_dy;
    uint32_t shadow_color; /* alpha 0 = no shadow */
    /* Faux-italic shear in text-local space (x += oblique * y about the baseline), folded into the
     * model on the CPU, so a family with no italic face can still slant. */
    float oblique;
    /* Per-glyph clip-space depth bias toward the near plane — the VS does gl_Position.z -= bias * w,
     * NOT a world/model-space offset. With depth_write, coplanar glyph quads z-fight at overlapping
     * AA fringes; a small bias separates them by draw order. Signed. */
    float glyph_depth_bias;
    bool underline;     /* one continuous solid quad per line at the font's underline metric */
    bool strikethrough; /* same at the strikeout metric */
} nt_text_style_t;

/* Destroys cached pipelines and the vertex input and clears all state. Call it before
 * nt_gfx_shutdown or a gfx re-init: new pools reuse handle ids. */
void nt_text_renderer_shutdown(void);

/* Selects the material for this frame's draws: call it every frame before drawing (asserted).
 * Another set_material replaces it, so a caller selects its own before drawing.
 * Requires an assigned slug_text program, premultiplied-compatible blend and cull NONE.
 * slug_text.frag never discards; a depth-writing material uses slug_text_depth.frag, which discards empty
 * pixels so they do not occlude. Pair it with a glyph_depth_bias for overlapping glyphs.
 * A text material declares no textures: the font's curve texture is the renderer's own bind (asserted). */
void nt_text_renderer_set_material(nt_material_t mat);

/* NULL or len=0 is a no-op; trailing partial UTF-8 codepoints are dropped without reading past utf8+len.
 * Unavailable font textures skip glyphs and decorations. Passes draw shadow, outline, fill, then
 * underline/strike, each over the whole run. */
void nt_text_renderer_draw_n(const nt_text_style_t *style, const float model[16], const char *utf8, size_t len);
void nt_text_renderer_draw(const nt_text_style_t *style, const float model[16], const char *utf8);

// #region test_access
#ifdef NT_TEST_ACCESS
/* Observed at every draw_n entry since the last reset_call_counters, also with a glyph-less font. */
void nt_text_renderer_test_reset_call_counters(void);
const float *nt_text_renderer_test_last_model(void);
uint32_t nt_text_renderer_test_draw_n_calls(void);
uint32_t nt_text_renderer_test_font_switches(void);
float nt_text_renderer_test_max_oblique(void);
float nt_text_renderer_test_max_weight(void);
float nt_text_renderer_test_max_outline_width(void);
bool nt_text_renderer_test_saw_underline(void);
/* Selected material id (0 = none); selection does not imply a pipeline is ready. */
uint32_t nt_text_renderer_test_material_id(void);
/* Occupied cache entries, including dead pipelines not yet removed by insertion/reset. */
uint16_t nt_text_renderer_test_pipeline_cache_count(void);
#endif
// #endregion

#endif /* NT_TEXT_RENDERER_H */
