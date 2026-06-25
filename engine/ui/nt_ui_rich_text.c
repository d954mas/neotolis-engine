/* Rich text: run-list SoA build + style-stack composition + builder + full
 * word-wrap/baseline/emit solver. */

/* Markup is UNTRUSTED localization data: a malformed tag/value logs once + degrades
 * (skip -> visible unstyled text), it never asserts. Distinct messages => distinct lines.
 * Defined BEFORE any include so nt_log.h (pulled transitively) picks "ui.rich", not the module default. */
#define NT_LOG_DOMAIN "ui.rich"

#include "ui/nt_ui_rich_text.h"

#include <math.h> /* isfinite (fail-early scale guards) */
#include <string.h>

#include "atlas/nt_atlas.h" /* inline-image region resolve + inverse-ppu (immediate emit) */
#include "clay.h"
#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "material/nt_material.h" /* nt_material_get_info: assert the image material is the plain u8 path */
#include "memory/nt_mem_scratch.h"
#include "renderers/nt_sprite_renderer.h" /* inline-image immediate emit (set_material + emit_region) */
#include "renderers/nt_text_renderer.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_fx.h" /* per-atom effect ABI + stock catalog */
#include "ui/nt_ui_rich_tagset.h"
#include "utf8/nt_utf8.h"

/* Default px font size the public widget feeds the solver when a run carries no per-run
 * size (size lives in the style scale multiplier; absolute <size> is deferred). */
#ifndef NT_UI_RICH_DEFAULT_FONT_SIZE
#define NT_UI_RICH_DEFAULT_FONT_SIZE 16.0F
#endif

/* Distinct z-order bands the self-emit walks (each adds a sprite+text flush boundary). A block with more
 * distinct <layer>s than this drops the over-cap layers BY ENCOUNTER ORDER (not by value) rather than OOB
 * the per-layer scratch. */
#ifndef NT_UI_RICH_MAX_LAYERS
#define NT_UI_RICH_MAX_LAYERS 16
#endif

// #region data-model
/* Frame-scratch run-list SoA. Parallel arrays bump-allocated by nt_ui_rich_begin;
 * stale after the next nt_mem_scratch_reset. A run only starts when the COMPOSED
 * style differs from the current run (dedup against the style table). */

#define NT_UI_RICH_PARSE_TAG_DEPTH 32 /* matched open-tag stack depth cap (parser) */
/* +1 over the parser tag cap so a balanced push/pop chain at max nesting never pops the base style. */
#define NT_UI_RICH_STACK_DEPTH (NT_UI_RICH_PARSE_TAG_DEPTH + 1) /* push/pop nesting per call */

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

/* A solved, positioned atom (the solver's output, consumed by emit). TEXT atoms keep a
 * byte range into the shared buffer + the run's resolved font/size/color so emit can
 * draw_n the exact span; IMAGE/OBJECT reserve their box. */
typedef struct {
    nt_rich_atom_kind_t kind;
    float x;
    float y; /* glyph-box top (TEXT) / box top (IMAGE), local Y-down */
    float w;
    float h;
    float asc; /* TEXT ascent px: baseline = y + asc (emit feeds draw_n the baseline) */
    uint32_t line;
    /* TEXT placement context. */
    nt_font_t font;
    float size;        /* px (font_size * style scale) */
    uint32_t color;    /* AABBGGRR */
    uint32_t text_off; /* byte range into st->text */
    uint32_t text_len;
    uint8_t flags;     /* NT_UI_RICH_RUN_SYNTH_ITALIC -> shear at emit */
    uint8_t effect_id; /* stock effect catalog index from the run's style; 0 = none */
    uint8_t layer;     /* EFFECTIVE z-order band (AUTO already resolved to the per-kind default) */
    uint32_t fx_idx;   /* stable per-block atom index fed to the effect curve (phase/stagger) */
    /* IMAGE/OBJECT box context. */
    uint16_t run_idx; /* back-reference for the image/object emit */
    uint32_t link_id; /* 0 = not a link */
} nt_ui_rich_solved_atom_t;

/* One <link>-marked run's hitbox rect (populated by the solver). */
typedef struct {
    uint32_t link_id;
    uint32_t line; /* line index this rect belongs to -- union key (no float-eq on accumulated y) */
    float x;
    float y;
    float w;
    float h;
} nt_ui_rich_link_rect_t;

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

    /* ---- Custom (game-supplied) effects ----
     * A per-call fixed-cap table captured at build: a composed-style effect_id >= the custom base
     * indexes (id - base) here. Captured so emit (which sees only the solved state, never the
     * tagset) can resolve a custom fn. */
    nt_ui_rich_fx_fn custom_fx[NT_UI_RICH_MAX_CUSTOM_FX];
    void *custom_fx_user[NT_UI_RICH_MAX_CUSTOM_FX];
    /* By-value param storage for stock effects tuned via push_effect_ex / `<fx=name k=v>`: the slot's
     * custom_fx_user points HERE so the params outlive the (transient) caller struct until emit. */
    nt_ui_rich_fx_params_t custom_fx_params[NT_UI_RICH_MAX_CUSTOM_FX];
    uint32_t custom_fx_count;
    /* The cap must keep every custom id (BASE + index) inside a uint8_t -- ceiling enforced here, not
     * by a dead runtime assert. */
    _Static_assert(NT_UI_RICH_MAX_CUSTOM_FX <= (255 - NT_UI_RICH_FX_CUSTOM_BASE), "custom-fx cap exceeds the uint8_t effect-id range above NT_UI_RICH_FX_CUSTOM_BASE");

    /* ---- Solver output (frame scratch; consumed by emit + the test probes) ---- */
    nt_ui_rich_solved_atom_t *solved; /* NT_UI_RICH_MAX_GLYPHS cap */
    uint32_t solved_count;
    uint32_t line_count;
    nt_ui_rich_link_rect_t *links; /* NT_UI_RICH_MAX_LINKS cap */
    uint32_t link_count;
    float total_w;
    float total_h;
    bool solved_ready;        /* solver ran for this call (emit re-walk safe, read-only) */
    uint32_t emit_span_count; /* draw_n spans the last emit produced (probe) */

    /* ---- Effects + links ---- */
    float time;            /* game-passed animation clock fed to per-atom effects */
    uint32_t hovered_link; /* the link id under the pointer this call (0 = none); gates effects */
    uint32_t clicked_link; /* the link id clicked this call (0 = none) */

    /* ---- Inline-image emit ---- */
    nt_material_t image_material; /* base-style inline-image material (.id==0 -> no images) */
    uint32_t image_emit_count;    /* IMAGE atoms emitted this call, post-walk (probe) */
    uint32_t image_region;        /* by-name resolved region index of the first IMAGE atom (probe) */
    float image_y;                /* solved y of the first IMAGE atom (probe) */
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
    s.layer = (uint8_t)NT_UI_RICH_LAYER_AUTO; /* per-kind default until an explicit <layer=N> */
    return s;
}

// #region style-stack
/* The composed style is the stack top: push_* pushes a mutated COPY, pop restores. A new run starts
 * only when the composed style differs from the current run's (dedup). Shared by builder + parser. */

static nt_ui_rich_style_t *rich_style_top(nt_ui_rich_state_t *st) {
    NT_ASSERT(st->stack_depth > 0 && "rich style stack underflow");
    /* HARD clamp (survives NT_ASSERT OFF): a desynced depth==0 would index stack[(uint32_t)-1] OOB.
     * Floor to the base slot rather than underflow the index (defense-in-depth with nt_ui_rich_pop). */
    const uint32_t i = st->stack_depth ? st->stack_depth - 1U : 0U;
    return &st->stack[i];
}

static void rich_push_copy(nt_ui_rich_state_t *st) {
    NT_ASSERT(st->stack_depth < NT_UI_RICH_STACK_DEPTH && "rich style stack overflow");
    /* HARD cap (survives NT_ASSERT OFF): the parser drives this from untrusted nesting; without the
     * bound a deep open-tag run would write past stack[] (and a matching </> would underflow-pop). */
    if (st->stack_depth >= NT_UI_RICH_STACK_DEPTH) {
        return;
    }
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
        return s->font_id[v]; /* exact variant exists: no synth (italic member present when v has italic) */
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

/* Member-wise equality: scale is float; object-rep compare is UB. */
static bool rich_style_eq(const nt_ui_rich_style_t *a, const nt_ui_rich_style_t *b) {
    if (a->color_abgr != b->color_abgr || a->scale != b->scale || a->variant != b->variant || a->effect_id != b->effect_id || a->layer != b->layer || a->image_material.id != b->image_material.id) {
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
    /* HARD cap (survives NT_ASSERT OFF): untrusted markup can mint many distinct composed styles;
     * once full, reuse the last slot rather than write past styles[] (a wrong tint beats an OOB). */
    if (st->style_count >= NT_UI_RICH_MAX_STYLES) {
        return (uint16_t)(NT_UI_RICH_MAX_STYLES - 1U);
    }
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
    /* HARD cap (survives NT_ASSERT OFF): the parser appends one run per style change in untrusted
     * markup. Once full, overwrite the last slot rather than index past runs[] -- callers only
     * memset+fill the returned pointer, so the worst OFF outcome is a dropped run, never an OOB. */
    const uint32_t idx = (st->run_count < NT_UI_RICH_MAX_RUNS) ? st->run_count++ : (NT_UI_RICH_MAX_RUNS - 1U);
    nt_ui_rich_run_t *r = &st->runs[idx];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    return r;
}
// #endregion

// #region builder
void nt_ui_rich_begin(nt_ui_context_t *ctx, const nt_ui_rich_style_t *base) {
    NT_ASSERT(ctx != NULL);
    NT_ASSERT(!ctx->rich_session_open && "rich-text calls do not nest");

    nt_ui_rich_state_t *st = NT_MEM_SCRATCH_ALLOC(nt_ui_rich_state_t);
    memset(st, 0, sizeof *st);
    st->runs = NT_MEM_SCRATCH_ALLOC_ARRAY(nt_ui_rich_run_t, NT_UI_RICH_MAX_RUNS);
    st->styles = NT_MEM_SCRATCH_ALLOC_ARRAY(nt_ui_rich_style_t, NT_UI_RICH_MAX_STYLES);
    st->text = NT_MEM_SCRATCH_ALLOC_ARRAY(char, NT_UI_RICH_MAX_TEXT_BYTES);
    /* solved[]/links[] are sized to the actual content at solve time (content-proportional,
     * not the full glyph cap) -- keeps the per-call scratch footprint tiny for a label. */

    st->stack[0] = base ? *base : nt_ui_rich_style_defaults();
    st->stack_depth = 1;
    ctx->pending_rich = st;
    ctx->rich_session_open = true;
}

void nt_ui_rich_push_color(nt_ui_context_t *ctx, uint32_t color_abgr) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_push_copy(st);
    rich_style_top(st)->color_abgr = color_abgr;
}

void nt_ui_rich_push_scale(nt_ui_context_t *ctx, float mult) {
    NT_ASSERT(mult > 0.0F && isfinite(mult) && "rich scale must be finite > 0 (zero/negative -> negative font size)");
    /* HARD clamp (survives NT_ASSERT OFF): <scale=0> / <scale=-2> feed a non-positive mult here. In OFF
     * the assert is elided, so style.scale would go <=0 -> size = font_size*scale <= 0 into nt_font_measure_n.
     * Fall back to identity (1.0) so OFF stays bounded like every other markup path. */
    if (!(mult > 0.0F) || !isfinite(mult)) {
        mult = 1.0F;
    }
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_push_copy(st);
    rich_style_top(st)->scale *= mult; /* multiplicative */
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

void nt_ui_rich_push_layer(nt_ui_context_t *ctx, uint8_t layer) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_push_copy(st);
    rich_style_top(st)->layer = layer; /* 255 (AUTO) is a valid no-op override; resolved per-kind at atom build */
}

/* Intern a custom (fn,user_data) into the per-call table; returns its custom effect_id (base +
 * index). Dedups identical pairs so a repeated <fx=name> shares a slot (and a table entry). */
static uint8_t rich_intern_custom_fx(nt_ui_rich_state_t *st, nt_ui_rich_fx_fn fn, void *user_data) {
    for (uint32_t i = 0; i < st->custom_fx_count; i++) {
        if (st->custom_fx[i] == fn && st->custom_fx_user[i] == user_data) {
            return (uint8_t)(NT_UI_RICH_FX_CUSTOM_BASE + i);
        }
    }
    /* The cap ceiling is enforced at compile time (see the _Static_assert at the table), so the
     * runtime check is just the table-overflow guard -- the id range can never exhaust first. */
    NT_ASSERT(st->custom_fx_count < NT_UI_RICH_MAX_CUSTOM_FX && "rich custom-effect table overflow");
    /* HARD cap (survives NT_ASSERT OFF): the parser mints a custom slot per distinct <fx=name>;
     * once full, reuse the last slot rather than write past custom_fx[] (a wrong effect beats OOB). */
    if (st->custom_fx_count >= NT_UI_RICH_MAX_CUSTOM_FX) {
        const uint32_t last = NT_UI_RICH_MAX_CUSTOM_FX - 1U;
        st->custom_fx[last] = fn;
        st->custom_fx_user[last] = user_data;
        return (uint8_t)(NT_UI_RICH_FX_CUSTOM_BASE + last);
    }
    const uint32_t idx = st->custom_fx_count++;
    st->custom_fx[idx] = fn;
    st->custom_fx_user[idx] = user_data;
    return (uint8_t)(NT_UI_RICH_FX_CUSTOM_BASE + idx);
}

void nt_ui_rich_push_effect_fn(nt_ui_context_t *ctx, nt_ui_rich_fx_fn fn, void *user_data) {
    NT_ASSERT(fn != NULL && "nt_ui_rich_push_effect_fn: fn must be non-NULL");
    nt_ui_rich_state_t *st = rich_state(ctx);
    const uint8_t id = rich_intern_custom_fx(st, fn, user_data);
    rich_push_copy(st);
    rich_style_top(st)->effect_id = id; /* >= NT_UI_RICH_FX_CUSTOM_BASE -> custom table index */
}

/* Intern a (stock fn, by-value params) into a FRESH custom slot whose user_data points at the
 * block-owned params copy. No dedup: each tuned push gets its own params slot (different params with
 * the same fn must not alias). Returns the custom effect_id. */
static uint8_t rich_intern_stock_ex(nt_ui_rich_state_t *st, nt_ui_rich_fx_fn fn, const nt_ui_rich_fx_params_t *params) {
    NT_ASSERT(st->custom_fx_count < NT_UI_RICH_MAX_CUSTOM_FX && "rich custom-effect table overflow");
    /* HARD cap (survives NT_ASSERT OFF): the parser mints a slot per tuned <fx=name k=v>; once full,
     * reuse the last slot rather than write past custom_fx_params[]. */
    const uint32_t idx = (st->custom_fx_count < NT_UI_RICH_MAX_CUSTOM_FX) ? st->custom_fx_count++ : (NT_UI_RICH_MAX_CUSTOM_FX - 1U);
    st->custom_fx[idx] = fn;
    st->custom_fx_params[idx] = *params;                  /* COPY by value -- read at emit, must be block-owned */
    st->custom_fx_user[idx] = &st->custom_fx_params[idx]; /* stock fn reads params via user_data */
    return (uint8_t)(NT_UI_RICH_FX_CUSTOM_BASE + idx);
}

void nt_ui_rich_push_effect_ex(nt_ui_context_t *ctx, uint8_t stock_id, const nt_ui_rich_fx_params_t *params) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    /* No params -> plain stock path (NULL user_data -> compile-time defaults), byte-identical to push_effect. */
    if (params == NULL) {
        rich_push_copy(st);
        rich_style_top(st)->effect_id = stock_id;
        return;
    }
    nt_ui_rich_fx_fn fn = nt_ui_rich_fx_stock(stock_id);
    NT_ASSERT(fn != NULL && "nt_ui_rich_push_effect_ex: stock_id is not a stock catalog id");
    const uint8_t id = rich_intern_stock_ex(st, fn, params);
    rich_push_copy(st);
    rich_style_top(st)->effect_id = id; /* routed through the per-block custom table -> &params copy at emit */
}

