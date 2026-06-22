#ifndef NT_UI_RICH_TEXT_H
#define NT_UI_RICH_TEXT_H

/* Code-first rich text: a push/pop builder produces a flat run-list SoA in frame
 * scratch (no heap). A run starts only when the COMPOSED style changes. Bold/italic
 * resolve to font VARIANTS (variant bits select font_id[4]) with BI->B->R fallback.
 * The solver (word-wrap + baseline + emit) lands across plans 04-07; this header is
 * the data model + builder + the public signature. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atlas/nt_atlas.h"       /* nt_atlas_region_ref_t */
#include "font/nt_font.h"         /* nt_font_t */
#include "material/nt_material.h" /* nt_material_t (inline-image custom-attr path) */
#include "ui/nt_ui.h"             /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;
typedef struct nt_ui_rich_tagset nt_ui_rich_tagset_t; /* parser vocabulary; defined in nt_ui_rich_tagset.h */

/* ---- Run-list caps (frame-scratch, ASSERT on overflow; D-67-01) ---- */
#ifndef NT_UI_RICH_MAX_RUNS
#define NT_UI_RICH_MAX_RUNS 256 /* runs per nt_ui_rich_text call */
#endif
#ifndef NT_UI_RICH_MAX_GLYPHS
#define NT_UI_RICH_MAX_GLYPHS 2048 /* positioned glyph atoms per call */
#endif
#ifndef NT_UI_RICH_MAX_LINES
#define NT_UI_RICH_MAX_LINES 64
#endif
#ifndef NT_UI_RICH_MAX_STYLES
#define NT_UI_RICH_MAX_STYLES 32 /* dedup'd composed styles per call */
#endif
#ifndef NT_UI_RICH_MAX_LINKS
#define NT_UI_RICH_MAX_LINKS 32 /* hitbox rects per call */
#endif
#ifndef NT_UI_RICH_MAX_CUSTOM_FX
#define NT_UI_RICH_MAX_CUSTOM_FX 16 /* distinct game-supplied (fn,user_data) effects per call */
#endif
/* Shared UTF-8 text buffer for all TEXT runs in one call. */
#ifndef NT_UI_RICH_MAX_TEXT_BYTES
#define NT_UI_RICH_MAX_TEXT_BYTES 4096
#endif

/* ---- Atom / run kinds ---- */
typedef enum {
    NT_RICH_ATOM_TEXT = 0,
    NT_RICH_ATOM_IMAGE,
    NT_RICH_ATOM_OBJECT,
} nt_rich_atom_kind_t;

/* ---- Per-atom effect ABI (full catalog + stock curves live in nt_ui_rich_fx.h) ----
 * Declared here (not fx.h) so the builder signature nt_ui_rich_push_effect_fn can name the fn
 * type without a text.h <-> fx.h include cycle (fx.h includes this header). The effect transform
 * applied to one atom at emit: offset shifts the quad / draw() xy; color is the ABSOLUTE resolved
 * RGBA tint (NOT a multiplier -- identity returns base_color verbatim); scale scales about the
 * atom center; visible==false skips the atom emit. */
typedef struct {
    float offset_x; /*  0: px shift of the atom quad / draw() x */
    float offset_y; /*  4: px shift of the atom quad / draw() y */
    float color[4]; /*  8: ABSOLUTE resolved RGBA tint -- REPLACES the base (identity == base_color), never multiplies */
    float scale;    /* 24: scale about the atom center (1 = identity) */
    bool visible;   /* 28: false -> skip the atom emit */
} nt_ui_rich_fx_result_t;
_Static_assert(sizeof(nt_ui_rich_fx_result_t) == 32, "nt_ui_rich_fx_result_t stable ABI (6 float + 1 bool + pad)");

/* Per-atom effect callback. base_xy/base_wh/base_color are the solver's resolved atom box +
 * color (read-only inputs); time is the game-owned clock (seconds); hovered is true only for
 * the atoms of the currently-hovered link (link hover gates effects, D-67-24). The fn returns
 * the visual-only transform; it MUST NOT mutate layout. */
