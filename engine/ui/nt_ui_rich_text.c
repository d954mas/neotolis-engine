/* Rich text: run-list SoA build + style-stack composition + builder + prototype
 * solver spike. The full word-wrap/baseline/emit solver lands across plans 04-07;
 * this TU carries the data model, the code-first builder, and the Wave-1 spike. */

#include "ui/nt_ui_rich_text.h"

#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "memory/nt_mem_scratch.h"
#include "renderers/nt_text_renderer.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_image.h" /* nt_ui_image_custom (inline-image emit path) */
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_tagset.h"
#include "utf8/nt_utf8.h"

/* Default px font size the public widget feeds the solver when a run carries no per-run
 * size (size lives in the style scale multiplier, D-67-11; absolute <size> is deferred). */
#ifndef NT_UI_RICH_DEFAULT_FONT_SIZE
#define NT_UI_RICH_DEFAULT_FONT_SIZE 16.0F
#endif

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

/* A solved, positioned atom (the solver's output, consumed by emit). TEXT atoms keep a
 * byte range into the shared buffer + the run's resolved font/size/color so emit can
 * draw_n the exact span; IMAGE/OBJECT reserve their box (emit lands in plans 05/07). */
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
    uint8_t flags; /* NT_UI_RICH_RUN_SYNTH_ITALIC -> shear at emit */
    /* IMAGE/OBJECT box context. */
    uint16_t run_idx; /* back-reference for the image/object emit (plans 05/07) */
    uint32_t link_id; /* 0 = not a link */
} nt_ui_rich_solved_atom_t;