void nt_ui_rich_pop(nt_ui_context_t *ctx) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(st->stack_depth > 1 && "rich pop past base style");
    /* HARD floor (survives NT_ASSERT OFF): this is the LONE style-stack mutator without an `if (cap)`
     * guard. An over-cap or unbalanced close (e.g. balanced </b> beyond what push could honour) would
     * wrap stack_depth below the base style -> later rich_style_top reads stack[(uint32_t)-1] OOB. */
    if (st->stack_depth <= 1U) {
        return;
    }
    st->stack_depth--;
}

void nt_ui_rich_link(nt_ui_context_t *ctx, uint32_t link_id) { rich_state(ctx)->pending_link = link_id; }

/* Finalize a TEXT run over [off, off+len) bytes ALREADY present in st->text (the bytes are not
 * copied here -- the caller wrote them). Shared by nt_ui_rich_text_n (copies first) and the
 * parser's direct accumulation (writes into st->text in place, then finalizes -- one copy, no
 * 4KB stack buffer). Extends the previous run when the composed style/flags/link match. */
static void rich_text_finalize_run(nt_ui_rich_state_t *st, uint32_t off, uint32_t len) {
    if (len == 0U) {
        return;
    }
    bool synth_italic = false;
    const nt_ui_rich_style_t *top = rich_style_top(st);
    (void)rich_resolve_font(top, &synth_italic);
    const uint8_t flags = synth_italic ? NT_UI_RICH_RUN_SYNTH_ITALIC : 0U;
    const uint16_t style_idx = rich_intern_style(st, top);

    if (rich_run_extends_text(st, style_idx, flags, st->pending_link)) {
        st->runs[st->run_count - 1].text_len += len; /* same style -> extend (dedup) */
        return;
    }
    nt_ui_rich_run_t *r = rich_new_run(st, NT_RICH_ATOM_TEXT);
    r->style_idx = style_idx;
    r->flags = flags;
    r->link_id = st->pending_link;
    r->text_off = off;
    r->text_len = len;
}

void nt_ui_rich_text_n(nt_ui_context_t *ctx, const char *utf8, size_t len) {
    if (utf8 == NULL || len == 0U) {
        return;
    }
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(st->text_len + len <= NT_UI_RICH_MAX_TEXT_BYTES && "rich text buffer overflow");
    /* HARD overflow guard (survives NT_ASSERT OFF + cannot wrap): on wasm32 size_t is 32-bit, so
     * `text_len + len` can wrap past the cap and bypass the assert. Subtraction-form bound: reject
     * any len past the cap, and any len that would not fit the remaining room, before the memcpy. */
    if (len > (size_t)NT_UI_RICH_MAX_TEXT_BYTES || st->text_len > NT_UI_RICH_MAX_TEXT_BYTES - (uint32_t)len) {
        return;
    }

    const uint32_t off = st->text_len;
    memcpy(st->text + off, utf8, len);
    st->text_len += (uint32_t)len;
    rich_text_finalize_run(st, off, (uint32_t)len);
}

void nt_ui_rich_image(nt_ui_context_t *ctx, nt_atlas_region_ref_t ref, nt_rich_valign_t valign, float offset_y, float scale) {
    NT_ASSERT(scale > 0.0F && isfinite(scale) && "rich image scale must be finite > 0 (negative -> negative Clay fixed size)");
    /* HARD clamp (survives NT_ASSERT OFF): <img scale=0/-2/NaN/huge> feeds rich_parse_float straight
     * here. In OFF the assert is elided, so a bad scale would multiply the box dims -> 0/NaN box into
     * the solver. Fall back to identity (1.0) so OFF stays bounded, mirroring nt_ui_rich_push_scale. */
    if (!(scale > 0.0F) || !isfinite(scale)) {
        scale = 1.0F;
    }
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
    ctx->rich_session_open = false; /* build phase done; pending_rich kept for solve/emit + probes */
    /* Unbalanced pushes are harmless: each run already captured its composed style at append time. */
}
// #endregion

// #region PARSER
/* Markup tokenizer that DRIVES the shared builder (no second composition path). Contract:
 *  - Escaping: `\` escapes the next byte (`\<` -> literal '<', `\\` -> literal '\').
 *  - Case: tag NAMES are case-insensitive; tag VALUES (hex, hashed names) are case-sensitive.
 *  - Whitespace: preserved verbatim (the solver owns wrap; no collapse).
 * Every scan loop is `len`-bounded so a non-terminating `<` can't OOB or loop. */

/* NT_UI_RICH_PARSE_TAG_DEPTH defined at the top (drives NT_UI_RICH_STACK_DEPTH = it + 1). */

/* The intrinsic tag a `<name ...>` open maps to; CLOSE/IMG handled separately. */
typedef enum {
    RICH_TAG_BOLD,
    RICH_TAG_ITALIC,
    RICH_TAG_COLOR,
    RICH_TAG_SCALE,
    RICH_TAG_FONT,
    RICH_TAG_LINK,
    RICH_TAG_EFFECT, /* <fx=name> via the tagset; pushes effect_id */
    RICH_TAG_LAYER,  /* <layer=N> intrinsic numeric; pushes the z-order band */
    RICH_TAG_NONE,
} rich_tag_kind_t;

/* ASCII lower-case (tag names are case-insensitive; values are not). */
static char rich_lc(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)((unsigned char)c | 0x20U); /* set bit 5 -> lower-case for ASCII letters */
    }
    return c;
}

/* Case-insensitive compare of [s,s+n) against a NUL-terminated lower-case literal. */
static bool rich_name_eq(const char *s, uint32_t n, const char *lit) {
    uint32_t i = 0;
    for (; i < n; i++) {
        if (lit[i] == '\0' || rich_lc(s[i]) != lit[i]) {
            return false;
        }
    }
    return lit[i] == '\0';
}

/* Parse "#RRGGBB" (6 hex digits) into packed AABBGGRR (alpha forced opaque). Markup accepts ONLY
 * #RRGGBB (not #RRGGBBAA) -- length/format is gated here so the two malformed-cases keep distinct
 * warnings; nt_color_parse_hex owns the pure nibble scan. Malformed (untrusted localization) ->
 * log once + opaque white. */
static uint32_t rich_parse_hex_color(const char *s, uint32_t n) {
    if (n != 7U || s[0] != '#') {
        NT_LOG_WARN_UNIQUE("rich markup: <color=#hex> must be #RRGGBB, got '%.*s' -- skipped", (int)n, s);
        return 0xFFFFFFFFU;
    }
    uint32_t packed = 0U;
    if (!nt_color_parse_hex(s, n, &packed)) {
        NT_LOG_WARN_UNIQUE("rich markup: malformed hex in <color=%.*s> -- skipped", (int)n, s);
        return 0xFFFFFFFFU;
    }
    return packed;
}

/* Parse a decimal/float value [s,s+n) (e.g. "1.5"). Malformed (untrusted localization: empty or a
 * non-numeric body) -> log once + safe default 0.0F (for amp/speed 0 means "use default"; the scale
 * paths validate >0 before the builder). Never reads past [s,s+n). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- per-digit scan + degrade branches
static float rich_parse_float(const char *s, uint32_t n) {
    if (n == 0U) {
        NT_LOG_WARN_UNIQUE("rich markup: empty numeric value -- using 0");
        return 0.0F;
    }
    float sign = 1.0F;
    uint32_t i = 0;
    if (s[0] == '-') {
        sign = -1.0F;
        i = 1;
    }
    float whole = 0.0F;
    bool any = false;
    for (; i < n && s[i] != '.'; i++) {
        if (s[i] < '0' || s[i] > '9') {
            NT_LOG_WARN_UNIQUE("rich markup: non-numeric value '%.*s' -- using 0", (int)n, s);
            return 0.0F;
        }
        whole = (whole * 10.0F) + (float)(s[i] - '0');
        any = true;
    }
    float frac = 0.0F;
    float scale = 1.0F;
    if (i < n && s[i] == '.') {
        for (i++; i < n; i++) {
            if (s[i] < '0' || s[i] > '9') {
                NT_LOG_WARN_UNIQUE("rich markup: non-numeric value '%.*s' -- using 0", (int)n, s);
                return 0.0F;
            }
            scale *= 0.1F;
            frac += (float)(s[i] - '0') * scale;
            any = true;
        }
    }
    if (!any) {
        NT_LOG_WARN_UNIQUE("rich markup: numeric value '%.*s' had no digits -- using 0", (int)n, s);
        return 0.0F;
    }
    return sign * (whole + frac);
}

/* Map an open-tag NAME (the part before '=' or '>') to its intrinsic kind. */
static rich_tag_kind_t rich_tag_kind(const char *name, uint32_t n) {
    if (rich_name_eq(name, n, "b")) {
        return RICH_TAG_BOLD;
    }
    if (rich_name_eq(name, n, "i")) {
        return RICH_TAG_ITALIC;
    }
    if (rich_name_eq(name, n, "color")) {
        return RICH_TAG_COLOR;
    }
    if (rich_name_eq(name, n, "scale")) {
        return RICH_TAG_SCALE;
    }
    if (rich_name_eq(name, n, "font")) {
        return RICH_TAG_FONT;
    }
    if (rich_name_eq(name, n, "link")) {
        return RICH_TAG_LINK;
    }
    if (rich_name_eq(name, n, "fx")) {
        return RICH_TAG_EFFECT;
    }
    if (rich_name_eq(name, n, "layer")) {
        return RICH_TAG_LAYER;
    }
    return RICH_TAG_NONE;
}

/* Parse a bounded decimal uint8 layer [s,s+n) for <layer=N>. Malformed/out-of-range (untrusted
 * markup: empty, non-digit, or > NT_UI_RICH_LAYER_MAX) -> log once + AUTO (per-kind default), never OOB. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- per-digit scan + degrade branches (mirrors rich_parse_float)
static uint8_t rich_parse_layer(const char *s, uint32_t n) {
    if (n == 0U) {
        NT_LOG_WARN_UNIQUE("rich markup: <layer=> needs a value -- using AUTO");
        return (uint8_t)NT_UI_RICH_LAYER_AUTO;
    }
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            NT_LOG_WARN_UNIQUE("rich markup: <layer=%.*s> must be a decimal uint -- using AUTO", (int)n, s);
            return (uint8_t)NT_UI_RICH_LAYER_AUTO; /* non-numeric -> skip, never OOB */
        }
        v = (v * 10U) + (uint32_t)(s[i] - '0');
        if (v > NT_UI_RICH_LAYER_MAX) {
            v = NT_UI_RICH_LAYER_MAX + 1U; /* saturate; the range log below fires once */
            break;
        }
    }
    if (v > NT_UI_RICH_LAYER_MAX) {
        NT_LOG_WARN_UNIQUE("rich markup: <layer=%.*s> exceeds 254 (255 is the AUTO sentinel) -- using AUTO", (int)n, s);
        return (uint8_t)NT_UI_RICH_LAYER_AUTO; /* clamp out-of-range to AUTO (per-kind default) */
    }
    return (uint8_t)v;
}

/* pushed_style records whether THIS open actually pushed a style entry (so close pops iff it did).
 * A named-tag miss (unknown <font=>/<color=>/<fx=>) or <link> opens with NO style push -> false; its
 * close must not pop the enclosing style (OFF-safe -- in DEBUG the miss already trapped at the open). */
typedef struct {
    rich_tag_kind_t kind;
    bool pushed_style;
} rich_tag_open_t;

typedef struct {
    rich_tag_open_t open_stack[NT_UI_RICH_PARSE_TAG_DEPTH];
    uint32_t depth;
} rich_tag_stack_t;

/* Map an <img valign=> keyword to its enum; 0xFF on unknown so the caller can hard-skip (OFF-safe). */
static uint8_t rich_parse_valign(const char *s, uint32_t n) {
    if (rich_name_eq(s, n, "baseline")) {
        return (uint8_t)NT_RICH_VALIGN_BASELINE;
    }
    if (rich_name_eq(s, n, "middle")) {
        return (uint8_t)NT_RICH_VALIGN_MIDDLE;
    }
    if (rich_name_eq(s, n, "top")) {
        return (uint8_t)NT_RICH_VALIGN_TOP;
    }
    if (rich_name_eq(s, n, "bottom")) {
        return (uint8_t)NT_RICH_VALIGN_BOTTOM;
    }
    return 0xFFU;
}

