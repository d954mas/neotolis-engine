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

// #region test_access
#ifdef NT_TEST_ACCESS
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

/* Solver spike filled in Task 3; Task 1 stubs the linkage. */
void nt_ui_rich_test_solve(nt_ui_context_t *ctx, float container_w) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    (void)container_w;
    st->atom_count = 0;
    st->line_count = (st->run_count > 0U) ? 1U : 0U;
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