/* One <link>-marked run's hitbox rect (populated by the solver, consumed in plan 07). */
typedef struct {
    uint32_t link_id;
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

    /* ---- Inline-image emit (RICH-67-05) ---- */
    nt_material_t image_material; /* base-style inline-image material (.id==0 -> no images) */
    uint32_t image_emit_count;    /* IMAGE atoms declared as Clay children this call (probe) */
    uint32_t image_block_bytes;   /* size of the last inline-image custom block (probe) */
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
    if (a->color_abgr != b->color_abgr || a->scale != b->scale || a->variant != b->variant || a->effect_id != b->effect_id || a->image_material.id != b->image_material.id) {
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
    /* solved[]/links[] are sized to the actual content at solve time (content-proportional,
     * not the full glyph cap) -- keeps the per-call scratch footprint tiny for a label. */

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

// #region PARSER
/* Runtime markup parser (D-67-02): tokenizes an angular `<tag>...</tag>` + self-closing
 * `<img=region/>` string and DRIVES the shared code-first builder (begin / push_* / text_n / image /
 * pop / end) -- so the run-list is identical to the equivalent builder calls by construction, never a
 * second composition path.
 *
 * Discretion decisions (D-67 Claude's Discretion; recorded in the plan summary):
 *  - Escaping: a backslash escapes the next byte -- `\<` emits a literal '<', `\\` a literal '\'.
 *  - Case: tag NAMES are case-insensitive (`<B>` == `<b>`, `<COLOR=...>` == `<color=...>`).
 *    Tag VALUES (hex digits, names hashed for the tagset) are case-sensitive to the hash.
 *  - Whitespace: preserved verbatim (the solver owns wrap; no collapse).
 *
 * Malformed markup fires NT_ASSERT (builder-validates spirit). Every scan loop is bounded by the
 * input `len` so a non-terminating `<` cannot OOB or loop (MARK-67-05). */

#define NT_UI_RICH_PARSE_TAG_DEPTH 32 /* matched open-tag stack depth cap (T-67-06-02) */

/* The intrinsic tag a `<name ...>` open maps to; CLOSE/IMG handled separately. */
typedef enum {
    RICH_TAG_BOLD,
    RICH_TAG_ITALIC,
    RICH_TAG_COLOR,
    RICH_TAG_SCALE,
    RICH_TAG_FONT,
    RICH_TAG_LINK,
    RICH_TAG_EFFECT, /* <fx=name> via the tagset; pushes effect_id */
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

/* One hex nibble; 0xFF on a non-hex char. */
static uint8_t rich_hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (uint8_t)(c - '0');
    }
    const char l = rich_lc(c);
    if (l >= 'a' && l <= 'f') {
        return (uint8_t)(l - 'a' + 10);
    }
    return 0xFFU;
}

/* Parse "#RRGGBB" (6 hex digits) into packed AABBGGRR (alpha forced opaque). Asserts on malformed. */
static uint32_t rich_parse_hex_color(const char *s, uint32_t n) {
    NT_ASSERT(n == 7U && s[0] == '#' && "rich markup: <color=#hex> must be #RRGGBB");
    uint32_t rgb = 0;
    for (uint32_t i = 1; i < 7U; i++) {
        const uint8_t nib = rich_hex_nibble(s[i]);
        NT_ASSERT(nib != 0xFFU && "rich markup: malformed hex in <color=#..>");
        rgb = (rgb << 4) | nib;
    }
    const uint8_t r = (uint8_t)((rgb >> 16) & 0xFFU);
    const uint8_t g = (uint8_t)((rgb >> 8) & 0xFFU);
    const uint8_t b = (uint8_t)(rgb & 0xFFU);
    return ((uint32_t)0xFFU << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r; /* AABBGGRR */
}

/* Parse a decimal/float value [s,s+n) (e.g. "1.5"); asserts on a non-numeric body. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- per-digit scan + NT_ASSERT validation branches
static float rich_parse_float(const char *s, uint32_t n) {
    NT_ASSERT(n > 0U && "rich markup: empty numeric value");
    float sign = 1.0F;
    uint32_t i = 0;
    if (s[0] == '-') {
        sign = -1.0F;
        i = 1;
    }
    float whole = 0.0F;
    bool any = false;
    for (; i < n && s[i] != '.'; i++) {
        NT_ASSERT(s[i] >= '0' && s[i] <= '9' && "rich markup: non-numeric in value");
        whole = (whole * 10.0F) + (float)(s[i] - '0');
        any = true;
    }
    float frac = 0.0F;
    float scale = 1.0F;
    if (i < n && s[i] == '.') {
        for (i++; i < n; i++) {
            NT_ASSERT(s[i] >= '0' && s[i] <= '9' && "rich markup: non-numeric fraction");
            scale *= 0.1F;
            frac += (float)(s[i] - '0') * scale;
            any = true;
        }
    }
    NT_ASSERT(any && "rich markup: numeric value had no digits");
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
    return RICH_TAG_NONE;
}

typedef struct {
    rich_tag_kind_t open_stack[NT_UI_RICH_PARSE_TAG_DEPTH];
    uint32_t depth;
} rich_tag_stack_t;

/* Apply a self-closing `<img=...>`: `<img=region/>` -> default atlas; `<img=alias:region/>` ->
 * the tagset alias's atlas. The region name is always resolved by name (the atlas IS the registry). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- alias-split scan + NT_ASSERT validation branches
static void rich_parse_img(nt_ui_context_t *ctx, const nt_ui_rich_tagset_t *tagset, const nt_ui_rich_style_t *base, const char *val, uint32_t vlen) {
    NT_ASSERT(vlen > 0U && "rich markup: <img=...> needs a region");
    /* Split on a single ':' -> alias:region. */
    uint32_t colon = vlen;
    for (uint32_t i = 0; i < vlen; i++) {
        if (val[i] == ':') {
            colon = i;
            break;
        }
    }
    nt_resource_t atlas = base->default_atlas.atlas;
    const char *region = val;
    uint32_t region_len = vlen;
    if (colon < vlen) {
        NT_ASSERT(tagset != NULL && "rich markup: <img=alias:..> needs a tagset");
        const uint64_t alias_hash = nt_hash64((const void *)val, colon).value;
        NT_ASSERT(nt_ui_rich_tagset_lookup_atlas(tagset, alias_hash, &atlas) && "rich markup: unknown <img> atlas alias");
        region = val + colon + 1;
        region_len = vlen - colon - 1;
        NT_ASSERT(region_len > 0U && "rich markup: <img=alias:region/> empty region");
    }
    const uint64_t region_hash = nt_hash64((const void *)region, region_len).value;
    nt_ui_rich_image(ctx, nt_atlas_ref(atlas, region_hash), NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
}

/* Dispatch one OPEN tag `<name=value>` (value may be empty) onto the builder + tag stack. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- tag-kind switch + per-tag NT_ASSERT validation branches
static void rich_open_tag(nt_ui_context_t *ctx, rich_tag_stack_t *ts_stack, const nt_ui_rich_tagset_t *tagset, const char *name, uint32_t nlen, const char *val, uint32_t vlen) {
    const rich_tag_kind_t kind = rich_tag_kind(name, nlen);
    NT_ASSERT(kind != RICH_TAG_NONE && "rich markup: unknown tag");
    switch (kind) {
    case RICH_TAG_BOLD:
        nt_ui_rich_push_bold(ctx);
        break;
    case RICH_TAG_ITALIC:
        nt_ui_rich_push_italic(ctx);
        break;
    case RICH_TAG_COLOR:
        NT_ASSERT(vlen > 0U && "rich markup: <color=..> needs a value");
        if (val[0] == '#') {
            nt_ui_rich_push_color(ctx, rich_parse_hex_color(val, vlen));
        } else {
            NT_ASSERT(tagset != NULL && "rich markup: named <color> needs a tagset");
            uint32_t abgr = 0;
            NT_ASSERT(nt_ui_rich_tagset_lookup_color(tagset, nt_hash64((const void *)val, vlen).value, &abgr) && "rich markup: unknown color name");
            nt_ui_rich_push_color(ctx, abgr);
        }
        break;
    case RICH_TAG_SCALE:
        nt_ui_rich_push_scale(ctx, rich_parse_float(val, vlen));
        break;
    case RICH_TAG_FONT: {
        NT_ASSERT(vlen > 0U && tagset != NULL && "rich markup: <font=name> needs a tagset");
        nt_font_t fam[4];
        NT_ASSERT(nt_ui_rich_tagset_lookup_font(tagset, nt_hash64((const void *)val, vlen).value, fam) && "rich markup: unknown font name");
        nt_ui_rich_push_font(ctx, fam);
        break;
    }
    case RICH_TAG_LINK:
        NT_ASSERT(vlen > 0U && "rich markup: <link=id> needs an id");
        nt_ui_rich_link(ctx, nt_hash32(val, vlen).value);
        break;
    case RICH_TAG_EFFECT: {
        NT_ASSERT(vlen > 0U && tagset != NULL && "rich markup: <fx=name> needs a tagset");
        uint8_t effect_id = 0;
        NT_ASSERT(nt_ui_rich_tagset_lookup_effect(tagset, nt_hash64((const void *)val, vlen).value, &effect_id) && "rich markup: unknown effect name");
        nt_ui_rich_push_effect(ctx, effect_id);
        break;
    }
    case RICH_TAG_NONE:
    default:
        NT_ASSERT(false && "rich markup: unreachable tag kind");
        break;
    }
    /* link is a pending property, not a stack push -- closed by </link> clearing the pending id. */
    NT_ASSERT(ts_stack->depth < NT_UI_RICH_PARSE_TAG_DEPTH && "rich markup: tag nesting too deep");
    ts_stack->open_stack[ts_stack->depth++] = kind;
}

/* Dispatch one CLOSE tag `</name>` -- must match the innermost open tag. */
static void rich_close_tag(nt_ui_context_t *ctx, rich_tag_stack_t *ts_stack, const char *name, uint32_t nlen) {
    const rich_tag_kind_t kind = rich_tag_kind(name, nlen);
    NT_ASSERT(kind != RICH_TAG_NONE && "rich markup: unknown close tag");
    NT_ASSERT(ts_stack->depth > 0U && "rich markup: close tag with no open");
    const rich_tag_kind_t top = ts_stack->open_stack[--ts_stack->depth];
    NT_ASSERT(top == kind && "rich markup: mismatched close tag");
    if (kind == RICH_TAG_LINK) {
        nt_ui_rich_link(ctx, 0U); /* clear the pending link id */
    } else {
        nt_ui_rich_pop(ctx); /* style-stack pop */
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- the tokenizer: text/tag/escape branches + bounded scans
void nt_ui_rich_parse(nt_ui_context_t *ctx, const nt_ui_rich_tagset_t *tagset, const nt_ui_rich_style_t *base, const char *markup, size_t len) {
    NT_ASSERT(ctx != NULL && "nt_ui_rich_parse: NULL ctx");
    NT_ASSERT(markup != NULL || len == 0U);

    nt_ui_rich_begin(ctx, base);
    rich_tag_stack_t ts_stack;
    ts_stack.depth = 0;

    /* Literal-text run accumulator: a flush point at every tag boundary or escape resolve. */
    char text_buf[NT_UI_RICH_MAX_TEXT_BYTES];
    uint32_t text_n = 0;

    size_t i = 0;
    while (i < len) {
        const char c = markup[i];
        if (c == '\\' && (i + 1U) < len) {
            /* Escape: the next byte is literal (\< -> '<', \\ -> '\'). */
            NT_ASSERT(text_n < NT_UI_RICH_MAX_TEXT_BYTES && "rich markup: text run overflow");
            text_buf[text_n++] = markup[i + 1U];
            i += 2U;
            continue;
        }
        if (c != '<') {
            NT_ASSERT(text_n < NT_UI_RICH_MAX_TEXT_BYTES && "rich markup: text run overflow");
            text_buf[text_n++] = c;
            i++;
            continue;
        }
        /* A '<' begins a tag. Flush any pending literal text first. */
        if (text_n > 0U) {
            nt_ui_rich_text_n(ctx, text_buf, text_n);
            text_n = 0;
        }
        /* Find the matching '>' (bounded by len -> a non-terminating '<' asserts, never loops). */
        size_t close = i + 1U;
        while (close < len && markup[close] != '>') {
            close++;
        }
        NT_ASSERT(close < len && "rich markup: unterminated tag (no '>')");

        const char *body = markup + i + 1U; /* between '<' and '>' */
        uint32_t blen = (uint32_t)(close - (i + 1U));
        NT_ASSERT(blen > 0U && "rich markup: empty tag <>");

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
                NT_ASSERT(rich_name_eq(name, nlen, "img") && "rich markup: only <img=.../> self-closes");
                rich_parse_img(ctx, tagset, base, val, vlen);
            } else {
                rich_open_tag(ctx, &ts_stack, tagset, name, nlen, val, vlen);
            }
        }
        i = close + 1U;
    }
    /* Flush the trailing literal text. */
    if (text_n > 0U) {
        nt_ui_rich_text_n(ctx, text_buf, text_n);
    }
    NT_ASSERT(ts_stack.depth == 0U && "rich markup: unclosed tag(s) at end of string");
    nt_ui_rich_end(ctx);
}
// #endregion

// #region SOLVER
/* Full word-wrap + baseline solver (generalizes the plan-03 spike). Two passes over an
 * ATOM stream (the wrap unit, not runs -- a word can span runs): PASS 1 greedy break with
 * break-anywhere overflow for over-long words; PASS 2 per-line max ascent/descent + L/C/R
 * align offset + per-atom baseline placement. Scratch-only, NO heap. The solved atoms feed
 * both emit (TEXT in this plan; IMAGE/OBJECT in plans 05/07) and the test probes. */

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
    uint16_t run_idx;
    uint32_t link_id;
} rich_atom_t;