/* Parse the optional attribute tail of `<img=region scale=1.8 oy=-4 valign=middle/>` ([s,s+n) = the
 * bytes AFTER the region spec, including the leading space(s)). Each space-separated token is
 * `key=value` (key: scale|oy|valign). Writes through valign/oy/scale (left untouched when a key is
 * absent, so the caller's defaults stand). Malformed (no '=', bad float, unknown key/valign) ->
 * log once + skip that token (no OOB, never loops; untrusted localization data). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- bounded token scan + per-key degrade (mirrors rich_parse_fx_params)
static void rich_parse_img_attrs(const char *s, uint32_t n, nt_rich_valign_t *valign, float *oy, float *scale) {
    uint32_t i = 0;
    while (i < n) {
        while (i < n && s[i] == ' ') { /* skip the separator run */
            i++;
        }
        if (i >= n) {
            break;
        }
        const uint32_t tok = i;
        while (i < n && s[i] != ' ') { /* span one key=value token */
            i++;
        }
        const uint32_t tlen = i - tok;
        uint32_t eq = tlen;
        for (uint32_t k = 0; k < tlen; k++) {
            if (s[tok + k] == '=') {
                eq = k;
                break;
            }
        }
        /* A token with no '=' (eq==tlen) leaves vvlen = tlen-eq-1 underflowing to UINT_MAX, so the
         * value readers would run massively OOB. Log once + skip such a token (untrusted data). */
        if (eq >= tlen || eq == 0U) {
            NT_LOG_WARN_UNIQUE("rich markup: <img k=v> attr needs key=value, got '%.*s' -- skipped", (int)tlen, s + tok);
            continue;
        }
        const char *key = s + tok;
        const char *vv = s + tok + eq + 1U;
        const uint32_t vvlen = tlen - eq - 1U;
        if (rich_name_eq(key, eq, "scale")) {
            *scale = rich_parse_float(vv, vvlen);
        } else if (rich_name_eq(key, eq, "oy")) {
            *oy = rich_parse_float(vv, vvlen);
        } else if (rich_name_eq(key, eq, "valign")) {
            const uint8_t v = rich_parse_valign(vv, vvlen);
            if (v != 0xFFU) {
                *valign = (nt_rich_valign_t)v;
            } else {
                NT_LOG_WARN_UNIQUE("rich markup: <img valign=%.*s> must be baseline|middle|top|bottom -- kept default", (int)vvlen, vv);
            }
        } else {
            NT_LOG_WARN_UNIQUE("rich markup: <img k=v> unknown attr key '%.*s' (use scale|oy|valign) -- skipped", (int)eq, key);
        }
    }
}

/* Apply a self-closing `<img=...>`: `<img=region/>` -> default atlas; `<img=alias:region/>` ->
 * the tagset alias's atlas. An optional space-separated attr tail (`scale=`/`oy=`/`valign=`) follows
 * the region spec. The region name is always resolved by name (the atlas IS the registry). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- alias-split scan + attr-tail + degrade branches
static void rich_parse_img(nt_ui_context_t *ctx, const nt_ui_rich_tagset_t *tagset, const nt_ui_rich_style_t *base, const char *val, uint32_t vlen) {
    /* nt_ui_rich_begin EXPLICITLY allows base==NULL (text-only markup). An <img> in such markup has
     * no default_atlas to resolve against -> log + skip (untrusted data), never deref. */
    if (base == NULL) {
        NT_LOG_WARN_UNIQUE("rich markup: <img=%.*s/> needs a base style (no default_atlas) -- skipped", (int)vlen, val);
        return;
    }
    if (vlen == 0U) {
        NT_LOG_WARN_UNIQUE("rich markup: <img=...> needs a region -- skipped");
        return;
    }
    /* Split the value at the FIRST space into [region_spec, attr_tail]; the tail may be empty. */
    uint32_t region_spec_len = vlen;
    for (uint32_t i = 0; i < vlen; i++) {
        if (val[i] == ' ') {
            region_spec_len = i;
            break;
        }
    }
    const char *attr_tail = val + region_spec_len;
    const uint32_t attr_len = vlen - region_spec_len;
    /* An empty region spec (leading space) resolves nothing -> log + skip. */
    if (region_spec_len == 0U) {
        NT_LOG_WARN_UNIQUE("rich markup: <img=%.*s/> needs a region before any attrs -- skipped", (int)vlen, val);
        return;
    }
    /* Split on a single ':' -> alias:region (within the region spec only). */
    uint32_t colon = region_spec_len;
    for (uint32_t i = 0; i < region_spec_len; i++) {
        if (val[i] == ':') {
            colon = i;
            break;
        }
    }
    nt_resource_t atlas = base->default_atlas.atlas;
    const char *region = val;
    uint32_t region_len = region_spec_len;
    if (colon < region_spec_len) {
        /* No tagset -> can't resolve the alias -> log + skip the image (mirrors the unresolved-name
         * skip below). Plain <img=region/> never reaches here. */
        if (tagset == NULL) {
            NT_LOG_WARN_UNIQUE("rich markup: <img=%.*s/> alias needs a tagset -- skipped", (int)region_spec_len, val);
            return;
        }
        const uint64_t alias_hash = nt_hash64((const void *)val, colon).value;
        const bool alias_ok = nt_ui_rich_tagset_lookup_atlas(tagset, alias_hash, &atlas);
        /* Unknown alias -> log + skip the image, never fall back to the default atlas with the wrong
         * region (mirrors the unresolved-name skip in rich_emit_images). */
        if (!alias_ok) {
            NT_LOG_WARN_UNIQUE("rich markup: unknown <img> atlas alias '%.*s' -- skipped", (int)colon, val);
            return;
        }
        region = val + colon + 1;
        region_len = region_spec_len - colon - 1;
        if (region_len == 0U) {
            NT_LOG_WARN_UNIQUE("rich markup: <img=%.*s/> has an empty region after the alias -- skipped", (int)region_spec_len, val);
            return;
        }
    }
    /* Fallback defaults; the attr tail overrides any key it sets. */
    nt_rich_valign_t valign = NT_RICH_VALIGN_MIDDLE;
    float oy = 0.0F;
    float scale = 1.0F;
    rich_parse_img_attrs(attr_tail, attr_len, &valign, &oy, &scale);
    /* Validate scale BEFORE the builder: rich_parse_float returns 0.0F on a bad/empty value, and the
     * builder's own scale>0 assert would TRAP a direct-code 0. For untrusted markup, degrade to 1.0. */
    if (!(scale > 0.0F) || !isfinite(scale)) {
        NT_LOG_WARN_UNIQUE("rich markup: <img=%.*s/> scale must be finite > 0 -- using 1.0", (int)vlen, val);
        scale = 1.0F;
    }
    const uint64_t region_hash = nt_hash64((const void *)region, region_len).value;
    nt_ui_rich_image(ctx, nt_atlas_ref(atlas, region_hash), valign, oy, scale);
}

/* `<obj=name/>` self-close: a game-drawn WidgetSpan (measure_fn sizes the box, draw_fn paints it).
 * Name resolved via the tagset using the SAME hash as register_object_tag (nt_hash64_str -> the full
 * name bytes), which equals nt_hash64(val, vlen) here since [val,val+vlen) IS the name. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- empty name + unknown/NULL-measure degrade branches
static void rich_parse_obj(nt_ui_context_t *ctx, const nt_ui_rich_tagset_t *tagset, const char *val, uint32_t vlen) {
    /* No tagset -> the lookup below would deref NULL on untrusted markup (<obj=x/> with a text-only,
     * tagset-less config). Log + skip the object. */
    if (tagset == NULL) {
        NT_LOG_WARN_UNIQUE("rich markup: <obj=%.*s/> needs a tagset -- skipped", (int)vlen, val);
        return;
    }
    /* Empty name resolves nothing -> never push a bogus object. */
    if (vlen == 0U) {
        NT_LOG_WARN_UNIQUE("rich markup: <obj/> needs a name -- skipped");
        return;
    }
    nt_ui_rich_tagset_object_t obj = {0};
    const bool obj_ok = nt_ui_rich_tagset_lookup_object(tagset, nt_hash64((const void *)val, vlen).value, &obj);
    /* Unknown name or a (defensively) NULL measure_fn -> log + skip; nt_ui_rich_object asserts
     * measure_fn != NULL, but register_object_tag rejects NULL measure so this only fires on miss. */
    if (!obj_ok || obj.measure_fn == NULL) {
        NT_LOG_WARN_UNIQUE("rich markup: unknown <obj=%.*s/> -- skipped", (int)vlen, val);
        return;
    }
    nt_ui_rich_object(ctx, obj.measure_fn, obj.draw_fn, obj.user_data);
}

/* Parse the param tail of `<fx=name amp=8 speed=3>` ([s,s+n) = the bytes AFTER the name, including
 * the leading space(s)). Each space-separated token is `key=value` (key: amp|speed; value: float).
 * Writes into *out; returns true if >=1 param was set. Malformed (bad float, unknown key, no '=')
 * -> log once + skip that token (untrusted localization). An empty/all-space tail returns false. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- bounded token scan + per-key degrade
static bool rich_parse_fx_params(const char *s, uint32_t n, nt_ui_rich_fx_params_t *out) {
    bool any = false;
    uint32_t i = 0;
    while (i < n) {
        while (i < n && s[i] == ' ') { /* skip the separator run */
            i++;
        }
        if (i >= n) {
            break;
        }
        const uint32_t tok = i;
        while (i < n && s[i] != ' ') { /* span one key=value token */
            i++;
        }
        const uint32_t tlen = i - tok;
        /* Split the token on '='. */
        uint32_t eq = tlen;
        for (uint32_t k = 0; k < tlen; k++) {
            if (s[tok + k] == '=') {
                eq = k;
                break;
            }
        }
        /* A token with no '=' (<fx=wave amp>) leaves eq==tlen, so vvlen = tlen-eq-1 underflows to
         * UINT_MAX and rich_parse_float(vv, UINT_MAX) reads massively OOB. eq==0 (<fx=wave =3>) gives
         * an empty key. Log once + skip a token that isn't key=value (untrusted data). */
        if (eq >= tlen || eq == 0U) {
            NT_LOG_WARN_UNIQUE("rich markup: <fx=name k=v> param needs key=value, got '%.*s' -- skipped", (int)tlen, s + tok);
            continue;
        }
        const char *key = s + tok;
        const char *vv = s + tok + eq + 1U;
        const uint32_t vvlen = tlen - eq - 1U;
        if (rich_name_eq(key, eq, "amp")) {
            out->amp = rich_parse_float(vv, vvlen);
            any = true;
        } else if (rich_name_eq(key, eq, "speed")) {
            out->speed = rich_parse_float(vv, vvlen);
            any = true;
        } else {
            NT_LOG_WARN_UNIQUE("rich markup: <fx=name k=v> unknown param key '%.*s' (use amp|speed) -- skipped", (int)eq, key);
        }
    }
    return any;
}

