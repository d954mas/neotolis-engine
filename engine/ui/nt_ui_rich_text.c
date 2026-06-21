/* Rich text: run-list SoA build + style-stack composition + builder + prototype
 * solver spike. The full word-wrap/baseline/emit solver lands across plans 04-07;
 * this TU carries the data model, the code-first builder, and the Wave-1 spike. */

#include "ui/nt_ui_rich_text.h"

#include <string.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_internal.h"

// #region data-model
/* Frame-scratch run-list SoA. Parallel arrays bump-allocated by nt_ui_rich_begin;
 * stale after the next nt_mem_scratch_reset. A run only starts when the COMPOSED
 * style differs from the current run (dedup against the style table). */

#define NT_UI_RICH_STACK_DEPTH 32 /* push/pop nesting per call */

typedef struct {
    nt_rich_atom_kind_t kind;
    uint16_t style_idx; /* index into the dedup'd style table */
    uint8_t flags;      /* NT_UI_RICH_RUN_* (synthetic italic) */
    uint32_t link_id;   /* 0 = not a link */
    /* TEXT: byte range into the shared utf8 buffer. */
    uint32_t text_off;
    uint32_t text_len;
    /* IMAGE: */
    nt_atlas_region_ref_t image_ref;
    nt_rich_valign_t image_valign;
    float image_offset_y;
    float image_scale;
    /* OBJECT: */
    nt_ui_rich_object_measure_fn object_measure;
    nt_ui_rich_object_draw_fn object_draw;
    void *object_user;
} nt_ui_rich_run_t;

typedef struct {
    /* Run list. */
    nt_ui_rich_run_t *runs;
    uint32_t run_count;
    /* Dedup'd composed-style table. */
    nt_ui_rich_style_t *styles;
    uint32_t style_count;
    /* Shared UTF-8 text buffer. */
    char *text;
    uint32_t text_len;
    /* Composed-style stack. */
    nt_ui_rich_style_t stack[NT_UI_RICH_STACK_DEPTH];
    uint32_t stack_depth;
    /* Pending link id applied to the next emitted run(s); 0 = none. */
    uint32_t pending_link;

#ifdef NT_TEST_ACCESS
    /* Solver-spike output (plan-03 gate). The spike works on word-/atom-granular
     * units, not per-glyph, so a small cap is plenty for the gate cases. */
    nt_ui_rich_test_atom_t atoms[64];
    uint32_t atom_count;
    uint32_t line_count;
#endif
} nt_ui_rich_state_t;

static nt_ui_rich_state_t *rich_state(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx->pending_rich != NULL && "no active nt_ui_rich_begin");
    return (nt_ui_rich_state_t *)ctx->pending_rich;
}
// #endregion

nt_ui_rich_style_t nt_ui_rich_style_defaults(void) {
    nt_ui_rich_style_t s;
    memset(&s, 0, sizeof s);
    s.color_abgr = 0xFFFFFFFFU;
    s.scale = 1.0F;
    s.variant = 0;
    s.effect_id = 0;
    return s;
}

// #region style-stack
/* The composed style is the top of the stack. push_* mutates a COPY pushed on top;
 * pop restores the previous. A new run is emitted only when this composed style
 * differs from the current run's style (dedup). Shared by the builder AND the
 * future parser (plan 06) -- both feed rich_emit_text_run / rich_style_top. */

static nt_ui_rich_style_t *rich_style_top(nt_ui_rich_state_t *st) {
    NT_ASSERT(st->stack_depth > 0 && "rich style stack underflow");
    return &st->stack[st->stack_depth - 1];
}

static void rich_push_copy(nt_ui_rich_state_t *st) {
    NT_ASSERT(st->stack_depth < NT_UI_RICH_STACK_DEPTH && "rich style stack overflow");
    st->stack[st->stack_depth] = st->stack[st->stack_depth - 1];
    st->stack_depth++;
}