/* x_height proxy (Open Q3): half the text ascent. Stable non-ascent reference so the
 * middle valign differs from baseline. */
static float rich_x_height(float ascent_px) { return ascent_px * 0.5F; }

static void rich_text_metrics(nt_font_t font, float size, float *out_asc, float *out_desc) {
    const nt_font_metrics_t m = nt_font_get_metrics(font);
    if (m.units_per_em == 0U) {
        *out_asc = size;
        *out_desc = 0.0F;
        return;
    }
    const float px = size / (float)m.units_per_em; /* same conversion as nt_ui_input.c:698 */
    *out_asc = (float)m.ascent * px;
    *out_desc = -(float)m.descent * px;
}

/* Append one TEXT chunk atom [off,off+len) measured under font/size. */
static void rich_push_text_atom(rich_atom_t *out, uint32_t *n, uint32_t cap, const nt_ui_rich_run_t *run, nt_font_t font, float size, uint32_t color, float t_asc, float t_desc, uint32_t off,
                                uint32_t len, float advance, bool breakable_before, uint16_t run_idx) {
    NT_ASSERT(*n < cap && "rich atom overflow (NT_UI_RICH_MAX_GLYPHS)");
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
    a->text_off = off;
    a->text_len = len;
    a->flags = run->flags;
    a->run_idx = run_idx;
    a->link_id = run->link_id;
}