/* Dispatch one OPEN tag `<name=value>` (value may be empty) onto the builder + tag stack. Untrusted
 * localization: an unknown name / unresolved value / missing value LOGS once + skips the tag (pushes
 * no style); the matching close then no-ops, preserving the enclosing styles. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- tag-kind switch + per-tag degrade branches
static void rich_open_tag(nt_ui_context_t *ctx, rich_tag_stack_t *ts_stack, const nt_ui_rich_tagset_t *tagset, const char *name, uint32_t nlen, const char *val, uint32_t vlen) {
    const rich_tag_kind_t kind = rich_tag_kind(name, nlen);
    /* Unknown tag name -> log + skip WITHOUT pushing the tag stack (so a later matching close, itself
     * unknown, also no-ops). Never an assert: tag names come from untrusted markup. */
    if (kind == RICH_TAG_NONE) {
        NT_LOG_WARN_UNIQUE("rich markup: unknown tag '%.*s' -- skipped", (int)nlen, name);
        return;
    }
    /* HARD cap (checked BEFORE the style push): untrusted nesting must not write past open_stack[], and
     * the matching builder-style push must not over-cap rich_push_copy (whose assert is a code
     * invariant we keep). Skip the whole over-deep tag -- no style push, no tag-stack record. */
    if (ts_stack->depth >= NT_UI_RICH_PARSE_TAG_DEPTH) {
        NT_LOG_WARN_UNIQUE("rich markup: tag nesting too deep (> %u) -- tag '%.*s' skipped", (unsigned)NT_UI_RICH_PARSE_TAG_DEPTH, (int)nlen, name);
        return;
    }
    /* Set true ONLY where a style is actually pushed below. A named-tag miss (unknown font/color/fx)
     * or a tagset-less skip leaves it false so the matching close does NOT pop the enclosing style. */
    bool pushed_style = false;
    switch (kind) {
    case RICH_TAG_BOLD:
        nt_ui_rich_push_bold(ctx);
        pushed_style = true;
        break;
    case RICH_TAG_ITALIC:
        nt_ui_rich_push_italic(ctx);
        pushed_style = true;
        break;
    case RICH_TAG_COLOR:
        if (vlen == 0U) {
            NT_LOG_WARN_UNIQUE("rich markup: <color=> needs a value -- skipped");
            break;
        }
        if (val[0] == '#') {
            nt_ui_rich_push_color(ctx, rich_parse_hex_color(val, vlen));
            pushed_style = true;
        } else {
            /* No tagset -> the named-color lookup would deref NULL. Log + skip. */
            if (tagset == NULL) {
                NT_LOG_WARN_UNIQUE("rich markup: named <color=%.*s> needs a tagset -- skipped", (int)vlen, val);
                break;
            }
            uint32_t abgr = 0;
            const bool color_ok = nt_ui_rich_tagset_lookup_color(tagset, nt_hash64((const void *)val, vlen).value, &abgr);
            /* Unknown name -> never push the unresolved 0 colour. Log + skip. */
            if (!color_ok) {
                NT_LOG_WARN_UNIQUE("rich markup: unknown color name '%.*s' -- skipped", (int)vlen, val);
                break;
            }
            nt_ui_rich_push_color(ctx, abgr);
            pushed_style = true;
        }
        break;
    case RICH_TAG_SCALE: {
        /* Validate BEFORE the builder: rich_parse_float returns 0.0F on bad/empty, and push_scale's
         * own >0 assert would TRAP a direct-code 0. For untrusted markup, degrade to identity 1.0. */
        float mult = rich_parse_float(val, vlen);
        if (!(mult > 0.0F) || !isfinite(mult)) {
            NT_LOG_WARN_UNIQUE("rich markup: <scale=%.*s> must be finite > 0 -- using 1.0", (int)vlen, val);
            mult = 1.0F;
        }
        nt_ui_rich_push_scale(ctx, mult);
        pushed_style = true;
        break;
    }
    case RICH_TAG_FONT: {
        if (vlen == 0U || tagset == NULL) {
            NT_LOG_WARN_UNIQUE("rich markup: <font=%.*s> needs a tagset + name -- skipped", (int)vlen, val);
            break;
        }
        nt_font_t fam[4];
        const bool font_ok = nt_ui_rich_tagset_lookup_font(tagset, nt_hash64((const void *)val, vlen).value, fam);
        /* On a miss fam[4] is UNINITIALISED -> skip, never push garbage. Log + skip. */
        if (!font_ok) {
            NT_LOG_WARN_UNIQUE("rich markup: unknown font name '%.*s' -- skipped", (int)vlen, val);
            break;
        }
        nt_ui_rich_push_font(ctx, fam);
        pushed_style = true;
        break;
    }
    case RICH_TAG_LINK: {
        if (vlen == 0U) {
            NT_LOG_WARN_UNIQUE("rich markup: <link=> needs an id -- skipped");
            break;
        }
        /* No nested links (HTML's no-nested-anchor rule): pending_link is a single scalar, so an inner
         * </link> would zero the outer's id -- text between the inner and outer close would silently
         * lose the enclosing link. Log + skip the inner link rather than compose wrong. */
        bool nested = false;
        for (uint32_t d = 0; d < ts_stack->depth; d++) {
            if (ts_stack->open_stack[d].kind == RICH_TAG_LINK) {
                nested = true;
                break;
            }
        }
        if (nested) {
            NT_LOG_WARN_UNIQUE("rich markup: nested <link=%.*s> is not allowed -- skipped", (int)vlen, val);
            break;
        }
        const uint32_t link_id = nt_hash32(val, vlen).value;
        /* A name that hashes to 0 collides with the none sentinel -> drop the link rather than mark a
         * span as "no link". Log + skip (the text stays, just non-clickable). */
        if (link_id == 0U) {
            NT_LOG_WARN_UNIQUE("rich markup: <link=%.*s> hashed to 0 (the none sentinel) -- skipped", (int)vlen, val);
            break;
        }
        nt_ui_rich_link(ctx, link_id);
        break;
    }
    case RICH_TAG_EFFECT: {
        if (vlen == 0U || tagset == NULL) {
            NT_LOG_WARN_UNIQUE("rich markup: <fx=%.*s> needs a tagset + name -- skipped", (int)vlen, val);
            break;
        }
        /* The value is `name` followed by space-separated `key=value` params (keys: amp, speed). Split
         * the name off the first space; the remainder (if any) parses into nt_ui_rich_fx_params_t. */
        uint32_t nlen_fx = vlen;
        for (uint32_t k = 0; k < vlen; k++) {
            if (val[k] == ' ') {
                nlen_fx = k;
                break;
            }
        }
        if (nlen_fx == 0U) {
            NT_LOG_WARN_UNIQUE("rich markup: <fx=%.*s> empty effect name -- skipped", (int)vlen, val);
            break;
        }
        nt_ui_rich_fx_params_t params = {0};
        const bool has_params = rich_parse_fx_params(val + nlen_fx, vlen - nlen_fx, &params);

        uint8_t effect_id = 0;
        nt_ui_rich_fx_fn fn = NULL;
        void *fx_user = NULL;
        const bool fx_ok = nt_ui_rich_tagset_lookup_effect_fn(tagset, nt_hash64((const void *)val, nlen_fx).value, &effect_id, &fn, &fx_user);
        /* Unknown name -> never push effect_id 0 as a real effect. Log + skip. */
        if (!fx_ok) {
            NT_LOG_WARN_UNIQUE("rich markup: unknown effect name '%.*s' -- skipped", (int)nlen_fx, val);
            break;
        }
        if (fn != NULL) {
            /* Markup k=v params apply to STOCK effects only: a custom fn carries its own user_data.
             * Log + push the custom fn IGNORING the params (the fn is valid, only the params don't apply). */
            if (has_params) {
                NT_LOG_WARN_UNIQUE("rich markup: <fx=%.*s k=v> params apply to STOCK effects only -- params ignored", (int)nlen_fx, val);
            }
            nt_ui_rich_push_effect_fn(ctx, fn, fx_user); /* custom resolves before stock */
        } else if (has_params) {
            nt_ui_rich_push_effect_ex(ctx, effect_id, &params);
        } else {
            nt_ui_rich_push_effect(ctx, effect_id);
        }
        pushed_style = true;
        break;
    }
    case RICH_TAG_LAYER:
        /* <layer=> with no value -> rich_parse_layer logs + returns AUTO (a valid no-op band). Still a
         * style push (AUTO override) so the matching close balances; no separate empty-value skip. */
        nt_ui_rich_push_layer(ctx, rich_parse_layer(val, vlen));
        pushed_style = true;
        break;
    case RICH_TAG_NONE:
    default:
        /* Unreachable: RICH_TAG_NONE is handled at the top. A genuine CODE invariant -> keep the assert. */
        NT_ASSERT(false && "rich markup: unreachable tag kind");
        break;
    }
    /* link is a pending property, not a stack push -- closed by </link> clearing the pending id.
     * depth < NT_UI_RICH_PARSE_TAG_DEPTH is guaranteed by the top-of-function cap, so this record is
     * always in-bounds and stays balanced with the builder style stack. */
    ts_stack->open_stack[ts_stack->depth].kind = kind;
    ts_stack->open_stack[ts_stack->depth].pushed_style = pushed_style;
    ts_stack->depth++;
}

/* Dispatch one CLOSE tag `</name>` -- should match the innermost open tag. Untrusted localization:
 * an unknown close name, an orphan close (no open), or a mismatched close LOGS once + no-ops (never
 * an assert, never an OOB), so enclosing styles are preserved. */
static void rich_close_tag(nt_ui_context_t *ctx, rich_tag_stack_t *ts_stack, const char *name, uint32_t nlen) {
    const rich_tag_kind_t kind = rich_tag_kind(name, nlen);
    if (kind == RICH_TAG_NONE) {
        NT_LOG_WARN_UNIQUE("rich markup: unknown close tag '</%.*s>' -- skipped", (int)nlen, name);
        return;
    }
    /* An orphan close (</b> with no open) would do --depth on depth==0 -> wraps to UINT_MAX ->
     * open_stack[UINT_MAX] OOB. Log + bail before the decrement. */
    if (ts_stack->depth == 0U) {
        NT_LOG_WARN_UNIQUE("rich markup: close tag '</%.*s>' with no open -- skipped", (int)nlen, name);
        return;
    }
    const rich_tag_open_t top = ts_stack->open_stack[--ts_stack->depth];
    /* Mismatched close (e.g. <b>..</i>): the tag structure is malformed data. We already popped the
     * tag stack (the </i> closes the <b> slot), but balance the BUILDER on what the OPEN actually
     * pushed (top), not on the close kind -- never an underflow-pop. Log it. */
    if (top.kind != kind) {
        NT_LOG_WARN_UNIQUE("rich markup: mismatched close '</%.*s>' -- closing the open tag instead", (int)nlen, name);
    }
    /* Decide the pop on what the matching OPEN actually did, not the close tag's kind. <link> opens
     * as a pending field (no style entry) -> clear it. A named-tag MISS (unknown font/color/fx) or a
     * tagset-less skip opened WITHOUT a style push -> top.pushed_style is false -> pop nothing, or the
     * close would pop the enclosing style (e.g. <b><font=typo>x</font>y</b> would lose y's bold).
     * Pop ONLY when the open really pushed a style. For well-formed markup this matches the old path. */
    if (top.kind == RICH_TAG_LINK) {
        nt_ui_rich_link(ctx, 0U); /* clear the pending link id */
    } else if (top.pushed_style) {
        nt_ui_rich_pop(ctx); /* style-stack pop -- balances the open's push */
    }
}

/* Validate a literal text segment is well-formed UTF-8. A localized/interpolated string with a corrupt
 * byte sequence is UNTRUSTED data: log once + report the segment must be SKIPPED (caller drops it),
 * never feeding raw invalid bytes downstream. Bounded by n -- a trailing incomplete sequence
 * (state != ACCEPT at the end) is also invalid. Returns true iff [buf,buf+n) is valid UTF-8. */
static bool rich_text_segment_valid_utf8(const char *buf, uint32_t n) {
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t cp = 0;
    for (uint32_t i = 0; i < n; i++) {
        nt_utf8_decode(&state, &cp, (uint8_t)buf[i]);
        if (state == NT_UTF8_REJECT) {
            NT_LOG_WARN_UNIQUE("rich markup: invalid UTF-8 in text -- segment skipped");
            return false;
        }
    }
    if (state != NT_UTF8_ACCEPT) {
        NT_LOG_WARN_UNIQUE("rich markup: truncated UTF-8 sequence in text -- segment skipped");
        return false;
    }
    return true;
}