/* Select the family member for the composed variant; fall back BI->B->R. Returns
 * the resolved font and, via out_synth_italic, whether italic must be synthesized
 * (italic requested but the family has no italic member). */
static nt_font_t rich_resolve_font(const nt_ui_rich_style_t *s, bool *out_synth_italic) {
    *out_synth_italic = false;
    const uint8_t v = s->variant & 3U;
    if (v == 0U) {
        return s->font_id[0];
    }
    if (s->font_id[v].id != 0U) {
        if ((v & NT_UI_RICH_VARIANT_ITALIC) != 0U && s->font_id[v].id == 0U) {
            *out_synth_italic = true;
        }
        return s->font_id[v];
    }
    /* Fallback chain BI(3) -> B(1) -> R(0). */
    if ((v & NT_UI_RICH_VARIANT_BOLD) != 0U && s->font_id[NT_UI_RICH_VARIANT_BOLD].id != 0U) {
        if ((v & NT_UI_RICH_VARIANT_ITALIC) != 0U) {
            *out_synth_italic = true; /* dropped the italic family member */
        }
        return s->font_id[NT_UI_RICH_VARIANT_BOLD];
    }
    if ((v & NT_UI_RICH_VARIANT_ITALIC) != 0U) {
        *out_synth_italic = true; /* only regular available; shear it */
    }
    return s->font_id[0];
}

/* Member-wise equality -- the struct has a float field (scale), so memcmp on the
 * object representation is undefined for dedup (tidy bugprone-suspicious-memory-comparison). */
static bool rich_style_eq(const nt_ui_rich_style_t *a, const nt_ui_rich_style_t *b) {
    if (a->color_abgr != b->color_abgr || a->scale != b->scale || a->variant != b->variant || a->effect_id != b->effect_id) {
        return false;
    }
    for (uint32_t i = 0; i < 4U; i++) {
        if (a->font_id[i].id != b->font_id[i].id) {
            return false;
        }
    }
    return a->default_atlas.name_hash == b->default_atlas.name_hash && a->default_atlas.atlas.id == b->default_atlas.atlas.id && a->default_atlas.region == b->default_atlas.region;
}

/* Intern the composed style into the dedup table; returns its index. */
static uint16_t rich_intern_style(nt_ui_rich_state_t *st, const nt_ui_rich_style_t *s) {
    for (uint32_t i = 0; i < st->style_count; i++) {
        if (rich_style_eq(&st->styles[i], s)) {
            return (uint16_t)i;
        }
    }
    NT_ASSERT(st->style_count < NT_UI_RICH_MAX_STYLES && "rich style table overflow");
    st->styles[st->style_count] = *s;
    return (uint16_t)(st->style_count++);
}

static bool rich_run_extends_text(const nt_ui_rich_state_t *st, uint16_t style_idx, uint8_t flags, uint32_t link_id) {
    if (st->run_count == 0U) {
        return false;
    }
    const nt_ui_rich_run_t *r = &st->runs[st->run_count - 1];
    return r->kind == NT_RICH_ATOM_TEXT && r->style_idx == style_idx && r->flags == flags && r->link_id == link_id;
}

static nt_ui_rich_run_t *rich_new_run(nt_ui_rich_state_t *st, nt_rich_atom_kind_t kind) {
    NT_ASSERT(st->run_count < NT_UI_RICH_MAX_RUNS && "rich run-list overflow");
    nt_ui_rich_run_t *r = &st->runs[st->run_count++];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    return r;
}
// #endregion