/* Split an over-long word (advance > container_w) at UTF-8 codepoint boundaries into chunks
 * each <= container_w, so no atom escapes the box (break-anywhere; CSS overflow-wrap:anywhere).
 * Returns the number of chunk atoms appended. */
static void rich_break_anywhere(rich_atom_t *out, uint32_t *n, uint32_t cap, const nt_ui_rich_run_t *run, nt_font_t font, float size, uint32_t color, float t_asc, float t_desc, const char *text,
                                uint32_t word_off, uint32_t word_len, float container_w, uint16_t run_idx, bool first_breakable) {
    uint32_t chunk_start = word_off;
    uint32_t i = word_off;
    const uint32_t word_end = word_off + word_len;
    uint32_t cp = 0;
    uint32_t last_boundary = word_off; /* last codepoint boundary that still fit */
    bool emitted = false;
    while (i < word_end) {
        uint32_t state = NT_UTF8_ACCEPT;
        do {
            nt_utf8_decode(&state, &cp, (uint8_t)text[i]);
            i++;
        } while (i < word_end && state != NT_UTF8_ACCEPT && state != NT_UTF8_REJECT);
        const float w = nt_font_measure_n(font, text + chunk_start, i - chunk_start, size, 0.0F).width;
        if (w > container_w && last_boundary > chunk_start) {
            /* Commit the chunk up to the previous boundary; restart the new chunk here. */
            const uint32_t clen = last_boundary - chunk_start;
            const float cw = nt_font_measure_n(font, text + chunk_start, clen, size, 0.0F).width;
            rich_push_text_atom(out, n, cap, run, font, size, color, t_asc, t_desc, chunk_start, clen, cw, emitted ? true : first_breakable, run_idx);
            emitted = true;
            chunk_start = last_boundary;
        }
        last_boundary = i;
    }
    /* Trailing chunk (or the whole word if it never exceeded). */
    if (chunk_start < word_end) {
        const uint32_t clen = word_end - chunk_start;
        const float cw = nt_font_measure_n(font, text + chunk_start, clen, size, 0.0F).width;
        rich_push_text_atom(out, n, cap, run, font, size, color, t_asc, t_desc, chunk_start, clen, cw, emitted ? true : first_breakable, run_idx);
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
                    rich_break_anywhere(out, &n, cap, run, font, size, style->color_abgr, t_asc, t_desc, st->text, word_start, word_len, container_w, (uint16_t)ri, breakable);
                } else {
                    rich_push_text_atom(out, &n, cap, run, font, size, style->color_abgr, t_asc, t_desc, word_start, word_len, advance, breakable, (uint16_t)ri);
                }
            }
        } else if (run->kind == NT_RICH_ATOM_IMAGE) {
            /* By-name resolve (D-67-13): the atlas IS the registry. Resolve-and-memoize writes
             * the region index back into the run's ref so emit reuses it (no per-image registry). */
            nt_atlas_resolve_ref(&st->runs[ri].image_ref);
            const uint32_t reg_idx = run->image_ref.region;
            const nt_texture_region_t *reg = (reg_idx != NT_ATLAS_INVALID_REGION) ? nt_atlas_get_region(run->image_ref.atlas, reg_idx) : NULL;
            const float src_w = (reg != NULL) ? (float)reg->source_w : 1.0F;
            const float src_h = (reg != NULL) ? (float)reg->source_h : 1.0F;
            NT_ASSERT(n < cap && "rich atom overflow (NT_UI_RICH_MAX_GLYPHS)");
            rich_atom_t *a = &out[n++];
            memset(a, 0, sizeof *a);
            a->kind = NT_RICH_ATOM_IMAGE;
            a->advance = src_w * run->image_scale;
            a->w = a->advance;
            a->h = src_h * run->image_scale;
            a->valign = run->image_valign;
            a->offset_y = run->image_offset_y;
            a->color = style->color_abgr; /* the run's tint -> the inline image's lossless a_tint */
            a->breakable_before = true;   /* image: break opportunity on both sides */
            a->run_idx = (uint16_t)ri;
            a->link_id = run->link_id;
        }
        /* OBJECT atoms: emit/measure callbacks generalized in plan 07; not placed here. */
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