/* Expected total byte length of the UTF-8 sequence a lead byte begins (0 = not a lead byte). */
static uint32_t rich_utf8_seq_len(uint8_t lead) {
    if (lead < 0x80U) {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 0U; /* continuation byte or invalid lead */
}

/* Largest m <= n such that buf[0,m) ends on a COMPLETE UTF-8 codepoint boundary. A buffer-full
 * truncation can sever a multi-byte sequence mid-codepoint; trimming the partial tail keeps the
 * flushed run valid UTF-8 (else rich_text_segment_valid_utf8 would log+drop an otherwise-valid run). */
static uint32_t rich_utf8_complete_prefix(const char *buf, uint32_t n) {
    uint32_t m = n;
    while (m > 0U && ((uint8_t)buf[m - 1U] & 0xC0U) == 0x80U) {
        m--; /* back over trailing continuation bytes to the lead */
    }
    if (m == 0U) {
        return n; /* all continuation bytes (no lead in range) -> let the validator catch it */
    }
    const uint32_t expect = rich_utf8_seq_len((uint8_t)buf[m - 1U]);
    const uint32_t have = n - (m - 1U);
    return (expect != 0U && have < expect) ? (m - 1U) : n; /* incomplete trailing sequence -> drop it whole */
}

/* Append one literal byte directly into the run-list text buffer (no intermediate stack copy).
 * Hitting the shared 4KB cap on long UNTRUSTED markup is graceful truncation, not a dev error: drop
 * the byte (no write past text[NT_UI_RICH_MAX_TEXT_BYTES]). rich_flush_text trims any partial trailing
 * codepoint a cap-truncation may have left, so a severed multi-byte sequence never reaches the validator. */
static void rich_parse_append_byte(nt_ui_rich_state_t *st, char b) {
    if (st->text_len >= NT_UI_RICH_MAX_TEXT_BYTES) {
        return;
    }
    st->text[st->text_len++] = b;
}

/* Flush the literal segment [seg_off, st->text_len): validate UTF-8 then finalize the run over the
 * bytes already in st->text (no second copy). Resets *seg_off to the new cursor. */
static void rich_flush_text(nt_ui_rich_state_t *st, uint32_t *seg_off) {
    uint32_t n = st->text_len - *seg_off;
    /* Drop a partial trailing codepoint a buffer-full truncation may have severed: rewind text_len so
     * the next segment also starts on a clean boundary (only shrinks when AT the cap -- the common
     * case is a no-op). The validator below then never sees a lone lead/continuation byte. */
    const uint32_t kept = rich_utf8_complete_prefix(st->text + *seg_off, n);
    if (kept < n) {
        st->text_len = *seg_off + kept;
        n = kept;
    }
    if (n > 0U && rich_text_segment_valid_utf8(st->text + *seg_off, n)) {
        rich_text_finalize_run(st, *seg_off, n); /* invalid UTF-8 -> skip the segment (logged), don't append */
    }
    *seg_off = st->text_len;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- the tokenizer: text/tag/escape branches + bounded scans
void nt_ui_rich_parse(nt_ui_context_t *ctx, const nt_ui_rich_tagset_t *tagset, const nt_ui_rich_style_t *base, const char *markup, size_t len) {
    NT_ASSERT(ctx != NULL && "nt_ui_rich_parse: NULL ctx");
    NT_ASSERT(markup != NULL || len == 0U);

    nt_ui_rich_begin(ctx, base);
    nt_ui_rich_state_t *st = rich_state(ctx);
    rich_tag_stack_t ts_stack;
    ts_stack.depth = 0;

    /* Literal text accumulates DIRECTLY into the run-list text buffer (st->text); seg_off marks
     * the current segment start. Flush at every tag boundary -> one copy, no 4KB stack frame. */
    uint32_t seg_off = st->text_len;

    size_t i = 0;
    while (i < len) {
        const char c = markup[i];
        if (c == '\\' && (i + 1U) < len) {
            rich_parse_append_byte(st, markup[i + 1U]); /* escape: next byte is literal (\< -> '<') */
            i += 2U;
            continue;
        }
        if (c != '<') {
            rich_parse_append_byte(st, c);
            i++;
            continue;
        }
        /* A '<' begins a tag. Flush any pending literal text first. */
        rich_flush_text(st, &seg_off);
        /* Find the matching '>' (bounded by len -> a non-terminating '<' stops, never loops). */
        size_t close = i + 1U;
        while (close < len && markup[close] != '>') {
            close++;
        }
        /* No '>' before len -> body would start at markup+len and body[0] would read 1 byte OOB.
         * Untrusted localization: log + stop the tokenizer without dereferencing body. */
        if (close >= len) {
            NT_LOG_WARN_UNIQUE("rich markup: unterminated tag (no '>') -- truncated");
            break;
        }

        const char *body = markup + i + 1U; /* between '<' and '>' */
        uint32_t blen = (uint32_t)(close - (i + 1U));
        /* An empty `<>` is meaningless. Log + skip it past the '>' without dispatching. */
        if (blen == 0U) {
            NT_LOG_WARN_UNIQUE("rich markup: empty tag <> -- skipped");
            i = close + 1U;
            continue;
        }

        const bool is_close = (body[0] == '/');
        const bool self_close = (blen >= 1U && body[blen - 1U] == '/');
        if (is_close) {
            const char *name = body + 1;
            const uint32_t nlen = blen - 1U;
            rich_close_tag(ctx, &ts_stack, name, nlen);
        } else {
            /* Trim a trailing '/' for self-closing tags. */
            uint32_t eff = self_close ? (blen - 1U) : blen;
            /* Split name=value. */
            uint32_t eq = eff;
            for (uint32_t k = 0; k < eff; k++) {
                if (body[k] == '=') {
                    eq = k;
                    break;
                }
            }
            const char *name = body;
            const uint32_t nlen = eq;
            const char *val = (eq < eff) ? (body + eq + 1U) : body;
            const uint32_t vlen = (eq < eff) ? (eff - eq - 1U) : 0U;
            if (self_close) {
                if (rich_name_eq(name, nlen, "img")) {
                    rich_parse_img(ctx, tagset, base, val, vlen);
                } else if (rich_name_eq(name, nlen, "obj")) {
                    rich_parse_obj(ctx, tagset, val, vlen);
                } else {
                    /* Any other self-closing name is unsupported markup -> log + skip. */
                    NT_LOG_WARN_UNIQUE("rich markup: only <img=.../> or <obj=.../> self-close, got '%.*s' -- skipped", (int)nlen, name);
                }
            } else {
                rich_open_tag(ctx, &ts_stack, tagset, name, nlen, val, vlen);
            }
        }
        i = close + 1U;
    }
    /* Flush the trailing literal text. */
    rich_flush_text(st, &seg_off);
    /* Unclosed tag(s) at end of string: untrusted localization left the tag stack non-empty. Each run
     * already captured its composed style at append time (nt_ui_rich_end tolerates unbalanced pushes),
     * so this is harmless -> log once + continue. */
    if (ts_stack.depth != 0U) {
        NT_LOG_WARN_UNIQUE("rich markup: %u unclosed tag(s) at end of string -- ignored", (unsigned)ts_stack.depth);
    }
    nt_ui_rich_end(ctx);
}
// #endregion

// #region SOLVER
/* Word-wrap + baseline solver over an ATOM stream (the wrap unit, not runs -- a word can span runs):
 * PASS 1 greedy break (break-anywhere for over-long words); PASS 2 per-line ascent/descent + L/C/R
 * align + baseline placement. Scratch-only, no heap. */

/* Internal layout atom: the unit of wrap/position. TEXT runs decompose into word atoms
 * (a trailing space stays with the word -> a break opportunity after); an over-long word
 * splits into break-anywhere chunks. IMAGE/OBJECT are one unbreakable atom each. */
typedef struct {
    nt_rich_atom_kind_t kind;
    float advance; /* x cost on the line */
    float w;       /* box width */
    float h;       /* box height (text line-box / image box) */
    float asc;     /* px above baseline demanded */
    float desc;    /* px below baseline demanded */
    nt_rich_valign_t valign;
    float offset_y;
    bool breakable_before; /* a break opportunity exists before this atom */
    uint32_t line;
    /* TEXT placement context (for emit). */
    nt_font_t font;
    float size;
    uint32_t color;
    uint32_t text_off;
    uint32_t text_len;
    uint8_t flags;
    uint8_t effect_id; /* stock effect catalog index from the run's style; 0 = none */
    uint8_t layer;     /* EFFECTIVE z-order band (per-kind default already resolved from style.layer AUTO) */
    uint16_t run_idx;
    uint32_t link_id;
} rich_atom_t;

/* x_height proxy: half the text ascent. Stable non-ascent reference so the
 * middle valign differs from baseline. */
static float rich_x_height(float ascent_px) { return ascent_px * 0.5F; }

/* Count UTF-8 codepoints in [off,off+len). Drives the running per-glyph effect index so an
 * effect's phase progresses monotonically across atoms instead of repeating per word. */
static uint32_t rich_glyph_count(const char *text, uint32_t off, uint32_t len) {
    uint32_t n = 0;
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t cp = 0;
    const uint32_t end = off + len;
    for (uint32_t i = off; i < end; i++) {
        if (nt_utf8_decode(&state, &cp, (uint8_t)text[i]) == NT_UTF8_ACCEPT) {
            n++;
        }
    }
    return n;
}

static void rich_text_metrics(nt_font_t font, float size, float *out_asc, float *out_desc) {
    const nt_font_metrics_t m = nt_font_get_metrics(font);
    if (m.units_per_em == 0U) {
        *out_asc = size;
        *out_desc = 0.0F;
        return;
    }
    const float px = size / (float)m.units_per_em; /* px = size/units_per_em, same scale as the input field's glyph metrics */
    *out_asc = (float)m.ascent * px;
    *out_desc = -(float)m.descent * px;
}

/* Resolve a style's z-order band: AUTO -> per-kind default (TEXT<IMAGE<OBJECT); else the explicit layer. */
static uint8_t rich_effective_layer(const nt_ui_rich_style_t *style, nt_rich_atom_kind_t kind) {
    if (style->layer != (uint8_t)NT_UI_RICH_LAYER_AUTO) {
        return style->layer;
    }
    if (kind == NT_RICH_ATOM_TEXT) {
        return 0U;
    }
    return (kind == NT_RICH_ATOM_IMAGE) ? 1U : 2U;
}

/* Append one TEXT chunk atom [off,off+len) measured under font/size. */
static void rich_push_text_atom(rich_atom_t *out, uint32_t *n, uint32_t cap, const nt_ui_rich_run_t *run, nt_font_t font, float size, uint32_t color, uint8_t effect_id, uint8_t layer, float t_asc,
                                float t_desc, uint32_t off, uint32_t len, float advance, bool breakable_before, uint16_t run_idx) {
    NT_ASSERT(*n < cap && "rich atom overflow (NT_UI_RICH_MAX_GLYPHS)");
    /* HARD cap (survives NT_ASSERT OFF): atom stream is content-proportional but capped at
     * NT_UI_RICH_MAX_GLYPHS; drop atoms past the cap rather than write past out[]. */
    if (*n >= cap) {
        return;
    }
    rich_atom_t *a = &out[(*n)++];
    memset(a, 0, sizeof *a);
    a->kind = NT_RICH_ATOM_TEXT;
    a->advance = advance;
    a->w = advance;
    a->h = t_asc + t_desc;
    a->asc = t_asc;
    a->desc = t_desc;
    a->breakable_before = breakable_before;
    a->font = font;
    a->size = size;
    a->color = color;
    a->effect_id = effect_id;
    a->layer = layer;
    a->text_off = off;
    a->text_len = len;
    a->flags = run->flags;
    a->run_idx = run_idx;
    a->link_id = run->link_id;
}

/* Split an over-long word (advance > container_w) at codepoint boundaries into chunks each
 * <= container_w (break-anywhere). Each chunk width is the WHOLE-prefix measure so inter-glyph
 * kerning counts -- a per-codepoint sum drops kerning and overflows under a kerning font. */
static void rich_break_anywhere(rich_atom_t *out, uint32_t *n, uint32_t cap, const nt_ui_rich_run_t *run, nt_font_t font, float size, uint32_t color, uint8_t effect_id, uint8_t layer, float t_asc,
                                float t_desc, const char *text, uint32_t word_off, uint32_t word_len, float container_w, uint16_t run_idx, bool first_breakable) {
    uint32_t chunk_start = word_off;
    uint32_t i = word_off;
    const uint32_t word_end = word_off + word_len;
    uint32_t cp = 0;
    uint32_t last_boundary = word_off; /* last codepoint boundary that still fit */
    float cur_w = 0.0F;                /* measured width of [chunk_start, i) -- prefix incl. kerning */
    float prev_w = 0.0F;               /* measured width of [chunk_start, last_boundary) */
    bool emitted = false;
    while (i < word_end) {
        uint32_t state = NT_UTF8_ACCEPT;
        do {
            nt_utf8_decode(&state, &cp, (uint8_t)text[i]);
            i++;
        } while (i < word_end && state != NT_UTF8_ACCEPT && state != NT_UTF8_REJECT);
        /* Prefix from chunk_start -> the running width carries inter-glyph kerning (NOT an isolated
         * per-glyph sum, which would drop it and let the chunk overflow under a kerning font). */
        cur_w = nt_font_measure_n(font, text + chunk_start, i - chunk_start, size, 0.0F).width;
        if (cur_w > container_w && last_boundary > chunk_start) {
            /* Commit the chunk up to the previous boundary; restart the new chunk at this glyph. */
            const uint32_t clen = last_boundary - chunk_start;
            rich_push_text_atom(out, n, cap, run, font, size, color, effect_id, layer, t_asc, t_desc, chunk_start, clen, prev_w, emitted ? true : first_breakable, run_idx);
            emitted = true;
            chunk_start = last_boundary;
            /* Re-seed the prefix from the new chunk_start: re-measure [chunk_start, i) so the carried
             * tail's width matches a standalone chunk (its own kerning chain, no left neighbour). */
            cur_w = nt_font_measure_n(font, text + chunk_start, i - chunk_start, size, 0.0F).width;
        }
        last_boundary = i;
        prev_w = cur_w; /* width of [chunk_start, i): the next commit point */
    }
    /* Trailing chunk (or the whole word if it never exceeded). */
    if (chunk_start < word_end) {
        const uint32_t clen = word_end - chunk_start;
        rich_push_text_atom(out, n, cap, run, font, size, color, effect_id, layer, t_asc, t_desc, chunk_start, clen, cur_w, emitted ? true : first_breakable, run_idx);
    }
}

/* Build the atom stream from the run-list. TEXT runs split into word atoms; an over-long
 * word break-anywhere-splits against container_w; IMAGE atoms are one unbreakable box. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- word-split + overflow policy
static uint32_t rich_build_atoms(nt_ui_rich_state_t *st, float font_size, float container_w, rich_atom_t *out, uint32_t cap) {
    uint32_t n = 0;
    for (uint32_t ri = 0; ri < st->run_count; ri++) {
        const nt_ui_rich_run_t *run = &st->runs[ri];
        const nt_ui_rich_style_t *style = &st->styles[run->style_idx];
        const float size = font_size * style->scale;

        if (run->kind == NT_RICH_ATOM_TEXT) {
            bool synth = false;
            const nt_font_t font = rich_resolve_font(style, &synth);
            const uint8_t layer = rich_effective_layer(style, NT_RICH_ATOM_TEXT);
            float t_asc = 0.0F;
            float t_desc = 0.0F;
            rich_text_metrics(font, size, &t_asc, &t_desc);

            uint32_t i = run->text_off;
            const uint32_t end = run->text_off + run->text_len;
            while (i < end) {
                const uint32_t word_start = i;
                while (i < end && st->text[i] != ' ') {
                    i++;
                }
                while (i < end && st->text[i] == ' ') {
                    i++; /* trailing spaces belong to this word atom */
                }
                const uint32_t word_len = i - word_start;
                const float advance = nt_font_measure_n(font, st->text + word_start, word_len, size, 0.0F).width;
                const bool breakable = (n > 0U);
                /* OVERFLOW POLICY: a single word wider than the box breaks anywhere. */
                if (advance > container_w && container_w > 0.0F) {
                    rich_break_anywhere(out, &n, cap, run, font, size, style->color_abgr, style->effect_id, layer, t_asc, t_desc, st->text, word_start, word_len, container_w, (uint16_t)ri, breakable);
                } else {
                    rich_push_text_atom(out, &n, cap, run, font, size, style->color_abgr, style->effect_id, layer, t_asc, t_desc, word_start, word_len, advance, breakable, (uint16_t)ri);
                }
            }
        } else if (run->kind == NT_RICH_ATOM_IMAGE) {
            /* By-name resolve: the atlas IS the registry. Resolve-and-memoize writes
             * the region index back into the run's ref so emit reuses it (no per-image registry). */
            nt_atlas_resolve_ref(&st->runs[ri].image_ref);
            const uint32_t reg_idx = run->image_ref.region;
            const nt_texture_region_t *reg = (reg_idx != NT_ATLAS_INVALID_REGION) ? nt_atlas_get_region(run->image_ref.atlas, reg_idx) : NULL;
            const float src_w = (reg != NULL) ? (float)reg->source_w : 1.0F;
            const float src_h = (reg != NULL) ? (float)reg->source_h : 1.0F;
            NT_ASSERT(n < cap && "rich atom overflow (NT_UI_RICH_MAX_GLYPHS)");
            if (n >= cap) {
                break; /* HARD cap (survives NT_ASSERT OFF): no write past out[] */
            }
            rich_atom_t *a = &out[n++];
            memset(a, 0, sizeof *a);
            a->kind = NT_RICH_ATOM_IMAGE;
            a->advance = src_w * run->image_scale;
            a->w = a->advance;
            a->h = src_h * run->image_scale;
            a->valign = run->image_valign;
            a->offset_y = run->image_offset_y;
            a->color = style->color_abgr; /* the run's tint -> the inline image's lossless a_tint */
            a->effect_id = style->effect_id;
            a->layer = rich_effective_layer(style, NT_RICH_ATOM_IMAGE);
            a->breakable_before = true; /* image: break opportunity on both sides */
            a->run_idx = (uint16_t)ri;
            a->link_id = run->link_id;
        } else { /* NT_RICH_ATOM_OBJECT: reserve the box via the run's measure_fn. */
            NT_ASSERT(run->object_measure != NULL && "rich object: measure_fn must be non-NULL");
            const nt_ui_rich_object_measure_t m = run->object_measure(run->object_user);
            /* measure_fn is game-trusted: fail early on a garbage box. */
            NT_ASSERT(isfinite(m.width) && isfinite(m.height) && isfinite(m.ascent) && m.width >= 0.0F && m.height >= 0.0F && "rich object: measure_fn must return finite, non-negative width/height");
            /* HARD clamp (survives NT_ASSERT OFF): a NaN/negative box must not reach rich_break_lines
             * (wrap), ascent/descent accumulation, or the Clay FIXED block size. */
            const float obj_w = (isfinite(m.width) && m.width >= 0.0F) ? m.width : 0.0F;
            const float obj_h = (isfinite(m.height) && m.height >= 0.0F) ? m.height : 0.0F;
            const float obj_asc = isfinite(m.ascent) ? m.ascent : 0.0F;
            NT_ASSERT(n < cap && "rich atom overflow (NT_UI_RICH_MAX_GLYPHS)");
            if (n >= cap) {
                break; /* HARD cap (survives NT_ASSERT OFF): no write past out[] */
            }
            rich_atom_t *a = &out[n++];
            memset(a, 0, sizeof *a);
            a->kind = NT_RICH_ATOM_OBJECT;
            a->advance = obj_w;
            a->w = obj_w;
            a->h = obj_h;
            a->asc = obj_asc; /* above-baseline for valign=baseline placement */
            a->valign = NT_RICH_VALIGN_BASELINE;
            a->color = style->color_abgr;
            a->effect_id = style->effect_id;
            a->layer = rich_effective_layer(style, NT_RICH_ATOM_OBJECT);
            a->breakable_before = true; /* object: break opportunity on both sides */
            a->run_idx = (uint16_t)ri;
            a->link_id = run->link_id;
        }
    }
    return n;
}

/* PASS 1: greedy line break. Writes each atom's line index; returns line count. */
static uint32_t rich_break_lines(rich_atom_t *atoms, uint32_t na, float container_w) {
    float line_w = 0.0F;
    uint32_t line = 0;
    for (uint32_t i = 0; i < na; i++) {
        const float aw = atoms[i].advance;
        if (line_w > 0.0F && atoms[i].breakable_before && (line_w + aw) > container_w) {
            line++; /* break BEFORE this atom (greedy) */
            line_w = 0.0F;
        }
        atoms[i].line = line;
        line_w += aw;
    }
    return (na > 0U) ? (line + 1U) : 0U;
}