// #region builder
void nt_ui_rich_begin(nt_ui_context_t *ctx, const nt_ui_rich_style_t *base) {
    NT_ASSERT(ctx != NULL);
    NT_ASSERT(ctx->pending_rich == NULL && "rich-text calls do not nest");

    nt_ui_rich_state_t *st = NT_MEM_SCRATCH_ALLOC(nt_ui_rich_state_t);
    memset(st, 0, sizeof *st);
    st->runs = NT_MEM_SCRATCH_ALLOC_ARRAY(nt_ui_rich_run_t, NT_UI_RICH_MAX_RUNS);
    st->styles = NT_MEM_SCRATCH_ALLOC_ARRAY(nt_ui_rich_style_t, NT_UI_RICH_MAX_STYLES);
    st->text = NT_MEM_SCRATCH_ALLOC_ARRAY(char, NT_UI_RICH_MAX_TEXT_BYTES);

    st->stack[0] = base ? *base : nt_ui_rich_style_defaults();
    st->stack_depth = 1;
    ctx->pending_rich = st;
}

void nt_ui_rich_push_color(nt_ui_context_t *ctx, uint32_t color_abgr) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_push_copy(st);
    rich_style_top(st)->color_abgr = color_abgr;
}

void nt_ui_rich_push_scale(nt_ui_context_t *ctx, float mult) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_push_copy(st);
    rich_style_top(st)->scale *= mult; /* multiplicative (D-67-11) */
}

void nt_ui_rich_push_font(nt_ui_context_t *ctx, const nt_font_t font_id[4]) {
    NT_ASSERT(font_id != NULL);
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_push_copy(st);
    nt_ui_rich_style_t *s = rich_style_top(st);
    for (uint32_t i = 0; i < 4U; i++) {
        s->font_id[i] = font_id[i];
    }
}

void nt_ui_rich_push_bold(nt_ui_context_t *ctx) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_push_copy(st);
    rich_style_top(st)->variant |= NT_UI_RICH_VARIANT_BOLD; /* additive */
}

void nt_ui_rich_push_italic(nt_ui_context_t *ctx) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_push_copy(st);
    rich_style_top(st)->variant |= NT_UI_RICH_VARIANT_ITALIC; /* additive */
}

void nt_ui_rich_push_effect(nt_ui_context_t *ctx, uint8_t effect_id) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_push_copy(st);
    rich_style_top(st)->effect_id = effect_id;
}

void nt_ui_rich_pop(nt_ui_context_t *ctx) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(st->stack_depth > 1 && "rich pop past base style");
    st->stack_depth--;
}

void nt_ui_rich_link(nt_ui_context_t *ctx, uint32_t link_id) { rich_state(ctx)->pending_link = link_id; }

void nt_ui_rich_text_n(nt_ui_context_t *ctx, const char *utf8, size_t len) {
    if (utf8 == NULL || len == 0U) {
        return;
    }
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(st->text_len + len <= NT_UI_RICH_MAX_TEXT_BYTES && "rich text buffer overflow");

    bool synth_italic = false;
    const nt_ui_rich_style_t *top = rich_style_top(st);
    (void)rich_resolve_font(top, &synth_italic);
    const uint8_t flags = synth_italic ? NT_UI_RICH_RUN_SYNTH_ITALIC : 0U;
    const uint16_t style_idx = rich_intern_style(st, top);

    const uint32_t off = st->text_len;
    memcpy(st->text + off, utf8, len);
    st->text_len += (uint32_t)len;

    if (rich_run_extends_text(st, style_idx, flags, st->pending_link)) {
        st->runs[st->run_count - 1].text_len += (uint32_t)len; /* same style -> extend (dedup) */
        return;
    }
    nt_ui_rich_run_t *r = rich_new_run(st, NT_RICH_ATOM_TEXT);
    r->style_idx = style_idx;
    r->flags = flags;
    r->link_id = st->pending_link;
    r->text_off = off;
    r->text_len = (uint32_t)len;
}

void nt_ui_rich_image(nt_ui_context_t *ctx, nt_atlas_region_ref_t ref, nt_rich_valign_t valign, float offset_y, float scale) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    const uint16_t style_idx = rich_intern_style(st, rich_style_top(st));
    nt_ui_rich_run_t *r = rich_new_run(st, NT_RICH_ATOM_IMAGE);
    r->style_idx = style_idx;
    r->link_id = st->pending_link;
    r->image_ref = ref;
    r->image_valign = valign;
    r->image_offset_y = offset_y;
    r->image_scale = scale;
}