/* An IMAGE atom's ascent/descent demand for the line, given the line's text ascent. */
static void rich_image_metrics(const rich_atom_t *a, float text_asc, float *io_asc, float *io_desc) {
    const float h = a->h;
    if (a->valign == NT_RICH_VALIGN_MIDDLE) {
        const float xh = rich_x_height(text_asc > 0.0F ? text_asc : h);
        const float asc = (h * 0.5F) + (xh * 0.5F);
        const float desc = (h * 0.5F) - (xh * 0.5F);
        *io_asc = (asc > *io_asc) ? asc : *io_asc;
        *io_desc = (desc > *io_desc) ? desc : *io_desc;
    } else { /* BASELINE/TOP/BOTTOM: whole box above the baseline (D-67-22 max line height) */
        *io_asc = (h > *io_asc) ? h : *io_asc;
    }
}

/* Per-line max ascent/descent over the line's atoms + the line's pixel width. */
static void rich_line_metrics(const rich_atom_t *atoms, uint32_t na, uint32_t L, float *out_asc, float *out_desc, float *out_w) {
    float text_asc = 0.0F;
    *out_asc = 0.0F;
    *out_desc = 0.0F;
    *out_w = 0.0F;
    for (uint32_t i = 0; i < na; i++) {
        if (atoms[i].line == L && atoms[i].kind == NT_RICH_ATOM_TEXT) {
            text_asc = (atoms[i].asc > text_asc) ? atoms[i].asc : text_asc;
            *out_asc = (atoms[i].asc > *out_asc) ? atoms[i].asc : *out_asc;
            *out_desc = (atoms[i].desc > *out_desc) ? atoms[i].desc : *out_desc;
        }
    }
    for (uint32_t i = 0; i < na; i++) {
        if (atoms[i].line == L && atoms[i].kind == NT_RICH_ATOM_IMAGE) {
            rich_image_metrics(&atoms[i], text_asc, out_asc, out_desc);
        }
    }
    for (uint32_t i = 0; i < na; i++) {
        if (atoms[i].line == L) {
            *out_w += atoms[i].advance;
        }
    }
}