/* An IMAGE atom's ascent/descent demand for the line, given the MIDDLE x-height reference
 * (mid_ref: line text ascent, or tallest image box when the line has no text). Placement reuses
 * the SAME mid_ref so the reserved box and the seated image agree. */
static void rich_image_metrics(const rich_atom_t *a, float mid_ref, float *io_asc, float *io_desc) {
    const float h = a->h;
    if (a->valign == NT_RICH_VALIGN_MIDDLE) {
        const float xh = rich_x_height(mid_ref);
        const float asc = (h * 0.5F) + (xh * 0.5F);
        const float desc = (h * 0.5F) - (xh * 0.5F);
        *io_asc = (asc > *io_asc) ? asc : *io_asc;
        *io_desc = (desc > *io_desc) ? desc : *io_desc;
    } else if (a->kind == NT_RICH_ATOM_OBJECT) {
        /* OBJECT carries a measured ascent: seat by it so a box with ascent != height (a glyph-like
         * object) leaves (h - ascent) below the baseline as descent instead of overlapping the next line. */
        const float asc = a->asc;
        const float desc = h - a->asc;
        *io_asc = (asc > *io_asc) ? asc : *io_asc;
        *io_desc = (desc > *io_desc) ? desc : *io_desc;
    } else { /* IMAGE BASELINE/TOP/BOTTOM: whole box above the baseline (max line height) */
        *io_asc = (h > *io_asc) ? h : *io_asc;
    }
}

/* The MIDDLE-valign x-height reference for a line: its text ascent, or the tallest image box when
 * the line has no text (so an image-dominant line still centers). Reserve AND placement share it
 * Also accumulates the line's text ascent/descent into io_asc/io_desc. */
static float rich_line_text_metrics(const rich_atom_t *atoms, uint32_t na, uint32_t L, float *io_asc, float *io_desc) {
    float text_asc = 0.0F;
    float max_box_h = 0.0F;
    for (uint32_t i = 0; i < na; i++) {
        if (atoms[i].line != L) {
            continue;
        }
        if (atoms[i].kind == NT_RICH_ATOM_TEXT) {
            text_asc = (atoms[i].asc > text_asc) ? atoms[i].asc : text_asc;
            *io_asc = (atoms[i].asc > *io_asc) ? atoms[i].asc : *io_asc;
            *io_desc = (atoms[i].desc > *io_desc) ? atoms[i].desc : *io_desc;
        } else if (atoms[i].kind == NT_RICH_ATOM_IMAGE) {
            max_box_h = (atoms[i].h > max_box_h) ? atoms[i].h : max_box_h;
        }
    }
    return (text_asc > 0.0F) ? text_asc : max_box_h;
}

/* Per-line max ascent/descent over the line's atoms + the line's pixel width. out_mid_ref is the
 * MIDDLE-valign x-height reference (see rich_line_text_metrics); PLACEMENT must reuse it so a
 * centered image seats inside its reserved box. */
static void rich_line_metrics(const rich_atom_t *atoms, uint32_t na, uint32_t L, float *out_asc, float *out_desc, float *out_w, float *out_mid_ref) {
    *out_asc = 0.0F;
    *out_desc = 0.0F;
    *out_w = 0.0F;
    const float mid_ref = rich_line_text_metrics(atoms, na, L, out_asc, out_desc);
    for (uint32_t i = 0; i < na; i++) {
        /* IMAGE + OBJECT both reserve a box that demands line ascent/descent. */
        if (atoms[i].line == L && (atoms[i].kind == NT_RICH_ATOM_IMAGE || atoms[i].kind == NT_RICH_ATOM_OBJECT)) {
            rich_image_metrics(&atoms[i], mid_ref, out_asc, out_desc);
        }
    }
    for (uint32_t i = 0; i < na; i++) {
        if (atoms[i].line == L) {
            *out_w += atoms[i].advance;
        }
    }
    *out_mid_ref = mid_ref;
}

static float rich_atom_y(const rich_atom_t *a, float baseline_y, float pen_y, float line_h, float mid_ref) {
    if (a->kind == NT_RICH_ATOM_TEXT) {
        return baseline_y - a->asc; /* top of the glyph box */
    }
    switch (a->valign) {
    case NT_RICH_VALIGN_MIDDLE: {
        const float xh = rich_x_height(mid_ref); /* SAME ref rich_image_metrics reserved with */
        return baseline_y - ((a->h * 0.5F) + (xh * 0.5F)) + a->offset_y;
    }
    case NT_RICH_VALIGN_TOP:
        return pen_y + a->offset_y;
    case NT_RICH_VALIGN_BOTTOM:
        return pen_y + line_h - a->h + a->offset_y;
    case NT_RICH_VALIGN_BASELINE:
    default:
        /* OBJECT seats by its measured ascent (box bottom lands at baseline + (h - asc)); IMAGE
         * baseline keeps the whole box above the baseline (asc == 0). */
        return baseline_y - ((a->kind == NT_RICH_ATOM_OBJECT) ? a->asc : a->h) + a->offset_y;
    }
}

/* Horizontal align offset for a line of the given width. */
static float rich_align_ox(nt_rich_align_t align, float container_w, float line_w) {
    if (align == NT_RICH_ALIGN_CENTER) {
        return (container_w - line_w) * 0.5F;
    }
    if (align == NT_RICH_ALIGN_RIGHT) {
        return container_w - line_w;
    }
    return 0.0F;
}

/* Resolve the layout width: explicit container_w is primary; container_w<=0 falls back to
 * the prev-frame nt_ui_get_bbox (the menu's two-pass trick -- no new getter). The
 * first frame (not found) uses the full screen width, then settles. A degenerate <=0 width
 * clamps to a minimum so the wrap loop never spins. */
static float rich_resolve_width(nt_ui_context_t *ctx, uint32_t id, float container_w) {
    float w = container_w;
    if (w <= 0.0F) {
        const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
        w = bb.found ? bb.width : ctx->begin_w; /* first frame: full screen width, then settles */
    }
    if (!(w > 1.0F)) {
        w = 1.0F; /* never 0/negative -> the greedy break would loop */
    }
    return w;
}

/* The full solve: width resolve -> atom build -> greedy wrap -> per-line baseline + align.
 * Writes st->solved[], st->line_count, st->links[], st->total_w/h. NO heap (scratch atoms). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- two-pass solve, regioned
static void rich_solve(nt_ui_context_t *ctx, nt_ui_rich_state_t *st, uint32_t id, float container_w, float font_size, nt_rich_align_t align) {
    const float box_w = rich_resolve_width(ctx, id, container_w);

    /* Content-proportional upper bound: break-anywhere splits to at most one atom per text
     * byte, plus one box atom per image/object run. Cap at NT_UI_RICH_MAX_GLYPHS. */
    uint32_t atom_cap = st->text_len + st->run_count + 1U;
    if (atom_cap > NT_UI_RICH_MAX_GLYPHS) {
        atom_cap = NT_UI_RICH_MAX_GLYPHS;
    }
    rich_atom_t *atoms = NT_MEM_SCRATCH_ALLOC_ARRAY(rich_atom_t, atom_cap);
    st->solved = NT_MEM_SCRATCH_ALLOC_ARRAY(nt_ui_rich_solved_atom_t, atom_cap);
    st->links = NT_MEM_SCRATCH_ALLOC_ARRAY(nt_ui_rich_link_rect_t, NT_UI_RICH_MAX_LINKS);
    const uint32_t na = rich_build_atoms(st, font_size, box_w, atoms, atom_cap);
    st->line_count = rich_break_lines(atoms, na, box_w);

    st->solved_count = 0;
    st->link_count = 0;

    /* Grow case (container_w<=0): the FIXED block ends up max_line_w wide, so L/C/R must align
     * against max_line_w, not box_w (the resolved/screen fallback) -- else center/right atoms land
     * outside the block (worst on the first frame). Pre-scan line widths to learn max_line_w, then
     * align against it. Explicit container_w aligns against the box as before. */
    float max_line_w = 0.0F;
    for (uint32_t L = 0; L < st->line_count; L++) {
        float line_w = 0.0F;
        for (uint32_t i = 0; i < na; i++) {
            if (atoms[i].line == L) {
                line_w += atoms[i].advance;
            }
        }
        max_line_w = (line_w > max_line_w) ? line_w : max_line_w;
    }
    const float align_w = (container_w > 0.0F) ? box_w : max_line_w;

    float pen_y = 0.0F;
    uint32_t glyph_cursor = 0; /* running per-glyph effect index across the whole block */
    for (uint32_t L = 0; L < st->line_count; L++) {
        float asc = 0.0F;
        float desc = 0.0F;
        float line_w = 0.0F;
        float mid_ref = 0.0F;
        rich_line_metrics(atoms, na, L, &asc, &desc, &line_w, &mid_ref);
        const float line_h = asc + desc;
        const float baseline_y = pen_y + asc;
        const float ox = rich_align_ox(align, align_w, line_w);

        float pen_x = ox;
        uint32_t prev_link_id = 0U; /* link id of the immediately-preceding atom on this line (0 = none/gap) */
        for (uint32_t i = 0; i < na; i++) {
            if (atoms[i].line != L) {
                continue;
            }
            const rich_atom_t *a = &atoms[i];
            NT_ASSERT(st->solved_count < na && "rich solved-atom overflow");
            /* HARD cap (survives NT_ASSERT OFF): solved[] is sized to atom_cap >= na; each atom is
             * visited once so this is structurally bounded, but guard the write regardless. */
            if (st->solved_count >= na) {
                break;
            }
            nt_ui_rich_solved_atom_t *s = &st->solved[st->solved_count++];
            s->kind = a->kind;
            s->x = pen_x;
            s->y = rich_atom_y(a, baseline_y, pen_y, line_h, mid_ref);
            s->w = a->w;
            s->h = a->h;
            s->asc = a->asc;
            s->line = L;
            s->font = a->font;
            s->size = a->size;
            s->color = a->color;
            s->effect_id = a->effect_id;
            s->layer = a->layer;
            /* Running glyph index: the effect curve phase advances monotonically across the block
             * (per-glyph for TEXT, one step per image/object) so adjacent words don't repeat phase. */
            s->fx_idx = glyph_cursor;
            glyph_cursor += (a->kind == NT_RICH_ATOM_TEXT) ? rich_glyph_count(st->text, a->text_off, a->text_len) : 1U;
            s->text_off = a->text_off;
            s->text_len = a->text_len;
            s->flags = a->flags;
            s->run_idx = a->run_idx;
            s->link_id = a->link_id;
            if (a->link_id != 0U) {
                /* Union into the last rect ONLY when the IMMEDIATELY-PRECEDING atom on this line carried
                 * the same link (a contiguous run). A non-link atom in between resets prev_link_id to 0,
                 * so [A][gap][A] opens a second rect instead of widening A's rect across the gap. */
                nt_ui_rich_link_rect_t *last = (st->link_count > 0U) ? &st->links[st->link_count - 1] : NULL;
                if (last != NULL && prev_link_id == a->link_id && last->link_id == a->link_id && last->line == L) {
                    last->w = (pen_x + a->w) - last->x; /* extend to cover this atom (adjacent same-link) */
                } else if (st->link_count >= NT_UI_RICH_MAX_LINKS) {
                    /* HARD cap (survives NT_ASSERT OFF): drop hitboxes past the cap rather than write
                     * past links[] -- untrusted markup can declare many disjoint <link> rects. */
                    NT_ASSERT(false && "rich link-rect overflow");
                } else {
                    nt_ui_rich_link_rect_t *lr = &st->links[st->link_count++];
                    lr->link_id = a->link_id;
                    lr->line = L;
                    lr->x = pen_x;
                    lr->y = pen_y;
                    lr->w = a->w;
                    lr->h = line_h;
                }
            }
            prev_link_id = a->link_id; /* a non-link atom (0) breaks the contiguous run -> next same-link opens a new rect */
            pen_x += a->advance;
        }
        pen_y += line_h;
    }
    /* The FIXED block hosts the container width (explicit) or the max line width (grow) -- the
     * same width L/C/R aligned against, so alignment stays inside the block. */
    st->total_w = align_w;
    st->total_h = pen_y;

    /* Probe: record the first IMAGE atom's by-name-resolved region + solved y. */
    st->image_region = NT_ATLAS_INVALID_REGION;
    st->image_y = 0.0F;
    for (uint32_t i = 0; i < st->solved_count; i++) {
        if (st->solved[i].kind == NT_RICH_ATOM_IMAGE) {
            st->image_region = st->runs[st->solved[i].run_idx].image_ref.region;
            st->image_y = st->solved[i].y;
            break;
        }
    }
    st->solved_ready = true;
}
// #endregion

// #region emit
/* Unpack a packed AABBGGRR color into a normalized RGBA float4 (text renderer order), then fold
 * opacity into alpha. nt_color_unpack owns the packed->[0,1] math (R,G,B,A order matches); the
 * opacity multiply is rich-specific so this stays a thin wrapper. */
static void rich_unpack_color(uint32_t abgr, float opacity, float out[4]) {
    nt_color_unpack(abgr, out);
    out[3] *= opacity;
}

/* Evaluate the per-atom effect -> visual-only transform; effect_id 0 / unregistered -> identity. */
static nt_ui_rich_fx_result_t rich_eval_fx(const nt_ui_rich_state_t *st, const nt_ui_rich_solved_atom_t *s, const float base_color[4]) {
    /* Custom (game-supplied) fns resolve BEFORE stock: a custom effect_id indexes the
     * per-block table captured at build; otherwise fall back to the stock catalog. */
    nt_ui_rich_fx_fn fn = NULL;
    void *user_data = NULL; /* custom fn gets its registered pointer; stock fns get NULL */
    if (nt_ui_rich_fx_id_is_custom(s->effect_id)) {
        const uint32_t idx = (uint32_t)s->effect_id - NT_UI_RICH_FX_CUSTOM_BASE;
        if (idx < st->custom_fx_count) {
            fn = st->custom_fx[idx];
            user_data = st->custom_fx_user[idx];
        }
    } else {
        fn = nt_ui_rich_fx_stock(s->effect_id);
    }
    if (fn == NULL) {
        return nt_ui_rich_fx_identity(base_color);
    }
    const float base_xy[2] = {s->x, s->y};
    const float base_wh[2] = {s->w, s->h};
    const bool hovered = (s->link_id != 0U) && (s->link_id == st->hovered_link);
    return fn(s->fx_idx, s->kind, base_xy, base_wh, base_color, st->time, hovered, user_data);
}

/* Per-span model mat4 for draw_n: LAYOUT pen (ox,oy) -> world, with the text Y-up <-> Clay Y-down
 * flip on col1 (mirrors emit_text in nt_ui.c). `shear` folds synthetic-italic lean (k*col0) into col1. */