void nt_ui_rich_object(nt_ui_context_t *ctx, nt_ui_rich_object_measure_fn measure_fn, nt_ui_rich_object_draw_fn draw_fn, void *user_data) {
    NT_ASSERT(measure_fn != NULL);
    nt_ui_rich_state_t *st = rich_state(ctx);
    const uint16_t style_idx = rich_intern_style(st, rich_style_top(st));
    nt_ui_rich_run_t *r = rich_new_run(st, NT_RICH_ATOM_OBJECT);
    r->style_idx = style_idx;
    r->link_id = st->pending_link;
    r->object_measure = measure_fn;
    r->object_draw = draw_fn;
    r->object_user = user_data;
}

void nt_ui_rich_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(st->stack_depth >= 1 && "rich style stack corrupted");
    /* Unbalanced pushes are fine -- a push not matched by a pop just stays active
     * to end-of-call and is discarded (the run-list already captured each run's
     * composed style). Run-list finalized; the solver/emit (plans 04-07) consume it.
     * The spike (test-only) runs via nt_ui_rich_test_solve; scratch frees on reset. */
}
// #endregion

void nt_ui_rich_text(nt_ui_context_t *ctx, uint32_t id, const nt_ui_element_data_t *data, const nt_ui_rich_style_t *style, float container_w, nt_rich_align_t align, float time) {
    /* Full word-wrap + baseline + emit lands across plans 04-07. Declared here so
     * consumers compile against the stable signature; the spike proves the math. */
    (void)ctx;
    (void)id;
    (void)data;
    (void)style;
    (void)container_w;
    (void)align;
    (void)time;
}

#ifdef NT_TEST_ACCESS
// #region SOLVER (prototype)
/* Wave-1 spike: mixed-run x-advance + asymmetric-icon baseline + a forced greedy
 * wrap, on deterministic metrics (no GL). Plan 04 generalizes this (alignment,
 * link rects, break-anywhere over-long words, OBJECT atoms, the real text/image
 * emit). Scratch-only, no heap. */

typedef struct {
    nt_rich_atom_kind_t kind;
    float advance; /* x cost on the line */
    float w;       /* box width (image) / text run width */
    float h;       /* box height (image) / line-box height (text) */
    float asc;     /* px above baseline this atom demands */
    float desc;    /* px below baseline this atom demands */
    nt_rich_valign_t valign;
    float offset_y;
    bool breakable_before; /* a break opportunity exists before this atom */
} spike_atom_t;

/* x_height proxy (Open Q3): half the text ascent. Deterministic; only needs to be
 * a stable non-ascent reference so middle != baseline placement. */
static float spike_x_height(float ascent_px) { return ascent_px * 0.5F; }

static void spike_text_metrics(nt_font_t font, float size, float *out_asc, float *out_desc) {
    const nt_font_metrics_t m = nt_font_get_metrics(font);
    if (m.units_per_em == 0U) {
        *out_asc = size;
        *out_desc = 0.0F;
        return;
    }
    const float px = size / (float)m.units_per_em;
    *out_asc = (float)m.ascent * px;
    *out_desc = -(float)m.descent * px;
}