static float rich_atom_y(const rich_atom_t *a, float baseline_y, float pen_y, float line_h, float line_asc) {
    if (a->kind == NT_RICH_ATOM_TEXT) {
        return baseline_y - a->asc; /* top of the glyph box */
    }
    switch (a->valign) {
    case NT_RICH_VALIGN_MIDDLE: {
        const float xh = rich_x_height(line_asc);
        return baseline_y - ((a->h * 0.5F) + (xh * 0.5F)) + a->offset_y;
    }
    case NT_RICH_VALIGN_TOP:
        return pen_y + a->offset_y;
    case NT_RICH_VALIGN_BOTTOM:
        return pen_y + line_h - a->h + a->offset_y;
    case NT_RICH_VALIGN_BASELINE:
    default:
        return baseline_y - a->h + a->offset_y;
    }
}

/* Horizontal align offset for a line of the given width (D-67-21). */
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
 * the prev-frame nt_ui_get_bbox (D-67-23, the menu's two-pass trick -- no new getter). The
 * first frame (not found) uses the full screen width, then settles. A degenerate <=0 width
 * clamps to a minimum so the wrap loop never spins (T-67-04-02). */
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
     * byte, plus one box atom per image/object run. Cap at NT_UI_RICH_MAX_GLYPHS (T-67-04-01). */
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
    float pen_y = 0.0F;
    float max_line_w = 0.0F;
    for (uint32_t L = 0; L < st->line_count; L++) {
        float asc = 0.0F;
        float desc = 0.0F;
        float line_w = 0.0F;
        rich_line_metrics(atoms, na, L, &asc, &desc, &line_w);
        const float line_h = asc + desc;
        const float baseline_y = pen_y + asc;
        const float ox = rich_align_ox(align, box_w, line_w);
        max_line_w = (line_w > max_line_w) ? line_w : max_line_w;

        float pen_x = ox;
        for (uint32_t i = 0; i < na; i++) {
            if (atoms[i].line != L) {
                continue;
            }
            const rich_atom_t *a = &atoms[i];
            NT_ASSERT(st->solved_count < na && "rich solved-atom overflow");
            nt_ui_rich_solved_atom_t *s = &st->solved[st->solved_count++];
            s->kind = a->kind;
            s->x = pen_x;
            s->y = rich_atom_y(a, baseline_y, pen_y, line_h, asc);
            s->w = a->w;
            s->h = a->h;
            s->asc = a->asc;
            s->line = L;
            s->font = a->font;
            s->size = a->size;
            s->color = a->color;
            s->text_off = a->text_off;
            s->text_len = a->text_len;
            s->flags = a->flags;
            s->run_idx = a->run_idx;
            s->link_id = a->link_id;
            if (a->link_id != 0U) {
                NT_ASSERT(st->link_count < NT_UI_RICH_MAX_LINKS && "rich link-rect overflow");
                nt_ui_rich_link_rect_t *lr = &st->links[st->link_count++];
                lr->link_id = a->link_id;
                lr->x = pen_x;
                lr->y = pen_y;
                lr->w = a->w;
                lr->h = line_h;
            }
            pen_x += a->advance;
        }
        pen_y += line_h;
    }
    /* The FIXED block hosts the container width (explicit) or the max line width (grow). */
    st->total_w = (container_w > 0.0F) ? box_w : max_line_w;
    st->total_h = pen_y;

    /* Probe: record the first IMAGE atom's by-name-resolved region + solved y (RICH-67-05). */
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
/* Unpack a packed AABBGGRR color into a normalized RGBA float4 (text renderer order). */
static void rich_unpack_color(uint32_t abgr, float opacity, float out[4]) {
    out[0] = (float)(abgr & 0xFFU) / 255.0F;
    out[1] = (float)((abgr >> 8) & 0xFFU) / 255.0F;
    out[2] = (float)((abgr >> 16) & 0xFFU) / 255.0F;
    out[3] = ((float)((abgr >> 24) & 0xFFU) / 255.0F) * opacity;
}

/* Build the per-span model mat4 for draw_n: LAYOUT pen (ox,oy) -> world via world_mat4,
 * with the text-renderer Y-up <-> Clay Y-down sign flip on col1 (mirrors emit_text in
 * nt_ui.c). `shear` applies synthetic italic for a run that requested italic with no italic
 * family member (D-67-04/16): in text-local Y-up space, x' = x + k*y leans glyphs right
 * above the baseline -- fold k*col0 into col1. */
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
    for (uint32_t i = 0; i < st->solved_count; i++) {
        const nt_ui_rich_solved_atom_t *s = &st->solved[i];
        if (s->kind != NT_RICH_ATOM_TEXT || s->text_len == 0U) {
            continue; /* IMAGE/OBJECT emit lands in plans 05/07 (boxes already reserved) */
        }
        nt_text_renderer_set_font(s->font);
        const float baseline_y = box_y + s->y + s->asc; /* solved y is glyph-box top */
        float model[16];
        rich_span_model(frame->world_mat4, box_x + s->x, baseline_y, (s->flags & NT_UI_RICH_RUN_SYNTH_ITALIC) != 0U, model);
        float color[4];
        rich_unpack_color(s->color, frame->opacity, color);
        nt_text_renderer_draw_n(st->text + s->text_off, s->text_len, model, s->size, color, 0.0F, 0.0F);
        st->emit_span_count++;
    }
}
// #endregion