typedef nt_ui_rich_fx_result_t (*nt_ui_rich_fx_fn)(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered);

/* Variant bits select font_id[]: bit0=bold bit1=italic (D-67-04/16). */
#define NT_UI_RICH_VARIANT_BOLD (1U << 0)
#define NT_UI_RICH_VARIANT_ITALIC (1U << 1)

/* Run flag bits. */
#define NT_UI_RICH_RUN_SYNTH_ITALIC (1U << 0) /* italic requested but no italic family member -> shear at emit */

/* Image vertical alignment against the line (solver plans 04+). */
typedef enum {
    NT_RICH_VALIGN_BASELINE = 0,
    NT_RICH_VALIGN_MIDDLE,
    NT_RICH_VALIGN_TOP,
    NT_RICH_VALIGN_BOTTOM,
} nt_rich_valign_t;

/* Horizontal alignment of the whole block (solver plan 04). */
typedef enum {
    NT_RICH_ALIGN_LEFT = 0,
    NT_RICH_ALIGN_CENTER,
    NT_RICH_ALIGN_RIGHT,
} nt_rich_align_t;

/* ---- The dedup'd composed style (the run_style[] target; D-67-10/11) ---- */
/* default_atlas (nt_atlas_region_ref_t) leads with a uint64 -> 8-byte alignment,
 * so the struct rounds up to 48 (the 4-byte color/scale/variant tail pads out). */
typedef struct {
    nt_atlas_region_ref_t default_atlas; /*  0: base-style default image atlas (D-67-15) */
    nt_font_t font_id[4];                /* 16: R/B/I/BI variants (D-67-04/16) */
    uint32_t color_abgr;                 /* 32: innermost <color> override (D-67-10) */
    float scale;                         /* 36: accumulated x multiplier (D-67-11) */
    uint8_t variant;                     /* 40: NT_UI_RICH_VARIANT_* -> selects font_id[] */
    uint8_t effect_id;                   /* 41: stock effect catalog index; 0 = none */
    uint8_t _pad[2];                     /* 42: alignment pad to the 4-byte material handle */
    nt_material_t image_material;        /* 44: inline-image custom-attr material (D-67-14/15); .id==0 = no inline images */
} nt_ui_rich_style_t;
_Static_assert(sizeof(nt_ui_rich_style_t) == 48, "nt_ui_rich_style_t stable ABI (16 ref + 4 font + u32 + f32 + variant/effect + material)");

/* Use instead of bare {0} -- color_abgr=0 renders fully transparent. */
nt_ui_rich_style_t nt_ui_rich_style_defaults(void);

/* ---- Object atom callbacks (callback path lands in plan 07) ---- */
typedef struct {
    float width;
    float height;
    float ascent; /* above baseline; for valign=baseline placement */
} nt_ui_rich_object_measure_t;

typedef nt_ui_rich_object_measure_t (*nt_ui_rich_object_measure_fn)(void *user_data);
typedef void (*nt_ui_rich_object_draw_fn)(void *user_data, float x, float y, float w, float h);

/* ---- Builder (code-first push/pop; D-67-02) ---- */
/* All builder calls operate on the per-call run-list owned by ctx between begin/end.
 * The composed style stack roots at the base `style` passed to nt_ui_rich_begin. */
void nt_ui_rich_begin(nt_ui_context_t *ctx, const nt_ui_rich_style_t *base);
void nt_ui_rich_push_color(nt_ui_context_t *ctx, uint32_t color_abgr);
void nt_ui_rich_push_scale(nt_ui_context_t *ctx, float mult);
void nt_ui_rich_push_font(nt_ui_context_t *ctx, const nt_font_t font_id[4]);
void nt_ui_rich_push_bold(nt_ui_context_t *ctx);
void nt_ui_rich_push_italic(nt_ui_context_t *ctx);
void nt_ui_rich_push_effect(nt_ui_context_t *ctx, uint8_t effect_id);
/* Push a game-supplied CUSTOM effect fn (resolved at emit, BEFORE the stock catalog). The
 * (fn,user_data) is captured into a per-call fixed-cap table; the composed style carries a
 * custom effect_id index into it (D-67-26). fn must be non-NULL. */