/* Build the atom stream from the run-list. TEXT runs split into word atoms (space
 * kept with the preceding word -> a break opportunity after each space); IMAGE runs
 * are one unbreakable atom sized from the region source px * scale. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- spike word-split, generalized in plan 04
static uint32_t spike_build_atoms(nt_ui_rich_state_t *st, float font_size, spike_atom_t *out, uint32_t cap) {
    uint32_t n = 0;
    for (uint32_t ri = 0; ri < st->run_count; ri++) {
        const nt_ui_rich_run_t *run = &st->runs[ri];
        const nt_ui_rich_style_t *style = &st->styles[run->style_idx];
        const float size = font_size * style->scale;

        if (run->kind == NT_RICH_ATOM_TEXT) {
            bool synth = false;
            const nt_font_t font = rich_resolve_font(style, &synth);
            float t_asc = 0.0F;
            float t_desc = 0.0F;
            spike_text_metrics(font, size, &t_asc, &t_desc);

            const char *base = st->text + run->text_off;
            uint32_t i = 0;
            const uint32_t len = run->text_len;
            while (i < len) {
                const uint32_t word_start = i;
                while (i < len && base[i] != ' ') {
                    i++;
                }
                while (i < len && base[i] == ' ') {
                    i++; /* trailing spaces belong to this word atom */
                }
                const uint32_t word_len = i - word_start;
                NT_ASSERT(n < cap && "spike atom overflow");
                spike_atom_t *a = &out[n++];
                memset(a, 0, sizeof *a);
                a->kind = NT_RICH_ATOM_TEXT;
                a->advance = nt_font_measure_n(font, base + word_start, word_len, size, 0.0F).width;
                a->w = a->advance;
                a->h = t_asc + t_desc;
                a->asc = t_asc;
                a->desc = t_desc;
                a->breakable_before = (n > 1); /* break opportunity between words */
            }
        } else if (run->kind == NT_RICH_ATOM_IMAGE) {
            const nt_texture_region_t *reg = nt_atlas_get_region(run->image_ref.atlas, run->image_ref.region);
            const float src_w = (reg != NULL) ? (float)reg->source_w : 1.0F;
            const float src_h = (reg != NULL) ? (float)reg->source_h : 1.0F;
            const float bw = src_w * run->image_scale;
            const float bh = src_h * run->image_scale;

            NT_ASSERT(n < cap && "spike atom overflow");
            spike_atom_t *a = &out[n++];
            memset(a, 0, sizeof *a);
            a->kind = NT_RICH_ATOM_IMAGE;
            a->advance = bw;
            a->w = bw;
            a->h = bh;
            a->valign = run->image_valign;
            a->offset_y = run->image_offset_y;
            a->breakable_before = true; /* image: break opportunity on both sides */
        }
        /* OBJECT atoms: generalized in plan 07; the spike does not place them. */
    }
    return n;
}

/* PASS 1: greedy line break. Writes each atom's line index; returns line count. */
static uint32_t spike_break_lines(const spike_atom_t *atoms, uint32_t na, float container_w, uint32_t *line_of) {
    float line_w = 0.0F;
    uint32_t line = 0;
    for (uint32_t i = 0; i < na; i++) {
        const float aw = atoms[i].advance;
        if (line_w > 0.0F && atoms[i].breakable_before && (line_w + aw) > container_w) {
            line++; /* break BEFORE this atom (greedy) */
            line_w = 0.0F;
        }
        line_of[i] = line;
        line_w += aw;
    }
    return (na > 0U) ? (line + 1U) : 0U;
}

/* An IMAGE atom's ascent/descent demand for the line, given the line's text ascent. */
static void spike_image_metrics(const spike_atom_t *a, float text_asc, float *io_asc, float *io_desc) {
    const float h = a->h;
    if (a->valign == NT_RICH_VALIGN_MIDDLE) {
        const float xh = spike_x_height(text_asc > 0.0F ? text_asc : h);
        const float asc = (h * 0.5F) + (xh * 0.5F);
        const float desc = (h * 0.5F) - (xh * 0.5F);
        *io_asc = (asc > *io_asc) ? asc : *io_asc;
        *io_desc = (desc > *io_desc) ? desc : *io_desc;
    } else { /* BASELINE: whole box above the baseline */
        *io_asc = (h > *io_asc) ? h : *io_asc;
    }
}