static void rich_span_model(const float world[16], float ox, float oy, bool shear, float out[16]) {
    const float sign_y = -1.0F;
    const float k = shear ? 0.2F : 0.0F; /* synthetic-italic shear factor */
    for (int rr = 0; rr < 4; ++rr) {
        const float col0 = world[rr];     /* text-local +x */
        const float col1 = world[4 + rr]; /* text-local +y (pre Y-flip) */
        out[rr] = col0;
        out[4 + rr] = (sign_y * col1) + (k * col0); /* Y-flip + shear lean */
        out[8 + rr] = world[8 + rr];
        out[12 + rr] = (ox * world[rr]) + (oy * world[4 + rr]) + world[12 + rr];
    }
}

/* Emit one TEXT atom WITHOUT an effect: one span per atom (batch-friendly). */
static void rich_emit_text_plain(nt_ui_rich_state_t *st, const nt_ui_custom_frame_t *frame, const nt_ui_rich_solved_atom_t *s, float box_x, float box_y, bool shear) {
    const float baseline_y = box_y + s->y + s->asc; /* solved y is glyph-box top */
    float model[16];
    rich_span_model(frame->world_mat4, box_x + s->x, baseline_y, shear, model);
    float color[4];
    rich_unpack_color(s->color, frame->opacity, color);
    nt_text_renderer_draw_n(st->text + s->text_off, s->text_len, model, s->size, color, 0.0F, 0.0F);
    st->emit_span_count++;
}

/* Emit one effected TEXT atom: per-glyph draw_n (curve phase-shifts per glyph), VISUAL-ONLY.
 * Per-glyph pen uses cumulative-prefix measures so sum(advances)==measure(whole) (keeps kerning;
 * stays in the reserved box). O(N^2) but N is one word/line-chunk, so tiny. */
static void rich_emit_text_effected(nt_ui_rich_state_t *st, const nt_ui_custom_frame_t *frame, const nt_ui_rich_solved_atom_t *s, float box_x, float box_y, bool shear) {
    float base_color[4];
    rich_unpack_color(s->color, frame->opacity, base_color);
    const uint32_t a0 = s->text_off; /* atom byte start: prefix measures are relative to it (kerning chain) */
    const uint32_t gend = s->text_off + s->text_len;
    uint32_t gi = s->text_off;
    uint32_t glyph_ord = 0;
    float prefix_w = 0.0F; /* measured width of the atom prefix BEFORE the current glyph */
    while (gi < gend) {
        uint32_t state = NT_UTF8_ACCEPT;
        uint32_t cp = 0;
        const uint32_t g0 = gi;
        do {
            nt_utf8_decode(&state, &cp, (uint8_t)st->text[gi]);
            gi++;
        } while (gi < gend && state != NT_UTF8_ACCEPT && state != NT_UTF8_REJECT);
        /* Prefix INCLUDING this glyph minus prefix before it -> the glyph's advance WITH kerning. */
        const float prefix_incl = nt_font_measure_n(s->font, st->text + a0, gi - a0, s->size, 0.0F).width;
        const float gw = prefix_incl - prefix_w;
        const float gx = s->x + prefix_w; /* solver-consistent pen (s->x is the atom's solved left) */

        /* Per-glyph fx: a unique fx_idx (atom base + glyph ordinal) advances the curve phase. */
        nt_ui_rich_solved_atom_t gs = *s;
        gs.fx_idx = s->fx_idx + glyph_ord;
        gs.x = gx;
        gs.w = gw;
        const nt_ui_rich_fx_result_t fx = rich_eval_fx(st, &gs, base_color);
        if (fx.visible) {
            /* Scale about the glyph center: shift the pen so the scaled glyph stays centered. */
            const float cx = gx + (gw * 0.5F);
            const float scaled_x = cx - ((gw * 0.5F) * fx.scale);
            const float baseline_y = box_y + s->y + s->asc + fx.offset_y;
            float model[16];
            rich_span_model(frame->world_mat4, box_x + scaled_x + fx.offset_x, baseline_y, shear, model);
            nt_text_renderer_draw_n(st->text + g0, gi - g0, model, s->size * fx.scale, fx.color, 0.0F, 0.0F);
            st->emit_span_count++;
        }
        prefix_w = prefix_incl;
        glyph_ord++;
    }
}

/* OBJECT atoms: the game's own draw_fn at the solved box, in the FIXED block's
 * absolute LAYOUT coords. The engine never draws the object itself (renderer-agnostic).
 * The per-atom effect shifts/scales the box about its center; not-visible skips the call. */
static void rich_emit_objects(nt_ui_rich_state_t *st, const nt_ui_custom_frame_t *frame, float box_x, float box_y, uint8_t layer) {
    for (uint32_t i = 0; i < st->solved_count; i++) {
        const nt_ui_rich_solved_atom_t *s = &st->solved[i];
        if (s->kind != NT_RICH_ATOM_OBJECT || s->layer != layer) {
            continue; /* only this z-band's objects (the caller walks bands ascending) */
        }
        const nt_ui_rich_run_t *run = &st->runs[s->run_idx];
        if (run->object_draw == NULL) {
            continue; /* measure-only object (box reserved, nothing drawn) */
        }
        float base_color[4];
        rich_unpack_color(s->color, frame->opacity, base_color);
        const nt_ui_rich_fx_result_t fx = rich_eval_fx(st, s, base_color);
        if (!fx.visible) {
            continue; /* fade_in / typewriter: skip the draw_fn call entirely */
        }
        const float scaled_w = s->w * fx.scale;
        const float scaled_h = s->h * fx.scale;
        const float ox = box_x + s->x + fx.offset_x + ((s->w - scaled_w) * 0.5F);
        const float oy = box_y + s->y + fx.offset_y + ((s->h - scaled_h) * 0.5F);
        /* fx.color is the absolute resolved RGBA: <color> with frame->opacity folded into alpha
         * + the effect tint -- the same color TEXT/IMAGE render with. */
        run->object_draw(run->object_user, ox, oy, scaled_w, scaled_h, fx.color, frame->world_mat4);
    }
}

/* Pack a normalized [0,1] RGBA float4 (text-renderer order) into 0xAABBGGRR for the u8 sprite tint. */
static uint32_t rich_pack_tint(const float c[4]) {
    const uint32_t r = (uint32_t)lrintf(nt_ui_clampf(c[0], 0.0F, 1.0F) * 255.0F);
    const uint32_t g = (uint32_t)lrintf(nt_ui_clampf(c[1], 0.0F, 1.0F) * 255.0F);
    const uint32_t b = (uint32_t)lrintf(nt_ui_clampf(c[2], 0.0F, 1.0F) * 255.0F);
    const uint32_t a = (uint32_t)lrintf(nt_ui_clampf(c[3], 0.0F, 1.0F) * 255.0F);
    return r | (g << 8) | (b << 16) | (a << 24);
}

/* Emit immediately via the sprite renderer so all inline images coalesce into one batch (set_material once).
 * The scroll scissor is GL-live during self-emit, so images clip to the panel automatically; fx.scale>1
 * over-draws like OBJECT atoms (acceptable). Opacity is folded into the tint here -- no walker fold in self-emit. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- early-out guards + per-atom resolve/fx/model build in one linear pass
static void rich_emit_images(nt_ui_rich_state_t *st, const nt_ui_custom_frame_t *frame, float box_x, float box_y, uint8_t layer) {
    /* Material validity is a band-invariant -> the caller checks st->image_material once before the layer
     * loop (id != 0, attr_map_count == 0); reaching here means it passed. */
    bool bound = false; /* per-call: each layer is its own drained batch -> rebind once per layer */
    for (uint32_t i = 0; i < st->solved_count; i++) {
        const nt_ui_rich_solved_atom_t *s = &st->solved[i];
        if (s->kind != NT_RICH_ATOM_IMAGE || s->layer != layer) {
            continue; /* only this z-band's images (coalesced within the band) */
        }
        const nt_ui_rich_run_t *run = &st->runs[s->run_idx];
        if (run->image_ref.region == NT_ATLAS_INVALID_REGION || run->image_ref.atlas.id == 0U) {
            continue; /* unresolved name -> no-art early-out (mirror nt_ui_fill/radial), not OOB UVs */
        }
        const nt_texture_region_t *reg = nt_atlas_get_region(run->image_ref.atlas, run->image_ref.region);
        if (reg == NULL || reg->vertex_count == 0U) {
            continue; /* tombstoned region -> skip */
        }
        float base_color[4];
        rich_unpack_color(s->color, frame->opacity, base_color); /* fold parent opacity (no walker fold here) */
        const nt_ui_rich_fx_result_t fx = rich_eval_fx(st, s, base_color);
        if (!fx.visible) {
            continue; /* fade_in / typewriter: skip the image entirely until its window opens */
        }
        /* VISUAL-ONLY effect: scale the box about its center, shift by the effect offset (solved box stays
         * the solver's). Same math as rich_emit_objects. */
        const float scaled_w = s->w * fx.scale;
        const float scaled_h = s->h * fx.scale;
        const float bx = box_x + s->x + fx.offset_x + ((s->w - scaled_w) * 0.5F);
        const float by = box_y + s->y + fx.offset_y + ((s->h - scaled_h) * 0.5F);

        /* Build the sprite model exactly like emit_image: source (origin,origin) anchors at the box center,
         * model = world x T(cx,cy) x S(sx,-sy,1) (source Y-up inverts before world maps to layout). */
        const float ipu = nt_atlas_get_inverse_pixels_per_unit(run->image_ref.atlas);
        const float src_w = (float)reg->source_w * ipu;
        const float src_h = (float)reg->source_h * ipu;
        if (!(src_w > 0.0F) || !(src_h > 0.0F)) {
            continue; /* broken atlas data: skip rather than divide by zero */
        }
        const float sx_f = scaled_w / src_w;
        const float sy_f = scaled_h / src_h;
        const float cx = bx + (scaled_w * 0.5F);
        const float cy = by + (scaled_h * 0.5F);
        const float *world = frame->world_mat4;
        float m[16];
        for (int rr = 0; rr < 4; ++rr) {
            m[rr] = sx_f * world[rr];
            m[4 + rr] = -sy_f * world[4 + rr];
            m[8 + rr] = world[8 + rr];
            m[12 + rr] = (cx * world[rr]) + (cy * world[4 + rr]) + world[12 + rr];
        }
        if (!bound) {
            nt_sprite_renderer_set_material(st->image_material); /* bind ONCE: all images coalesce into one batch */
            bound = true;
        }
        nt_sprite_renderer_emit_region(run->image_ref.atlas, run->image_ref.region, m, reg->origin_x, reg->origin_y, rich_pack_tint(fx.color), 0U);
        st->image_emit_count++;
    }
}

/* Group same-band TEXT by font.id so set_font fires once per distinct font, not per face transition
 * (the text renderer flushes on every set_font). No distinct-font cap -> a >4-family block never drops a face. */
static void rich_emit_text_layer(nt_ui_rich_state_t *st, const nt_ui_custom_frame_t *frame, float box_x, float box_y, uint8_t layer) {
    for (uint32_t i = 0; i < st->solved_count; i++) {
        const nt_ui_rich_solved_atom_t *s = &st->solved[i];
        if (s->kind != NT_RICH_ATOM_TEXT || s->text_len == 0U || s->layer != layer) {
            continue;
        }
        bool first = true; /* is this the FIRST same-band TEXT atom carrying s->font.id? */
        for (uint32_t j = 0; j < i; j++) {
            const nt_ui_rich_solved_atom_t *p = &st->solved[j];
            if (p->kind == NT_RICH_ATOM_TEXT && p->text_len != 0U && p->layer == layer && p->font.id == s->font.id) {
                first = false;
                break;
            }
        }
        if (!first) {
            continue; /* this font's pass already emitted every same-band atom that shares it */
        }
        nt_text_renderer_set_font(s->font); /* once per distinct font on the band: collapses per-transition flushes */
        for (uint32_t k = i; k < st->solved_count; k++) {
            const nt_ui_rich_solved_atom_t *e = &st->solved[k];
            if (e->kind != NT_RICH_ATOM_TEXT || e->text_len == 0U || e->layer != layer || e->font.id != s->font.id) {
                continue; /* other kinds/layers/fonts -> their own pass */
            }
            const bool shear = (e->flags & NT_UI_RICH_RUN_SYNTH_ITALIC) != 0U;
            if (e->effect_id == 0U) {
                rich_emit_text_plain(st, frame, e, box_x, box_y, shear);
            } else {
                rich_emit_text_effected(st, frame, e, box_x, box_y, shear);
            }
        }
    }
}

/* Gather the DISTINCT layers present across the solved atoms, insertion-sorted ascending, into out[].
 * Capped at NT_UI_RICH_MAX_LAYERS with a HARD guard that survives NT_ASSERT OFF. The drop is by ENCOUNTER
 * order (the 17th distinct layer seen, of any value), so over-cap content silently vanishes -- the assert
 * fires in DEBUG; OFF keeps the hard skip rather than OOB. Returns the count. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one insertion-sort pass: dup-skip + cap-guard + shift
static uint32_t rich_gather_layers(const nt_ui_rich_state_t *st, uint8_t out[NT_UI_RICH_MAX_LAYERS]) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < st->solved_count; i++) {
        const uint8_t L = st->solved[i].layer;
        uint32_t pos = 0;
        bool dup = false;
        while (pos < count && out[pos] < L) {
            pos++;
        }
        if (pos < count && out[pos] == L) {
            dup = true;
        }
        if (dup) {
            continue;
        }
        if (count >= NT_UI_RICH_MAX_LAYERS) {
            NT_ASSERT(false && "rich distinct-layer count over NT_UI_RICH_MAX_LAYERS (raise the cap)");
            continue; /* HARD skip (survives NT_ASSERT OFF): drop the over-cap distinct layer, never write past out[] */
        }
        for (uint32_t k = count; k > pos; k--) {
            out[k] = out[k - 1]; /* shift up to keep ascending order */
        }
        out[pos] = L;
        count++;
    }
    /* Structural invariant: the >= cap guard above makes overflow unreachable; this can never fire. */
    NT_ASSERT(count <= NT_UI_RICH_MAX_LAYERS && "rich layer gather overflow");
    return count;
}