// #region image-emit
/* Inline IMAGE atoms ride the EXISTING Phase-66 custom-attr sprite path (nt_ui_image_custom,
 * D-67-14) -- exactly as nt_ui_radial.c does, but geom_mode REGION (real atlas region) with
 * the 48 B {a_tint, a_uvrect, a_layout} block: a_tint = the run's lossless float4 tint, a_uvrect
 * + a_layout walker-filled by name. Each image is a FLOATING child of the FIXED block, attached
 * at the block's top-left and offset to the solver's solved (x,y) -- so wrap/baseline stay the
 * solver's, and the image batches on the shared rich_image material. */
static void rich_declare_inline_image(nt_ui_context_t *ctx, nt_ui_rich_state_t *st, const nt_ui_rich_solved_atom_t *s) {
    const nt_ui_rich_run_t *run = &st->runs[s->run_idx];
    if (run->image_ref.region == NT_ATLAS_INVALID_REGION || run->image_ref.atlas.id == 0U) {
        return; /* unresolved name -> no-art early-out (mirror nt_ui_fill/radial), not OOB UVs */
    }

    /* Block in attr_map order {a_tint, a_uvrect, a_layout}: a_tint carries the run's lossless
     * tint float4; a_uvrect/a_layout are {0} placeholders the walker fills by name. */
    float blk[12];
    memset(blk, 0, sizeof blk);
    rich_unpack_color(s->color, 1.0F, &blk[0]); /* a_tint @ 0..3 (lossless; opacity rides element_data) */
    st->image_block_bytes = (uint32_t)sizeof blk;

    static const char *const attr_names[] = {"a_tint", "a_uvrect", "a_layout", NULL};
    const nt_ui_image_custom_t img = {
        .atlas = run->image_ref.atlas,
        .region_index = run->image_ref.region,
        .material = st->image_material,
        .custom_attrs = blk,
        .custom_bytes = (uint8_t)sizeof blk,
        .attr_names = attr_names,
        .geom_mode = NT_UI_IMAGE_GEOM_REGION,
        .slice9_scale = 1.0F,
        .color_packed = 0xFFFFFFFFU, /* tint lives in a_tint, not color_packed */
    };

    /* Position at the solved (x,y): a floating child attached to the FIXED block's top-left,
     * slid by the solver offset. The transform offset is render-only (no layout effect). */
    nt_ui_transform_t tt = nt_ui_transform_defaults();
    tt.offset_x = s->x;
    tt.offset_y = s->y;
    nt_ui_element_data_t *idata = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(idata != NULL && "nt_ui_rich_text: scratch alloc failed (image data)");
    *idata = (nt_ui_element_data_t){.user_data = NULL, .layer = 0U, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_TRANSFORM, .transform = tt, .opacity = 1.0F};

    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(s->w), CLAY_SIZING_FIXED(s->h)}},
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}},
    };
    nt_ui_image_custom(ctx, idata, &img, &decl);
    st->image_emit_count++;
}