/* Per-line max ascent/descent over the line's atoms. Text drives x_height. */
static void spike_line_metrics(const spike_atom_t *atoms, uint32_t na, const uint32_t *line_of, uint32_t L, float *out_asc, float *out_desc) {
    float text_asc = 0.0F;
    *out_asc = 0.0F;
    *out_desc = 0.0F;
    for (uint32_t i = 0; i < na; i++) {
        if (line_of[i] == L && atoms[i].kind == NT_RICH_ATOM_TEXT) {
            text_asc = (atoms[i].asc > text_asc) ? atoms[i].asc : text_asc;
            *out_asc = (atoms[i].asc > *out_asc) ? atoms[i].asc : *out_asc;
            *out_desc = (atoms[i].desc > *out_desc) ? atoms[i].desc : *out_desc;
        }
    }
    for (uint32_t i = 0; i < na; i++) {
        if (line_of[i] == L && atoms[i].kind == NT_RICH_ATOM_IMAGE) {
            spike_image_metrics(&atoms[i], text_asc, out_asc, out_desc);
        }
    }
}

static float spike_atom_y(const spike_atom_t *a, float baseline_y, float line_asc) {
    if (a->kind == NT_RICH_ATOM_TEXT) {
        return baseline_y - a->asc; /* top of the glyph box */
    }
    if (a->valign == NT_RICH_VALIGN_MIDDLE) {
        const float xh = spike_x_height(line_asc);
        return baseline_y - ((a->h * 0.5F) + (xh * 0.5F)) + a->offset_y;
    }
    return baseline_y - a->h + a->offset_y; /* BASELINE */
}

void nt_ui_rich_test_solve(nt_ui_context_t *ctx, float container_w, float font_size) {
    nt_ui_rich_state_t *st = rich_state(ctx);

    spike_atom_t atoms[64];
    uint32_t line_of[64];
    const uint32_t na = spike_build_atoms(st, font_size, atoms, 64U);
    st->line_count = spike_break_lines(atoms, na, container_w, line_of);

    float pen_y = 0.0F;
    st->atom_count = 0;
    for (uint32_t L = 0; L < st->line_count; L++) {
        float asc = 0.0F;
        float desc = 0.0F;
        spike_line_metrics(atoms, na, line_of, L, &asc, &desc);
        const float baseline_y = pen_y + asc;

        float pen_x = 0.0F;
        for (uint32_t i = 0; i < na; i++) {
            if (line_of[i] != L) {
                continue;
            }
            NT_ASSERT(st->atom_count < (uint32_t)(sizeof st->atoms / sizeof st->atoms[0]));
            nt_ui_rich_test_atom_t *probe = &st->atoms[st->atom_count++];
            probe->kind = atoms[i].kind;
            probe->x = pen_x;
            probe->y = spike_atom_y(&atoms[i], baseline_y, asc);
            probe->w = atoms[i].w;
            probe->h = atoms[i].h;
            probe->line = L;
            pen_x += atoms[i].advance;
        }
        pen_y += asc + desc;
    }
}
// #endregion

// #region test_access
uint32_t nt_ui_rich_test_run_count(nt_ui_context_t *ctx) { return rich_state(ctx)->run_count; }

nt_ui_rich_style_t nt_ui_rich_test_run_style(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return st->styles[st->runs[run].style_idx];
}

uint8_t nt_ui_rich_test_run_flags(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return st->runs[run].flags;
}

nt_font_t nt_ui_rich_test_run_font(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    bool synth = false;
    return rich_resolve_font(&st->styles[st->runs[run].style_idx], &synth);
}

uint32_t nt_ui_rich_test_line_count(nt_ui_context_t *ctx) { return rich_state(ctx)->line_count; }
uint32_t nt_ui_rich_test_atom_count(nt_ui_context_t *ctx) { return rich_state(ctx)->atom_count; }

nt_ui_rich_test_atom_t nt_ui_rich_test_atom(nt_ui_context_t *ctx, uint32_t atom) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(atom < st->atom_count);
    return st->atoms[atom];
}
#endif
// #endregion
