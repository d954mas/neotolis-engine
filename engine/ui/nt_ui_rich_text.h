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

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "font/nt_font.h"   /* nt_font_t */
#include "ui/nt_ui.h"       /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

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
    uint8_t _pad[6];                     /* 42: explicit tail pad to the 8-byte-aligned 48 */
} nt_ui_rich_style_t;
_Static_assert(sizeof(nt_ui_rich_style_t) == 48, "nt_ui_rich_style_t stable ABI (16 ref + 4 font + u32 + f32 + 8 tail)");

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
void nt_ui_rich_text_n(nt_ui_context_t *ctx, const char *utf8, size_t len);
void nt_ui_rich_image(nt_ui_context_t *ctx, nt_atlas_region_ref_t ref, nt_rich_valign_t valign, float offset_y, float scale);
void nt_ui_rich_object(nt_ui_context_t *ctx, nt_ui_rich_object_measure_fn measure_fn, nt_ui_rich_object_draw_fn draw_fn, void *user_data);
void nt_ui_rich_link(nt_ui_context_t *ctx, uint32_t link_id);
void nt_ui_rich_pop(nt_ui_context_t *ctx);
void nt_ui_rich_end(nt_ui_context_t *ctx);

/* ---- Public widget (full impl across plans 04-07) ---- */
/* Emits the built run-list as a wrapped, baseline-aligned rich-text block under a
 * FIXED Clay element. `time` feeds per-atom effects (game owns the clock, D-67-18). */
void nt_ui_rich_text(nt_ui_context_t *ctx, uint32_t id, const nt_ui_element_data_t *data, const nt_ui_rich_style_t *style, float container_w, nt_rich_align_t align, float time);

// #region test_access
#ifdef NT_TEST_ACCESS
/* Run-list probes for the build tests. */
uint32_t nt_ui_rich_test_run_count(nt_ui_context_t *ctx);
nt_ui_rich_style_t nt_ui_rich_test_run_style(nt_ui_context_t *ctx, uint32_t run);
uint8_t nt_ui_rich_test_run_flags(nt_ui_context_t *ctx, uint32_t run);
nt_font_t nt_ui_rich_test_run_font(nt_ui_context_t *ctx, uint32_t run);

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
#endif
// #endregion

#endif /* NT_UI_RICH_TEXT_H */