/* Declare the ONE measured Clay element hosting the solved run-list (D-67-03, Architecture A)
 * -- never a per-run Clay element. The custom data routes the walk back to our self-emit.
 * Inline IMAGE atoms are declared as FLOATING children of this block (positioned at the solved
 * (x,y)); their textured sprite emit rides nt_ui_image_custom (RICH-67-05). */
static void rich_declare_fixed_block(nt_ui_context_t *ctx, nt_ui_rich_state_t *st, const nt_ui_element_data_t *data) {
    nt_ui_custom_data_t *cd = NT_MEM_SCRATCH_ALLOC(nt_ui_custom_data_t);
    NT_ASSERT(cd != NULL && "nt_ui_rich_text: scratch alloc failed (custom data)");
    *cd = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_RICH_TEXT, .data = st};

    Clay_ElementDeclaration decl = {0};
    decl.layout.sizing.width = CLAY_SIZING_FIXED(st->total_w);
    decl.layout.sizing.height = CLAY_SIZING_FIXED(st->total_h);
    decl.custom = (Clay_CustomElementConfig){.customData = cd};
    decl.userData = (void *)data;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(decl);
    /* Inline images: FLOATING children declared inside the FIXED block scope. Only when the base
     * style supplies a material (otherwise images are skipped -- text-only stays a single block). */
    st->image_emit_count = 0;
    if (st->image_material.id != 0U) {
        for (uint32_t i = 0; i < st->solved_count; i++) {
            if (st->solved[i].kind == NT_RICH_ATOM_IMAGE) {
                rich_declare_inline_image(ctx, st, &st->solved[i]);
            }
        }
    }
    nt_ui_clay_priv_close_element();
}
// #endregion

void nt_ui_rich_text(nt_ui_context_t *ctx, uint32_t id, const nt_ui_element_data_t *data, const nt_ui_rich_style_t *style, float container_w, nt_rich_align_t align, float time) {
    NT_ASSERT(ctx != NULL && "nt_ui_rich_text: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_rich_text: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_rich_text: style must be non-NULL");
    (void)time; /* per-atom effects (D-67-18) land in plan 07 */

    nt_ui_rich_state_t *st = rich_state(ctx);
    st->image_material = style->image_material;
    rich_solve(ctx, st, id, container_w, NT_UI_RICH_DEFAULT_FONT_SIZE, align);
    rich_declare_fixed_block(ctx, st, data);
}

void nt_ui_rich_text_markup(nt_ui_context_t *ctx, uint32_t id, const nt_ui_element_data_t *data, const nt_ui_rich_tagset_t *tagset, const nt_ui_rich_style_t *style, const char *markup, size_t len,
                            float container_w, nt_rich_align_t align, float time) {
    NT_ASSERT(ctx != NULL && "nt_ui_rich_text_markup: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_rich_text_markup: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_rich_text_markup: style must be non-NULL");
    (void)time; /* per-atom effects (D-67-18) land in plan 07 */

    nt_ui_rich_parse(ctx, tagset, style, markup, len); /* parse opens its own begin/end */
    nt_ui_rich_state_t *st = rich_state(ctx);
    st->image_material = style->image_material;
    rich_solve(ctx, st, id, container_w, NT_UI_RICH_DEFAULT_FONT_SIZE, align);
    rich_declare_fixed_block(ctx, st, data);
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
uint32_t nt_ui_rich_test_image_block_bytes(nt_ui_context_t *ctx) { return rich_state(ctx)->image_block_bytes; }
uint32_t nt_ui_rich_test_image_region(nt_ui_context_t *ctx) { return rich_state(ctx)->image_region; }
float nt_ui_rich_test_image_y(nt_ui_context_t *ctx) { return rich_state(ctx)->image_y; }
#endif
// #endregion