void nt_ui_rich_push_effect_fn(nt_ui_context_t *ctx, nt_ui_rich_fx_fn fn, void *user_data);
void nt_ui_rich_text_n(nt_ui_context_t *ctx, const char *utf8, size_t len);
void nt_ui_rich_image(nt_ui_context_t *ctx, nt_atlas_region_ref_t ref, nt_rich_valign_t valign, float offset_y, float scale);
void nt_ui_rich_object(nt_ui_context_t *ctx, nt_ui_rich_object_measure_fn measure_fn, nt_ui_rich_object_draw_fn draw_fn, void *user_data);
void nt_ui_rich_link(nt_ui_context_t *ctx, uint32_t link_id);
void nt_ui_rich_pop(nt_ui_context_t *ctx);
void nt_ui_rich_end(nt_ui_context_t *ctx);

/* ---- Link interaction result (FX-67-03, Model D) ---- */
/* The widget hit-tests its `<link=id>` solver rects itself (NO extra Clay element) and reports
 * the link under the pointer + the link clicked this frame. 0 = none. Link hover gates effects
 * (an effect reads `hovered==true` only for the hovered link's atoms). */
typedef struct {
    uint32_t hovered_link; /* link id under the pointer this frame (0 = none) */
    uint32_t clicked_link; /* link id clicked (released over its rect) this frame (0 = none) */
} nt_ui_rich_result_t;

/* ---- Runtime markup parser (D-67-02; the SECOND authoring front) ---- */
/* Parses an angular `<tag>...</tag>` + self-closing `<img=region/>` markup string into the
 * SAME run-list as the equivalent code-first builder calls, by DRIVING the shared builder
 * (begin / push_* / text_n / image / pop / end) -- no second composition path. A markup string and the
 * equivalent builder sequence produce a byte-identical run-list (MARK-67-04).
 *
 * CORE tags are intrinsic (no registration, D-67-08): `<b>` `<i>` `<color=#hex>` `<scale=N>`
 * `<font=name>` `<link=id>` and self-closing `<img=region/>` / `<img=alias:region/>`. Names
 * (`<font=name>`, `<color=name>`, `<img=alias:...>`) resolve via the passed tagset; `<color=#hex>`
 * is intrinsic hex; `<img=region/>` resolves against base->default_atlas.
 *
 * Malformed markup fires NT_ASSERT (builder-validates spirit). The tokenizer scan is bounded by
 * `len` so adversarial input cannot OOB or loop (MARK-67-05). `tagset` may be NULL when the markup
 * uses only intrinsic tags. The parser opens/closes its own begin/end on ctx. */
void nt_ui_rich_parse(nt_ui_context_t *ctx, const nt_ui_rich_tagset_t *tagset, const nt_ui_rich_style_t *base, const char *markup, size_t len);

/* Convenience entry: parse the markup, then solve + emit it as a wrapped block (parse -> the
 * public widget pipeline). Mirrors nt_ui_rich_text's signature with a markup string in place of
 * the code-first builder calls. */
void nt_ui_rich_text_markup(nt_ui_context_t *ctx, uint32_t id, const nt_ui_element_data_t *data, const nt_ui_rich_tagset_t *tagset, const nt_ui_rich_style_t *style, const char *markup, size_t len,
                            float container_w, nt_rich_align_t align, float time, nt_ui_rich_result_t *out);

/* ---- Public widget (full impl across plans 04-07) ---- */
/* Emits the built run-list as a wrapped, baseline-aligned rich-text block under a
 * FIXED Clay element. `time` feeds per-atom effects (game owns the clock, D-67-18). `out` (NULL-able)
 * receives the link hover/click result; link hover gates the per-atom effects. */
void nt_ui_rich_text(nt_ui_context_t *ctx, uint32_t id, const nt_ui_element_data_t *data, const nt_ui_rich_style_t *style, float container_w, nt_rich_align_t align, float time,
                     nt_ui_rich_result_t *out);