void nt_ui_rich_internal_emit_custom(const nt_ui_custom_frame_t *frame, void *data) {
    nt_ui_rich_state_t *st = (nt_ui_rich_state_t *)data;
    NT_ASSERT(st != NULL && "rich emit: NULL state");
    if (!st->solved_ready) {
        return;
    }
    const Clay_RenderCommand *c = (const Clay_RenderCommand *)frame->clay_cmd;
    const float box_x = c->boundingBox.x; /* FIXED block origin in LAYOUT (Y-down) */
    const float box_y = c->boundingBox.y;

    st->emit_span_count = 0;
    st->image_emit_count = 0; /* single reset point: rich_emit_images sums into this across all layer passes */

    /* Inline-image material is a BAND-INVARIANT: validate it ONCE here, not per-band (rich_emit_images runs
     * up to NT_UI_RICH_MAX_LAYERS times/frame). id==0 -> text-only block, no images. The plain u8 sprite
     * path requires attr_map_count==0 (emit_region bakes no custom-attr block); a NULL/custom-attr material
     * is a HARD guard (survives NT_ASSERT OFF), keeping the assert for the fail-early dev signal. */
    bool emit_images = false;
    if (st->image_material.id != 0U) {
        const nt_material_info_t *mi = nt_material_get_info(st->image_material);
        NT_ASSERT(mi != NULL && mi->attr_map_count == 0U && "rich inline-image material must be the plain u8 sprite path (attr_map_count==0)");
        emit_images = (mi != NULL && mi->attr_map_count == 0U);
    }

    /* Cross-renderer z is flush order (painter-order, depth off): emit ascending by layer and drain after
     * EVERY band so band N lands before N+1 and the block is a self-contained z island. */
    uint8_t layers[NT_UI_RICH_MAX_LAYERS];
    const uint32_t layer_count = rich_gather_layers(st, layers);
    for (uint32_t li = 0; li < layer_count; li++) {
        const uint8_t L = layers[li];
        /* Within ONE band, draw order is text BEHIND images BEHIND objects (matches the per-kind default
         * text<image<object): drain text first so it lands under the band's sprites/objects. */
        rich_emit_text_layer(st, frame, box_x, box_y, L);
        nt_text_renderer_flush(); /* text behind: land it before the band's sprites */
        if (emit_images) {
            rich_emit_images(st, frame, box_x, box_y, L);
        }
        rich_emit_objects(st, frame, box_x, box_y, L);
        nt_sprite_renderer_flush(); /* DRAIN sprites: land this band before the next so layer order == z order */
        nt_text_renderer_flush();   /* cheap safety drain in case an object draw_fn emitted text; no-op otherwise */
    }
}
// #endregion

// #region fixed-block
/* Declare the ONE measured Clay element hosting the solved run-list (never a per-run element); the
 * custom data routes the walk back to self-emit. Inline IMAGE atoms are NOT Clay children -- they emit
 * immediately in the self-emit (rich_emit_images), coalesced into one sprite batch. */
static void rich_declare_fixed_block(nt_ui_rich_state_t *st, uint32_t id, const nt_ui_element_data_t *data) {
    nt_ui_custom_data_t *cd = NT_MEM_SCRATCH_ALLOC(nt_ui_custom_data_t);
    NT_ASSERT(cd != NULL && "nt_ui_rich_text: scratch alloc failed (custom data)");
    *cd = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_RICH_TEXT, .data = st};

    Clay_ElementDeclaration decl = {0};
    decl.id = (Clay_ElementId){.id = id}; /* the block carries `id` so nt_ui_get_bbox resolves it (width fallback + link origin) */
    decl.layout.sizing.width = CLAY_SIZING_FIXED(st->total_w);
    decl.layout.sizing.height = CLAY_SIZING_FIXED(st->total_h);
    decl.custom = (Clay_CustomElementConfig){.customData = cd};
    decl.userData = (void *)data;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(decl);
    /* Inline images are NOT declared here -- they emit immediately in the self-emit (rich_emit_images),
     * coalesced into one sprite batch instead of one floating Clay child + clipTo scissor per image. */
    nt_ui_clay_priv_close_element();
}
// #endregion

// #region LINK
/* Per-rich-block retained press-target cell. Links have NO Clay element, so the capture-cell
 * mechanism (nt_ui_events) cannot track them; this keyed-by-block-id cell persists the link the
 * press STARTED on across frames so a click requires press+release on the SAME link. */
#define NT_UI_RICH_LINK_TAG NT_UI_STATE_TAG('r', 'l', 'n', 'k')
typedef struct {
    uint32_t pressed_link; /* link id the LEFT press began over (0 = pressed over no link / no press) */
} nt_ui_rich_link_press_t;

/* Resolve {hovered_link, clicked_link} from the solver's `<link>` rects (no per-link Clay element).
 * A click needs press AND release over the SAME link (latched in the per-block press cell).
 * Must run BEFORE emit -- emit reads st->hovered_link (hover gates effects). */
static void rich_resolve_links(nt_ui_context_t *ctx, nt_ui_rich_state_t *st, uint32_t id) {
    st->hovered_link = 0U;
    st->clicked_link = 0U;
    if (st->link_count == 0U) {
        return;
    }
    /* Link rects are offsets from the block origin; map the pointer into the SAME local space (prev-
     * frame bbox; first frame not-found -> origin 0,0) before testing. */
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
    const float ox = bb.found ? bb.x : 0.0F;
    const float oy = bb.found ? bb.y : 0.0F;

    nt_ui_internal_ensure_pointers_layout(ctx); /* device -> layout (idempotent) before reading */
    if (ctx->frame_pointer_count == 0U) {
        return;
    }
    /* Links hit-test the PRIMARY pointer only (frame_pointers[0]); multi-pointer link interaction
     * (a 2nd touch hovering/clicking a different link) is not supported. */
    const nt_pointer_t *p = &ctx->frame_pointers[0];
    const bool pressed = p->buttons[NT_BUTTON_LEFT].is_pressed;
    const bool released = p->buttons[NT_BUTTON_LEFT].is_released;

    /* Map via the block's baked transform (inverse-affine 2D / raycast 3D) so the hit-test matches the
     * draw; the flat 2D case maps 1:1. !mapped (clipped / 3D ray miss) -> no link under the pointer. */
    float lpx = 0.0F;
    float lpy = 0.0F;
    const bool mapped = nt_ui_internal_pointer_to_local(ctx, id, p->x, p->y, &lpx, &lpy);

    /* The link under the pointer this frame (0 = none); geometric hover doubles as the press/release target. */
    uint32_t over_link = 0U;
    if (mapped) {
        for (uint32_t i = 0; i < st->link_count; i++) {
            const nt_ui_rich_link_rect_t *lr = &st->links[i];
            const float x0 = ox + lr->x;
            const float y0 = oy + lr->y;
            if (lpx >= x0 && lpx <= x0 + lr->w && lpy >= y0 && lpy <= y0 + lr->h) {
                over_link = lr->link_id; /* topmost/first hit wins */
                break;
            }
        }
    }
    st->hovered_link = over_link;

    /* Retained press-target cell, keyed by the block id (links have no Clay element / capture cell). */
    nt_ui_rich_link_press_t *cell = (nt_ui_rich_link_press_t *)nt_ui_state(ctx, id, (uint32_t)sizeof *cell, NT_UI_RICH_LINK_TAG);
    if (pressed) {
        cell->pressed_link = over_link; /* press-edge: latch the link the press started on (0 = none) */
    }
    if (released) {
        /* Click only when the release lands on the SAME link the press started on. */
        if (over_link != 0U && over_link == cell->pressed_link) {
            st->clicked_link = over_link;
        }
        cell->pressed_link = 0U; /* release clears the latch regardless */
    }
}

/* Copy the resolved hover/click + the first solved link's BLOCK-LOCAL rect into the public result. */
static void rich_fill_result(const nt_ui_rich_state_t *st, nt_ui_rich_result_t *out) {
    out->hovered_link = st->hovered_link;
    out->clicked_link = st->clicked_link;
    if (st->link_count > 0U) {
        const nt_ui_rich_link_rect_t *lr = &st->links[0];
        out->first_link = lr->link_id;
        out->first_link_rect[0] = lr->x;
        out->first_link_rect[1] = lr->y;
        out->first_link_rect[2] = lr->w;
        out->first_link_rect[3] = lr->h;
    } else {
        out->first_link = 0U;
        out->first_link_rect[0] = out->first_link_rect[1] = out->first_link_rect[2] = out->first_link_rect[3] = 0.0F;
    }
}
// #endregion

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- entry-guard NT_ASSERT branches + a linear solve/resolve/emit pipeline
void nt_ui_rich_text(nt_ui_context_t *ctx, uint32_t id, const nt_ui_element_data_t *data, const nt_ui_rich_style_t *style, float container_w, nt_rich_align_t align, float time,
                     nt_ui_rich_result_t *out) {
    NT_ASSERT(ctx != NULL && "nt_ui_rich_text: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_rich_text: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_rich_text: style must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_rich_text: id must be non-zero (drives bbox resolve + link rect origin)");

    nt_ui_rich_state_t *st = rich_state(ctx);
    st->image_material = style->image_material;
    st->time = time; /* game-owned animation clock; no global frame clock */
    rich_solve(ctx, st, id, container_w, NT_UI_RICH_DEFAULT_FONT_SIZE, align);
    rich_resolve_links(ctx, st, id); /* hover gates effects -> must precede emit */
    rich_declare_fixed_block(st, id, data);
    if (out != NULL) {
        rich_fill_result(st, out);
    }
    /* Builder session done: release the no-nest lock so the next rich-text call (same frame) is
     * allowed. pending_rich stays as the last-state handle (probes); solved state also lives in
     * the custom command. */
    ctx->rich_session_open = false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- entry-guard NT_ASSERT branches + a linear parse/solve/resolve/emit pipeline
void nt_ui_rich_text_markup(nt_ui_context_t *ctx, uint32_t id, const nt_ui_element_data_t *data, const nt_ui_rich_tagset_t *tagset, const nt_ui_rich_style_t *style, const char *markup, size_t len,
                            float container_w, nt_rich_align_t align, float time, nt_ui_rich_result_t *out) {
    NT_ASSERT(ctx != NULL && "nt_ui_rich_text_markup: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_rich_text_markup: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_rich_text_markup: style must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_rich_text_markup: id must be non-zero (drives bbox resolve + link rect origin)");

    nt_ui_rich_parse(ctx, tagset, style, markup, len); /* parse opens its own begin/end */
    nt_ui_rich_state_t *st = rich_state(ctx);
    st->image_material = style->image_material;
    st->time = time;
    rich_solve(ctx, st, id, container_w, NT_UI_RICH_DEFAULT_FONT_SIZE, align);
    rich_resolve_links(ctx, st, id);
    rich_declare_fixed_block(st, id, data);
    if (out != NULL) {
        rich_fill_result(st, out);
    }
    ctx->rich_session_open = false; /* release the no-nest lock (see nt_ui_rich_text) */
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

nt_rich_atom_kind_t nt_ui_rich_test_run_kind(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return st->runs[run].kind;
}

uint32_t nt_ui_rich_test_run_link(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return st->runs[run].link_id;
}

uint32_t nt_ui_rich_test_run_text_len(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return (st->runs[run].kind == NT_RICH_ATOM_TEXT) ? st->runs[run].text_len : 0U;
}

const char *nt_ui_rich_test_run_text(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return st->text + st->runs[run].text_off;
}

nt_atlas_region_ref_t nt_ui_rich_test_run_image_ref(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return st->runs[run].image_ref;
}

float nt_ui_rich_test_run_image_scale(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return st->runs[run].image_scale;
}

float nt_ui_rich_test_run_image_offset_y(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return st->runs[run].image_offset_y;
}

nt_rich_valign_t nt_ui_rich_test_run_image_valign(nt_ui_context_t *ctx, uint32_t run) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(run < st->run_count);
    return st->runs[run].image_valign;
}

void nt_ui_rich_test_solve(nt_ui_context_t *ctx, float container_w, float font_size) { rich_solve(ctx, rich_state(ctx), 0U, container_w, font_size, NT_RICH_ALIGN_LEFT); }

void nt_ui_rich_test_solve_ex(nt_ui_context_t *ctx, uint32_t id, float container_w, float font_size, nt_rich_align_t align) { rich_solve(ctx, rich_state(ctx), id, container_w, font_size, align); }

uint32_t nt_ui_rich_test_line_count(nt_ui_context_t *ctx) { return rich_state(ctx)->line_count; }
uint32_t nt_ui_rich_test_atom_count(nt_ui_context_t *ctx) { return rich_state(ctx)->solved_count; }

nt_ui_rich_test_atom_t nt_ui_rich_test_atom(nt_ui_context_t *ctx, uint32_t atom) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(atom < st->solved_count);
    const nt_ui_rich_solved_atom_t *s = &st->solved[atom];
    nt_ui_rich_test_atom_t probe;
    probe.kind = s->kind;
    probe.x = s->x;
    probe.y = s->y;
    probe.w = s->w;
    probe.h = s->h;
    probe.line = s->line;
    return probe;
}

float nt_ui_rich_test_total_w(nt_ui_context_t *ctx) { return rich_state(ctx)->total_w; }
float nt_ui_rich_test_total_h(nt_ui_context_t *ctx) { return rich_state(ctx)->total_h; }

uint32_t nt_ui_rich_test_emit_span_count(nt_ui_context_t *ctx) { return rich_state(ctx)->emit_span_count; }

uint32_t nt_ui_rich_test_image_emit_count(nt_ui_context_t *ctx) { return rich_state(ctx)->image_emit_count; }
uint32_t nt_ui_rich_test_image_region(nt_ui_context_t *ctx) { return rich_state(ctx)->image_region; }
float nt_ui_rich_test_image_y(nt_ui_context_t *ctx) { return rich_state(ctx)->image_y; }

uint32_t nt_ui_rich_test_hovered_link(nt_ui_context_t *ctx) { return rich_state(ctx)->hovered_link; }
uint32_t nt_ui_rich_test_clicked_link(nt_ui_context_t *ctx) { return rich_state(ctx)->clicked_link; }
uint32_t nt_ui_rich_test_link_rect_count(nt_ui_context_t *ctx) { return rich_state(ctx)->link_count; }

uint8_t nt_ui_rich_test_atom_effect_id(nt_ui_context_t *ctx, uint32_t atom) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(atom < st->solved_count);
    return st->solved[atom].effect_id;
}

uint8_t nt_ui_rich_test_atom_layer(nt_ui_context_t *ctx, uint32_t atom) {
    nt_ui_rich_state_t *st = rich_state(ctx);
    NT_ASSERT(atom < st->solved_count);
    return st->solved[atom].layer;
}

uint32_t nt_ui_rich_test_parse_tag_depth(void) { return NT_UI_RICH_PARSE_TAG_DEPTH; }
#endif
// #endregion