// #region test_access
#ifdef NT_TEST_ACCESS
/* Run-list probes for the build tests. */
uint32_t nt_ui_rich_test_run_count(nt_ui_context_t *ctx);
nt_ui_rich_style_t nt_ui_rich_test_run_style(nt_ui_context_t *ctx, uint32_t run);
uint8_t nt_ui_rich_test_run_flags(nt_ui_context_t *ctx, uint32_t run);
nt_font_t nt_ui_rich_test_run_font(nt_ui_context_t *ctx, uint32_t run);

/* Run kind / link / TEXT bytes / IMAGE ref probes -- the builder==parser byte-identity gate. */
nt_rich_atom_kind_t nt_ui_rich_test_run_kind(nt_ui_context_t *ctx, uint32_t run);
uint32_t nt_ui_rich_test_run_link(nt_ui_context_t *ctx, uint32_t run);
uint32_t nt_ui_rich_test_run_text_len(nt_ui_context_t *ctx, uint32_t run);
const char *nt_ui_rich_test_run_text(nt_ui_context_t *ctx, uint32_t run); /* NOT null-terminated; use with run_text_len */
nt_atlas_region_ref_t nt_ui_rich_test_run_image_ref(nt_ui_context_t *ctx, uint32_t run);

/* Solver-spike probes (plan-03 gate). Positions resolved by nt_ui_rich_test_solve. */
typedef struct {
    nt_rich_atom_kind_t kind;
    float x;
    float y;
    float w;
    float h;
    uint32_t line;
} nt_ui_rich_test_atom_t;

/* Solver entry: font_size feeds the deterministic measure (advance = size/2/char
 * under nt_font_test_set_metrics); the resolved per-atom boxes land in the atoms probe.
 * The bare form is LEFT-aligned, id 0 (explicit container_w only) -- plan-03 cases. */
void nt_ui_rich_test_solve(nt_ui_context_t *ctx, float container_w, float font_size);
/* Full form: id feeds the two-pass nt_ui_get_bbox fallback when container_w<=0; align
 * selects the per-line L/C/R offset. */
void nt_ui_rich_test_solve_ex(nt_ui_context_t *ctx, uint32_t id, float container_w, float font_size, nt_rich_align_t align);
uint32_t nt_ui_rich_test_line_count(nt_ui_context_t *ctx);
uint32_t nt_ui_rich_test_atom_count(nt_ui_context_t *ctx);
nt_ui_rich_test_atom_t nt_ui_rich_test_atom(nt_ui_context_t *ctx, uint32_t atom);
/* Solved total size that feeds the ONE Clay CLAY_SIZING_FIXED block (D-67-03). */
float nt_ui_rich_test_total_w(nt_ui_context_t *ctx);
float nt_ui_rich_test_total_h(nt_ui_context_t *ctx);
/* draw_n spans the last walk's rich-text emit produced (== solved TEXT line-fragments). */
uint32_t nt_ui_rich_test_emit_span_count(nt_ui_context_t *ctx);

/* Inline-image emit probes (RICH-67-05). The solver records the first IMAGE atom's resolved
 * region + custom-block size + solved y; the emit counter tracks images declared this call. */
uint32_t nt_ui_rich_test_image_emit_count(nt_ui_context_t *ctx);
uint32_t nt_ui_rich_test_image_block_bytes(nt_ui_context_t *ctx);
uint32_t nt_ui_rich_test_image_region(nt_ui_context_t *ctx);
float nt_ui_rich_test_image_y(nt_ui_context_t *ctx);

/* Link/effect probes (FX-67-01..04). The link hover/click the last call resolved + per-link rects. */
uint32_t nt_ui_rich_test_hovered_link(nt_ui_context_t *ctx);
uint32_t nt_ui_rich_test_clicked_link(nt_ui_context_t *ctx);
uint32_t nt_ui_rich_test_link_rect_count(nt_ui_context_t *ctx);
/* The solver effect_id carried by a solved atom (0 = none) -- proves effects ride the solved stream. */
uint8_t nt_ui_rich_test_atom_effect_id(nt_ui_context_t *ctx, uint32_t atom);
#endif
// #endregion

#endif /* NT_UI_RICH_TEXT_H */
