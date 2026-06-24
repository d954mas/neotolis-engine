/* Rich-text markup parser + tagset vocabulary. No GL: the parser writes a frame-scratch
 * run-list (read back via the build probes); the tagset is a plain owned struct. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_fx.h"
#include "ui/nt_ui_rich_tagset.h"
#include "ui/nt_ui_rich_text.h"
#include "unity.h"
#include "utf8/nt_utf8.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* ---- test log sink: count WARN lines + record the last message ----
 * Markup data errors now log-once-per-distinct-message (nt_log_warn_unique) instead of asserting.
 * This sink counts every warn line the real nt_log fans out, so the dedup tests can assert "logged
 * exactly once" / "two distinct errors -> two lines". Registered per-test (add in arrange, remove in
 * the assert) so counts are local. nt_log_warn_unique dedups PROGRAM-WIDE + saturating, so the dedup
 * tests must use message strings UNIQUE to this process (distinct bad tokens), never reused elsewhere. */
#define SINK_MSG_CAP 256
typedef struct {
    uint32_t warn_count;
    char last_msg[SINK_MSG_CAP];
} test_log_sink_t;
static test_log_sink_t s_sink;

static void test_warn_sink(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)domain;
    test_log_sink_t *s = (test_log_sink_t *)user;
    if (level == NT_LOG_LEVEL_WARN && s != NULL) {
        s->warn_count++;
        (void)snprintf(s->last_msg, sizeof s->last_msg, "%s", msg ? msg : "");
    }
}

static void sink_attach(void) {
    s_sink.warn_count = 0;
    s_sink.last_msg[0] = '\0';
    nt_log_set_level(NT_LOG_LEVEL_INFO); /* ensure WARN is not filtered out */
    nt_log_add_sink(test_warn_sink, &s_sink);
}
static void sink_detach(void) { nt_log_remove_sink(test_warn_sink, &s_sink); }

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
}

void tearDown(void) {
    sink_detach(); /* idempotent: a no-op when not attached */
    ui_walker_fixture_shutdown(&s_fx);
}

static uint64_t h(const char *s) { return nt_hash64_str(s).value; }

/* (1) register_font then lookup by name returns the family; an unknown name misses. */
static void test_tagset_font_register_lookup(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    const nt_font_t fam[4] = {{.id = 10}, {.id = 11}, {.id = 12}, {.id = 13}};
    nt_ui_rich_tagset_register_font(&ts, "heading", fam);

    nt_font_t out[4];
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_font(&ts, h("heading"), out));
    TEST_ASSERT_EQUAL_UINT32(10U, out[0].id);
    TEST_ASSERT_EQUAL_UINT32(13U, out[3].id);

    nt_font_t miss[4];
    TEST_ASSERT_FALSE(nt_ui_rich_tagset_lookup_font(&ts, h("body"), miss));
}

/* (2) register_color / register_atlas / register_effect resolve by name. */
static void test_tagset_color_atlas_effect(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    nt_ui_rich_tagset_register_color(&ts, "gold", 0xFF00D7FFU);
    nt_ui_rich_tagset_register_atlas(&ts, "icons", (nt_resource_t){.id = 42U});
    nt_ui_rich_tagset_register_effect(&ts, "wave", 3U);

    uint32_t color = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_color(&ts, h("gold"), &color));
    TEST_ASSERT_EQUAL_HEX32(0xFF00D7FFU, color);

    nt_resource_t atlas = {0};
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_atlas(&ts, h("icons"), &atlas));
    TEST_ASSERT_EQUAL_UINT32(42U, atlas.id);

    uint8_t fx = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_effect(&ts, h("wave"), &fx));
    TEST_ASSERT_EQUAL_UINT8(3U, fx);
}

/* A stub custom effect fn (identity) used only to prove tagset custom registration/lookup. */
static nt_ui_rich_fx_result_t parse_stub_fx(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                            void *user_data) {
    (void)atom_idx;
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)time;
    (void)hovered;
    (void)user_data;
    return nt_ui_rich_fx_identity(base_color);
}

/* Stub object measure fn (fixed box) used only to prove object-tag registration guards. */
static nt_ui_rich_object_measure_t parse_stub_object_measure(void *user_data) {
    (void)user_data;
    return (nt_ui_rich_object_measure_t){.width = 8.0F, .height = 8.0F, .ascent = 8.0F};
}

/* (2b) register_effect_fn registers a CUSTOM effect: the full lookup returns fn+user_data, and the
 * stock-only lookup MISSES it (so a custom name never resolves to a bogus stock id). */
static void test_tagset_custom_effect_fn(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    int marker = 0;
    nt_ui_rich_tagset_register_effect_fn(&ts, "customfx", parse_stub_fx, &marker);

    /* Full lookup resolves the custom entry to its fn + user_data. */
    uint8_t id = 0xFFU;
    nt_ui_rich_fx_fn fn = NULL;
    void *user = NULL;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_effect_fn(&ts, h("customfx"), &id, &fn, &user));
    TEST_ASSERT_TRUE_MESSAGE(fn == parse_stub_fx, "custom lookup returns the registered fn");
    TEST_ASSERT_TRUE_MESSAGE(user == &marker, "custom lookup returns the registered user_data");

    /* Stock-only lookup misses the custom entry (fn != NULL). */
    uint8_t stock_id = 0;
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_rich_tagset_lookup_effect(&ts, h("customfx"), &stock_id), "stock-only lookup misses a custom entry");

    /* register_effect_fn requires a non-NULL fn. */
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_effect_fn(&ts, "bad", NULL, NULL));
}

/* (2c) a STOCK entry coexists with custom entries: it resolves via BOTH the stock-only lookup and
 * the full lookup (which reports fn==NULL for a stock entry). */
static void test_tagset_stock_effect_coexists(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    int marker = 0;
    nt_ui_rich_tagset_register_effect_fn(&ts, "customfx", parse_stub_fx, &marker);
    nt_ui_rich_tagset_register_effect(&ts, "stockfx", NT_UI_RICH_FX_ID_PULSE);

    uint8_t stock_id = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_effect(&ts, h("stockfx"), &stock_id));
    TEST_ASSERT_EQUAL_UINT8(NT_UI_RICH_FX_ID_PULSE, stock_id);

    uint8_t id = 0;
    nt_ui_rich_fx_fn fn = parse_stub_fx;
    void *user = NULL;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_effect_fn(&ts, h("stockfx"), &id, &fn, &user));
    TEST_ASSERT_TRUE_MESSAGE(fn == NULL, "stock entry full-lookup reports fn==NULL");
    TEST_ASSERT_EQUAL_UINT8(NT_UI_RICH_FX_ID_PULSE, id);
}

/* (3) re-registering a name overrides it in place (no duplicate entry). */
static void test_tagset_override_in_place(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFF112233U);
    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFFAABBCCU);
    TEST_ASSERT_EQUAL_UINT32(1U, ts.color_count);

    uint32_t color = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_color(&ts, h("accent"), &color));
    TEST_ASSERT_EQUAL_HEX32(0xFFAABBCCU, color);
}

/* (4) reset empties every table; lookups then miss. */
static void test_tagset_reset(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_color(&ts, "gold", 0xFF00D7FFU);
    nt_ui_rich_tagset_reset(&ts);

    uint32_t color = 0;
    TEST_ASSERT_EQUAL_UINT32(0U, ts.color_count);
    TEST_ASSERT_FALSE(nt_ui_rich_tagset_lookup_color(&ts, h("gold"), &color));
}

/* (4b) an empty registration name ("") is a dead slot the parser can never address (value
 * parsers guard vlen>0) -> fail-early NT_ASSERT in every register_* (including the fn/object-tag
 * variants, which guard name[0]!='\0' too). */
static void test_tagset_empty_name_asserts(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    const nt_font_t fam[4] = {{.id = 1}, {.id = 2}, {.id = 3}, {.id = 4}};
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_font(&ts, "", fam));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_atlas(&ts, "", (nt_resource_t){.id = 1U}));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_color(&ts, "", 0xFFFFFFFFU));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_effect(&ts, "", 1U));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_effect_fn(&ts, "", parse_stub_fx, NULL));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_object_tag(&ts, "", parse_stub_object_measure, NULL, NULL));
}

/* ---- parser malformed-markup death tests ---- */
static void parse_lit(const char *m) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, m, strlen(m));
}

/* (5) unclosed tag at end of string is UNTRUSTED data -> graceful: no trap, the run captured its
 * composed style at append time, so "HP" is still bold (the <b> push stands). */
static void test_parse_unclosed_tag_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<b>HP", 5U); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, runs, "unclosed <b>HP -> one text run, no trap");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_UI_RICH_VARIANT_BOLD, nt_ui_rich_test_run_style(s_fx.ctx, 0).variant, "HP carries the bold push (run captured style at append)");
}

/* (6) mismatched close (<b>HP</i>) -> graceful: no trap, the </i> closes the <b> slot (balance on the
 * OPEN, never an underflow-pop). HP is bold; the stack returns to base. */
static void test_parse_mismatched_close_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<b>HP</i>x", 10U); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "mismatched close produced runs, no trap");
    const nt_ui_rich_style_t last = nt_ui_rich_test_run_style(s_fx.ctx, runs - 1U);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, last.variant, "trailing x is back at base (mismatched close still balanced the <b>)");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF112233U, last.color_abgr, "trailing x carries the base color");
}

/* (7) unknown tag (<bogus>x</bogus>) -> graceful: log + skip BOTH the open and the (unknown) close,
 * so x carries the base style and the run-list is as-if the tag were absent. */
static void test_parse_unknown_tag_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF445566U;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<bogus>x</bogus>", 16U); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, runs, "unknown tag skipped -> one text run for 'x'");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF445566U, nt_ui_rich_test_run_style(s_fx.ctx, 0).color_abgr, "x carries the base style (unknown tag pushed nothing)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, nt_ui_rich_test_run_style(s_fx.ctx, 0).variant, "x is unstyled");
}

/* (8) malformed hex in <color=#..> -> graceful: log + opaque white (the parse degrades to a visible
 * unstyled-white run rather than trapping). */
static void test_parse_bad_hex_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<color=#zzz>x</color>", 21U); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, runs, "bad hex -> one text run (color pushed opaque white)");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFFFFFFFFU, nt_ui_rich_test_run_style(s_fx.ctx, 0).color_abgr, "bad hex degrades to opaque white");
}

/* (9) a close tag with no matching open (HP</b>) -> graceful: log + no-op, HP is the only run. */
static void test_parse_orphan_close_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF778899U;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "HP</b>", 6U); /* no trap, no stack underflow */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, runs, "orphan close -> one text run for 'HP'");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF778899U, nt_ui_rich_test_run_style(s_fx.ctx, 0).color_abgr, "HP carries the base style");
}

/* (9b) a mismatched link/style close (<link=1></b>): top==LINK but the close kind==BOLD -> graceful:
 * the close balances on `top` (LINK -> clear pending), never nt_ui_rich_pop past base. No trap. */
static void test_parse_mismatched_link_close_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<link=1>x</b>y", 14U); /* no trap, no underflow-pop */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "mismatched link close produced runs, no trap");
}

/* (9c) an empty numeric value (<scale=>) -> graceful: rich_parse_float logs+returns 0.0F, then the
 * SCALE path validates >0 and degrades to identity 1.0 BEFORE the builder (no <=0 font size, no trap). */
static void test_parse_empty_scale_value_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.scale = 1.0F;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<scale=>x</scale>y", 18U); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "empty <scale=> -> runs, no trap");
    /* x is inside the (degraded-to-1.0) scale push: its scale must be base*1.0 == 1.0, never 0. */
    const float sx = nt_ui_rich_test_run_style(s_fx.ctx, 0).scale;
    TEST_ASSERT_TRUE_MESSAGE(sx > 0.5F && sx < 2.0F, "empty <scale=> degrades to identity (scale ~1, never 0)");
}

/* Parse a <fx=...> markup against a tagset that knows the stock "wave" + a custom "fade". */
static void parse_fx(const char *m) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_effect(&ts, "wave", NT_UI_RICH_FX_ID_WAVE);
    nt_ui_rich_tagset_register_effect_fn(&ts, "fade", parse_stub_fx, NULL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, m, strlen(m));
}

/* (8b) a well-formed <fx=wave amp=8 speed=3> parses without an assert (the happy path). */
static void test_parse_fx_params_ok(void) {
    parse_fx("<fx=wave amp=8 speed=3>hi</fx>");
    parse_fx("<fx=wave speed=2.5>hi</fx>"); /* a single param + a float value */
    TEST_ASSERT_TRUE_MESSAGE(true, "tuned <fx=...> markup parsed without an assert");
}

/* (8c) a bad float value in an fx param -> graceful: rich_parse_float logs+returns 0.0F (= default),
 * the <fx=wave> effect still pushes (the name resolved); "hi" is present. No trap. */
static void test_parse_fx_bad_float_graceful(void) {
    parse_fx("<fx=wave amp=8x>hi</fx>"); /* no trap */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) >= 1U, "bad fx float degrades, effect still parses");
}

/* (8d) an unknown fx param key -> graceful: log + skip the param, the effect still pushes. No trap. */
static void test_parse_fx_unknown_key_graceful(void) {
    parse_fx("<fx=wave bogus=3>hi</fx>"); /* no trap */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) >= 1U, "unknown fx key skipped, effect still parses");
}

/* (8e) a param token with no '=' -> graceful: log + skip the token, the effect still pushes. No trap. */
static void test_parse_fx_no_equals_graceful(void) {
    parse_fx("<fx=wave amp>hi</fx>"); /* no trap, no OOB underflow read */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) >= 1U, "fx param with no '=' skipped, effect still parses");
}

/* (8f) k=v params on a CUSTOM-fn effect name -> graceful: log + push the custom fn IGNORING the
 * params (the fn is valid; only the params don't apply). No trap. */
static void test_parse_fx_params_on_custom_graceful(void) {
    parse_fx("<fx=fade amp=8>hi</fx>"); /* no trap */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) >= 1U, "custom-fn fx with params still parses (params ignored)");
}

/* (8g) an empty fx param value (amp=) -> graceful: rich_parse_float logs+returns 0.0F (= "use
 * default"), the effect still pushes. No trap, no OOB read. */
static void test_parse_fx_empty_value_graceful(void) {
    parse_fx("<fx=wave amp=>hi</fx>"); /* no trap */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) >= 1U, "empty fx value degrades to default, effect still parses");
}

/* Parse markup against a tagset that registers an OBJECT tag "widget". The parse-side name hash
 * (nt_hash64(val, vlen)) MUST equal the register-side hash (nt_hash64_str) or lookup_object misses. */
static void parse_obj(const char *m) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_object_tag(&ts, "widget", parse_stub_object_measure, NULL, NULL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, m, strlen(m));
}

/* (8h) <obj=name/> self-closes and dispatches via the tagset -> a NT_RICH_ATOM_OBJECT run exists
 * (proves the parse-side hash matches the register-side hash and the run is appended). */
static void test_parse_obj_self_close_emits_object_run(void) {
    parse_obj("a <obj=widget/> b");
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    bool found_object = false;
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_OBJECT) {
            found_object = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_object, "<obj=widget/> produced a NT_RICH_ATOM_OBJECT run");
}

/* (8i) <obj=nope/> with a name not in the tagset -> graceful: log + skip, no OBJECT run produced. */
static void test_parse_obj_unknown_graceful(void) {
    parse_obj("a<obj=nope/>b"); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    bool found_object = false;
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_OBJECT) {
            found_object = true;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(found_object, "unknown <obj> produced NO object run (skipped)");
}

/* (8j) <obj/> with an empty name -> graceful: log + skip, no OBJECT run. */
static void test_parse_obj_empty_name_graceful(void) {
    parse_obj("a<obj/>b"); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    bool found_object = false;
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_OBJECT) {
            found_object = true;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(found_object, "empty-name <obj/> produced NO object run (skipped)");
}

/* (8k) <obj=x/> with a NULL tagset (a legit text-only config) -> graceful: log + skip (early-return
 * BEFORE the lookup derefs the NULL tagset), no OBJECT run. No trap. */
static void test_parse_obj_null_tagset_graceful(void) {
    parse_lit("a<obj=x/>b"); /* tagset NULL; no trap, no NULL deref */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    bool found_object = false;
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_OBJECT) {
            found_object = true;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(found_object, "<obj> with NULL tagset produced NO object run (skipped)");
}

/* (8l) <img=alias:region/> with a NULL tagset -> NT_ASSERT in FULL (alias needs a tagset). Same OFF
 * hard guard (early-return inside the colon branch before nt_ui_rich_tagset_lookup_atlas derefs ts);
 * by-construction OFF-safe, not unit-testable without an OFF preset. A VALID base is supplied so the
 * trap is the tagset assert (the colon branch), not the base-NULL guard at the top of rich_parse_img. */
static void parse_img_null_tagset(const char *m) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));
}
/* (8l) <img=alias:region/> with a NULL tagset (alias needs a tagset) -> graceful: log + skip the
 * image (early-return BEFORE the lookup derefs the NULL tagset), no IMAGE run. No trap. */
static void test_parse_img_alias_null_tagset_graceful(void) {
    parse_img_null_tagset("a<img=a:b/>b"); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    bool found_image = false;
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_IMAGE) {
            found_image = true;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(found_image, "<img=alias:..> with NULL tagset produced NO image run (skipped)");
}

/* (8m) named <color>/<font>/<fx> with a NULL tagset -> graceful: log + skip the tag (break BEFORE the
 * lookup derefs the NULL tagset), so 'x' carries the base style. No trap. */
static void test_parse_color_null_tagset_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF010203U;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<color=gold>x</color>", 21U); /* no trap, no NULL deref */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_rich_test_run_count(s_fx.ctx), "named color w/ NULL tagset skipped -> one run");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF010203U, nt_ui_rich_test_run_style(s_fx.ctx, 0).color_abgr, "x carries the base color (named color skipped)");
}
static void test_parse_font_null_tagset_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    const nt_font_t base_face = {.id = 9};
    for (uint32_t i = 0; i < 4U; i++) {
        base.font_id[i] = base_face;
    }
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<font=mono>x</font>", 19U); /* no trap, no NULL deref */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_rich_test_run_count(s_fx.ctx), "named font w/ NULL tagset skipped -> one run");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(9U, nt_ui_rich_test_run_style(s_fx.ctx, 0).font_id[0].id, "x carries the base font (named font skipped)");
}
static void test_parse_fx_null_tagset_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<fx=wave>x</fx>", 15U); /* no trap, no NULL deref */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_rich_test_run_count(s_fx.ctx), "named fx w/ NULL tagset skipped -> one run");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, nt_ui_rich_test_run_style(s_fx.ctx, 0).effect_id, "x carries no effect (fx skipped)");
}

/* (8n) <img=alias:region/> with a NON-null tagset that lacks the alias -> graceful: log + skip the
 * image (never fall back to the default atlas with the wrong region), no IMAGE run. No trap. */
static void parse_img_no_alias(const char *m) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts); /* a valid tagset that registers NO atlas alias -> the alias lookup misses */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, m, strlen(m));
}
static void test_parse_img_unknown_alias_graceful(void) {
    parse_img_no_alias("a<img=nope:region/>b"); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    bool found_image = false;
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_IMAGE) {
            found_image = true;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(found_image, "unknown <img> alias produced NO image run (skipped)");
}

/* (img-attrs) markup `<img=region scale=1.8 oy=-4 valign=middle/>` produces a run BYTE-IDENTICAL to the
 * builder nt_ui_rich_image(ref, MIDDLE, -4, 1.8): same kind, same region ref, same scale/oy/valign. */
static void test_parse_img_attrs_match_builder(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;

    /* BUILDER: a single inline image with explicit scale/oy/valign. */
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_image(s_fx.ctx, nt_atlas_ref(s_fx.atlas.handle, h("heart")), NT_RICH_VALIGN_MIDDLE, -4.0F, 1.8F);
    nt_ui_rich_end(s_fx.ctx);

    const uint32_t b_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, b_runs, "builder: one <img> -> one run");
    const nt_atlas_region_ref_t b_ref = nt_ui_rich_test_run_image_ref(s_fx.ctx, 0);
    const nt_rich_valign_t b_valign = nt_ui_rich_test_run_image_valign(s_fx.ctx, 0);
    /* The bit-pattern of the run's scale/oy: byte-identity means the SAME float bits, so compare ints
     * (UNITY_EXCLUDE_FLOAT precludes TEST_ASSERT_EQUAL_FLOAT). */
    uint32_t b_scale_bits = 0;
    uint32_t b_oy_bits = 0;
    {
        const float bs = nt_ui_rich_test_run_image_scale(s_fx.ctx, 0);
        const float bo = nt_ui_rich_test_run_image_offset_y(s_fx.ctx, 0);
        memcpy(&b_scale_bits, &bs, sizeof bs);
        memcpy(&b_oy_bits, &bo, sizeof bo);
    }

    /* PARSER: byte-identical markup with the attr tail. */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "<img=heart scale=1.8 oy=-4 valign=middle/>";
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));

    const uint32_t p_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b_runs, p_runs, "parser run count == builder");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_RICH_ATOM_IMAGE, nt_ui_rich_test_run_kind(s_fx.ctx, 0), "parser run is an IMAGE");
    const nt_atlas_region_ref_t p_ref = nt_ui_rich_test_run_image_ref(s_fx.ctx, 0);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b_ref.atlas.id, p_ref.atlas.id, "parser image atlas == builder");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b_ref.region, p_ref.region, "parser image region == builder");
    uint32_t p_scale_bits = 0;
    uint32_t p_oy_bits = 0;
    {
        const float ps = nt_ui_rich_test_run_image_scale(s_fx.ctx, 0);
        const float po = nt_ui_rich_test_run_image_offset_y(s_fx.ctx, 0);
        memcpy(&p_scale_bits, &ps, sizeof ps);
        memcpy(&p_oy_bits, &po, sizeof po);
    }
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(b_scale_bits, p_scale_bits, "parser image scale bits == builder (1.8)");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(b_oy_bits, p_oy_bits, "parser image oy bits == builder (-4)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)b_valign, (uint8_t)nt_ui_rich_test_run_image_valign(s_fx.ctx, 0), "parser image valign == builder (MIDDLE)");
}

/* (img-attrs) a bare <img=heart/> (no attr tail) keeps the historical defaults: scale=1, oy=0, MIDDLE. */
static void test_parse_img_no_attrs_defaults(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "<img=heart/>";
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_rich_test_run_count(s_fx.ctx), "bare <img=heart/> -> one run");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_RICH_ATOM_IMAGE, nt_ui_rich_test_run_kind(s_fx.ctx, 0), "bare <img> run is an IMAGE");
    uint32_t scale_bits = 0;
    uint32_t oy_bits = 0;
    const float one = 1.0F;
    const float zero = 0.0F;
    uint32_t one_bits = 0;
    uint32_t zero_bits = 0;
    {
        const float s = nt_ui_rich_test_run_image_scale(s_fx.ctx, 0);
        const float o = nt_ui_rich_test_run_image_offset_y(s_fx.ctx, 0);
        memcpy(&scale_bits, &s, sizeof s);
        memcpy(&oy_bits, &o, sizeof o);
        memcpy(&one_bits, &one, sizeof one);
        memcpy(&zero_bits, &zero, sizeof zero);
    }
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(one_bits, scale_bits, "bare <img> default scale == 1");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(zero_bits, oy_bits, "bare <img> default oy == 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_RICH_VALIGN_MIDDLE, (uint8_t)nt_ui_rich_test_run_image_valign(s_fx.ctx, 0), "bare <img> default valign == MIDDLE");
}

/* Probe the first IMAGE run's scale bits; returns true if an IMAGE run exists. */
static bool first_image_scale(uint32_t *out_bits) {
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_IMAGE) {
            const float s = nt_ui_rich_test_run_image_scale(s_fx.ctx, i);
            memcpy(out_bits, &s, sizeof s);
            return true;
        }
    }
    return false;
}

/* (img-attrs) a malformed attr float (<img=heart scale=abc/>) -> graceful: rich_parse_float logs +
 * returns 0.0F, then rich_parse_img validates scale>0 and degrades to 1.0 BEFORE the builder. The
 * image is present with scale clamped to 1.0 -- never trapped, never a 0/NaN box. */
static void test_parse_img_bad_attr_float_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<img=heart scale=abc/>", 22U); /* no trap */
    uint32_t scale_bits = 0;
    uint32_t one_bits = 0;
    const float one = 1.0F;
    memcpy(&one_bits, &one, sizeof one);
    TEST_ASSERT_TRUE_MESSAGE(first_image_scale(&scale_bits), "bad attr float still emits an IMAGE run (degraded)");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(one_bits, scale_bits, "bad scale degrades to 1.0 (not 0/NaN)");
}

/* (img-attrs) an unknown valign keyword (<img=heart valign=sideways/>) -> graceful: log + keep the
 * default MIDDLE. The image is present. No trap. */
static void test_parse_img_bad_valign_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<img=heart valign=sideways/>", 28U); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    bool found_image = false;
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_IMAGE) {
            found_image = true;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_RICH_VALIGN_MIDDLE, (uint8_t)nt_ui_rich_test_run_image_valign(s_fx.ctx, i), "bad valign keeps the default MIDDLE");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_image, "bad valign still emits an IMAGE run");
}

/* (img-attrs) an unknown attr key (<img=heart bogus=3/>) -> graceful: log + skip the attr, the image
 * is present with default scale/valign. No trap. */
static void test_parse_img_unknown_attr_key_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<img=heart bogus=3/>", 20U); /* no trap */
    uint32_t scale_bits = 0;
    TEST_ASSERT_TRUE_MESSAGE(first_image_scale(&scale_bits), "unknown attr key still emits an IMAGE run (attr skipped)");
}

/* (10) over-deep <b> nesting (40 > NT_UI_RICH_PARSE_TAG_DEPTH 32) -> graceful: the over-cap tag-depth
 * guard logs + stops tracking further opens (the hard cap survives), never writing past open_stack[].
 * No trap; the parse completes bounded. Pathological untrusted nesting must degrade, not crash. */
static void test_parse_over_deep_style_stack_graceful(void) {
    char buf[256];
    uint32_t n = 0;
    for (uint32_t i = 0; i < 40U; i++) { /* 40 > NT_UI_RICH_PARSE_TAG_DEPTH (32) */
        buf[n++] = '<';
        buf[n++] = 'b';
        buf[n++] = '>';
    }
    buf[n++] = 'x'; /* trailing text so a run is produced */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, n); /* no trap, no OOB */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) >= 1U, "over-deep nesting parses bounded (no trap, run produced)");
}

/* (10c) cap-parity regression (#1+#2): a BALANCED <b>xN + </b>xN at N == the parser tag cap must
 * parse WITHOUT a trap (no over-cap), and the trailing text must carry the BASE style -- proof the
 * style stack balanced exactly back to base (depth never desynced from the tag stack, so no pop
 * underflow / OOB in OFF). Sweeps N up to NT_UI_RICH_PARSE_TAG_DEPTH. */
static void test_parse_balanced_at_cap_stays_synced(void) {
    /* Read the REAL NT_UI_RICH_PARSE_TAG_DEPTH (private to nt_ui_rich_text.c) via the test surface so the
     * sweep always lands on the true cap. The engine _Static_assert ties NT_UI_RICH_STACK_DEPTH == this + 1,
     * so a balanced nest at this depth must NOT over-cap the style stack. */
    const uint32_t parse_tag_depth = nt_ui_rich_test_parse_tag_depth();
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U; /* a base tint distinguishable from any <b> push (bold doesn't change color) */
    for (uint32_t depth = 1U; depth <= parse_tag_depth; depth++) {
        char buf[512];
        uint32_t n = 0;
        for (uint32_t i = 0; i < depth; i++) { /* depth opens */
            buf[n++] = '<';
            buf[n++] = 'b';
            buf[n++] = '>';
        }
        for (uint32_t i = 0; i < depth; i++) { /* depth balanced closes */
            buf[n++] = '<';
            buf[n++] = '/';
            buf[n++] = 'b';
            buf[n++] = '>';
        }
        buf[n++] = 'x'; /* trailing text -> must carry the BASE style (stack back at base) */

        nt_mem_scratch_reset();
        s_fx.ctx->pending_rich = NULL;
        s_fx.ctx->rich_session_open = false;
        nt_ui_rich_parse(s_fx.ctx, NULL, &base, buf, n); /* no trap in FULL: balanced, never over-cap */

        const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
        TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "balanced-at-cap markup produced the trailing-text run");
        /* The LAST run is the trailing "x": its composed style must equal base (variant cleared, color base). */
        const nt_ui_rich_style_t last = nt_ui_rich_test_run_style(s_fx.ctx, runs - 1U);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, last.variant, "trailing text after balanced <b> nesting must be NON-bold (stack back at base)");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF112233U, last.color_abgr, "trailing text must carry the BASE color (style stack balanced back to base)");
    }
}

/* (10d) mixed-tag stack-sync: interleave style-stack PUSH tags (<color>/<scale>/<font>) with <link>
 * (which does NOT push -- it only sets the pending link field) in a nested+balanced markup. Because
 * rich_close_tag branches on top==RICH_TAG_LINK to NOT pop the style stack, the trailing text after
 * all closes must carry the BASE style -- proof the style stack and the tag stack stay in sync across
 * the link asymmetry (a desync here would leave the trailing run mid-stack: wrong color/scale/font). */
static void test_parse_mixed_tag_stack_sync(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFFAABBCCU);
    const nt_font_t hdr[4] = {{.id = 21}, {.id = 22}, {.id = 23}, {.id = 24}};
    nt_ui_rich_tagset_register_font(&ts, "hdr", hdr);

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U; /* distinguishable from the accent push */
    base.scale = 1.0F;
    const nt_font_t base_face = {.id = 7};
    for (uint32_t i = 0; i < 4U; i++) {
        base.font_id[i] = base_face; /* a base family distinct from "hdr" */
    }

    /* <link> wraps the pushes; its </link> must NOT pop the style stack. Balanced, well-formed. */
    const char *m = "<color=accent><link=q1><scale=2><font=hdr>inner</font></scale></link></color>tail";
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, m, strlen(m));

    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "mixed-tag markup produced the trailing-text run");
    const nt_ui_rich_style_t last = nt_ui_rich_test_run_style(s_fx.ctx, runs - 1U);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF112233U, last.color_abgr, "trailing text carries the BASE color (style stack synced past <link>)");
    const float scale_err = (last.scale > 1.0F) ? (last.scale - 1.0F) : (1.0F - last.scale);
    TEST_ASSERT_TRUE_MESSAGE(scale_err < 1e-3F, "trailing text carries the BASE scale (no leaked <scale> push)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(7U, last.font_id[0].id, "trailing text carries the BASE font (no leaked <font> push)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, last.variant, "trailing text variant back at base");
}

/* (10e) named-tag MISS: a tagset that knows OTHER names but not the requested one. An unknown
 * <font=typo>/<color=xyz>/<fx=bad> is UNTRUSTED data -> log + skip (no style push), and the matching
 * close (pop-iff-pushed) must NOT pop the enclosing style: <b><font=typo>x</font>y</b> keeps both x
 * AND y bold. No trap. This is the headline graceful-skip case from the objective. */
static void parse_named_miss(const char *m, nt_ui_rich_style_t *base) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts); /* valid tagset; registers names DISTINCT from the misses below */
    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFFAABBCCU);
    const nt_font_t hdr[4] = {{.id = 21}, {.id = 22}, {.id = 23}, {.id = 24}};
    nt_ui_rich_tagset_register_font(&ts, "hdr", hdr);
    nt_ui_rich_tagset_register_effect(&ts, "wave", NT_UI_RICH_FX_ID_WAVE);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, &ts, base, m, strlen(m));
}
static void test_parse_unknown_named_tags_graceful(void) {
    /* <b><font=typo>x</font>y</b>: the unknown <font=typo> pushes nothing, its </font> pops nothing,
     * so BOTH x and y stay bold (the enclosing <b> is preserved). */
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    parse_named_miss("<b><font=typo>x</font>y</b>", &base); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "unknown font miss -> runs, no trap");
    for (uint32_t i = 0; i < runs; i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_UI_RICH_VARIANT_BOLD, nt_ui_rich_test_run_style(s_fx.ctx, i).variant, "all text inside <b> stays bold (unknown <font> pushed/popped nothing)");
    }

    /* Unknown color/fx misses likewise skip without trapping; the text is preserved unstyled. */
    nt_ui_rich_style_t base2 = nt_ui_rich_style_defaults();
    base2.color_abgr = 0xFF0A0B0CU;
    parse_named_miss("<color=xyz>x</color>", &base2); /* no trap */
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF0A0B0CU, nt_ui_rich_test_run_style(s_fx.ctx, 0).color_abgr, "unknown color miss keeps the base color");

    nt_ui_rich_style_t base3 = nt_ui_rich_style_defaults();
    parse_named_miss("<fx=bad>x</fx>", &base3); /* no trap */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, nt_ui_rich_test_run_style(s_fx.ctx, 0).effect_id, "unknown fx miss keeps no effect");
}

/* (10f) pop-iff-pushed correctness (#1): a WELL-FORMED <b><i>x</i>y</b> still composes byte-identically
 * -- every real push pops, the trailing 'y' is bold-not-italic, the final 'z' is back at base. Proves
 * the pushed-flag refactor did not break well-formed nesting (no over-pop, no leaked push). */
static void test_parse_wellformed_nesting_pops_correctly(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    /* x: bold+italic ; y: bold (italic popped) ; z: base (bold popped). */
    const char *m = "<b><i>x</i>y</b>z";
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));

    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3U, runs, "<b><i>x</i>y</b>z -> three distinct-style runs");
    const uint8_t bold_italic = (uint8_t)(NT_UI_RICH_VARIANT_BOLD | NT_UI_RICH_VARIANT_ITALIC);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(bold_italic, nt_ui_rich_test_run_style(s_fx.ctx, 0).variant, "x is bold+italic");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_UI_RICH_VARIANT_BOLD, nt_ui_rich_test_run_style(s_fx.ctx, 1).variant, "y is bold only (</i> popped italic)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, nt_ui_rich_test_run_style(s_fx.ctx, 2).variant, "z is base (</b> popped bold)");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF112233U, nt_ui_rich_test_run_style(s_fx.ctx, 2).color_abgr, "z carries the BASE color (stack balanced back to base)");
}

/* (17b) markup-path bad image scale: <img=heart scale=0/> -> graceful: rich_parse_img validates
 * scale>0 and degrades to identity 1.0 BEFORE the builder, so the solver never multiplies the box by
 * 0. The image is present with scale clamped to 1.0. No trap. */
static void test_parse_img_zero_scale_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<img=heart scale=0/>", 20U); /* no trap */
    uint32_t scale_bits = 0;
    uint32_t one_bits = 0;
    const float one = 1.0F;
    memcpy(&one_bits, &one, sizeof one);
    TEST_ASSERT_TRUE_MESSAGE(first_image_scale(&scale_bits), "<img scale=0/> still emits an IMAGE run");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(one_bits, scale_bits, "<img scale=0/> degrades to scale 1.0 (never 0)");
}

/* (16b) <scale=0> / <scale=-1>: the SCALE path validates >0 and degrades to identity 1.0 before the
 * builder (no <=0 font size into nt_font_measure_n). No trap. */
static void test_parse_scale_nonpositive_graceful(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.scale = 1.0F;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<scale=0>x</scale>y", 19U); /* no trap */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_style(s_fx.ctx, 0).scale > 0.0F, "<scale=0> degrades to a positive scale (never 0)");

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<scale=-1>x</scale>y", 20U); /* no trap */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_style(s_fx.ctx, 0).scale > 0.0F, "<scale=-1> degrades to a positive scale (never negative)");
}

/* (10b) nested <link>: a single <link> around text parses fine; a second <link> opened inside the
 * first is UNTRUSTED data -> log + skip the inner link (HTML's no-nested-anchor rule), no trap. The
 * text is preserved; only the inner link is dropped. */
static void test_parse_nested_link_graceful(void) {
    /* One link parses fine. */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, "<link=a>hi</link>", 17U);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) == 1U, "single <link> around text -> one run");

    /* A nested <link> inside an open <link> -> log + skip, no trap, text preserved. */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, "<link=a><link=b>hi</link></link>", 32U); /* no trap */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) >= 1U, "nested <link> skipped gracefully, text preserved");
}

/* ---- bounded-scan robustness ---- */

/* (11) a pathological non-terminating '<' string logs + stops at the unterminated tag and does NOT
 * loop forever or read past `len` -- the test completing proves the scan is bounded. No trap. */
static void test_parse_non_terminating_bounded(void) {
    char buf[64];
    memset(buf, '<', sizeof buf); /* "<<<<...<" -- no '>' anywhere */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, sizeof buf); /* no trap, no loop, no OOB */
    TEST_ASSERT_TRUE_MESSAGE(true, "non-terminating '<' run is bounded (completed without trap/loop)");
}

/* (12) the scan respects `len`: a '<' at the very end with bytes BEYOND len must not be read. A len
 * that stops before any '>' -> the tokenizer logs + stops on the bounded buffer, never scanning into
 * the trailing (out-of-range) bytes. No trap. */
static void test_parse_len_bounded(void) {
    /* The full buffer is well-formed, but we lie about the length: only "<b" is in-bounds. */
    const char *full = "<b>ok</b>";
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, full, 2U); /* only "<b" visible -> unterminated, bounded */
    TEST_ASSERT_TRUE_MESSAGE(true, "len-bounded scan stopped at the unterminated tag (no OOB, no trap)");
}

/* (13) a literal-< escape (\<) emits a real '<' in the text, not a tag start. */
static void test_parse_escape_literal_lt(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "a\\<b"; /* a, literal '<', b */
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, m, strlen(m));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_run_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_rich_test_run_text_len(s_fx.ctx, 0));
    const char *t = nt_ui_rich_test_run_text(s_fx.ctx, 0);
    TEST_ASSERT_EQUAL_MEMORY("a<b", t, 3);
}

/* (14) invalid UTF-8 in a literal text segment -> graceful: log + SKIP the malformed segment (never
 * append the raw invalid bytes downstream). No trap. The whole single segment is dropped, so no run
 * carries the bad bytes. */
static void test_parse_invalid_utf8_graceful(void) {
    const char bad[] = {'h', 'i', (char)0xFF, 'x'}; /* 0xFF is never valid UTF-8 */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, bad, sizeof bad); /* no trap */
    /* The single text segment is invalid UTF-8 -> skipped entirely; no run contains the 0xFF byte. */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_TEXT) {
            const char *t = nt_ui_rich_test_run_text(s_fx.ctx, i);
            const uint32_t tl = nt_ui_rich_test_run_text_len(s_fx.ctx, i);
            for (uint32_t k = 0; k < tl; k++) {
                TEST_ASSERT_NOT_EQUAL_MESSAGE((char)0xFF, t[k], "no emitted run carries the invalid 0xFF byte (segment skipped)");
            }
        }
    }
}

/* (14b) a buffer-full truncation that lands MID multibyte sequence must not sever a codepoint:
 * rich_parse_append_byte trims the partial trailing UTF-8 so rich_flush_text's validator never sees
 * a lone lead/continuation byte (which would TRAP in FULL on otherwise-valid input). Fill the text
 * buffer so a 2-byte 'é' (0xC3 0xA9) straddles the cap: the lead lands at the last slot, the
 * continuation is dropped, and the trim rolls the lone lead back -> kept text is valid UTF-8. */
static void test_parse_utf8_truncation_codepoint_aware(void) {
    static char buf[NT_UI_RICH_MAX_TEXT_BYTES + 64];
    uint32_t n = 0;
    for (uint32_t i = 0; i < NT_UI_RICH_MAX_TEXT_BYTES - 1U; i++) {
        buf[n++] = 'a'; /* fills text_len to cap-1 */
    }
    buf[n++] = (char)0xC3; /* 2-byte lead -> lands at the last slot (text_len -> cap, buffer now full) */
    buf[n++] = (char)0xA9; /* continuation -> dropped; the trim must roll the lone lead back too */
    buf[n++] = (char)0xC3; /* a SECOND straddling sequence past the cap -> entirely dropped */
    buf[n++] = (char)0xA9;
    buf[n++] = 'z'; /* trailing ASCII past the cap -> dropped */

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    /* No NT_TEST_EXPECT_ASSERT: a trap here is a FAILURE (the whole point is no trap on valid input). */
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, n);

    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, runs, "one text run from the truncated literal");
    const uint32_t kept = nt_ui_rich_test_run_text_len(s_fx.ctx, 0);
    /* Kept text stops at the last COMPLETE codepoint: the cap-1 'a' bytes, lone lead trimmed away. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_UI_RICH_MAX_TEXT_BYTES - 1U, kept, "partial trailing UTF-8 lead trimmed (no severed codepoint)");
    /* Re-validate the kept bytes are well-formed UTF-8 (no lone lead/continuation survived). */
    const char *t = nt_ui_rich_test_run_text(s_fx.ctx, 0);
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t cp = 0;
    for (uint32_t i = 0; i < kept; i++) {
        nt_utf8_decode(&state, &cp, (uint8_t)t[i]);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_UTF8_ACCEPT, state, "kept truncated text is valid UTF-8 (ends on a codepoint boundary)");
}

/* (14c) the SAME cap-straddle trim, but the truncated trailing sequence is 3-byte (0xE2 0x80 0x99 ’)
 * and 4-byte (0xF0 0x9F 0x98 0x80 😀). Exercises rich_utf8_seq_len's 3/4-byte branches and
 * rich_utf8_complete_prefix's multi-continuation drop: a lead at the cap whose continuations spill
 * past the buffer must roll the WHOLE partial sequence back, so the kept prefix decodes clean. */
static void utf8_truncation_lead_case(const char *seq, uint32_t seq_len) {
    static char buf[NT_UI_RICH_MAX_TEXT_BYTES + 64];
    uint32_t n = 0;
    for (uint32_t i = 0; i < NT_UI_RICH_MAX_TEXT_BYTES - 1U; i++) {
        buf[n++] = 'a'; /* fills text_len to cap-1; the lead below lands at the last slot */
    }
    for (uint32_t i = 0; i < seq_len; i++) {
        buf[n++] = seq[i]; /* lead at cap, continuations spill past -> whole sequence must roll back */
    }
    buf[n++] = 'z'; /* trailing ASCII past the cap -> dropped */

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, n); /* a trap here is a FAILURE (valid input) */

    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, runs, "one text run from the truncated literal");
    const uint32_t kept = nt_ui_rich_test_run_text_len(s_fx.ctx, 0);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_UI_RICH_MAX_TEXT_BYTES - 1U, kept, "partial multibyte lead trimmed whole (no severed codepoint)");
    const char *t = nt_ui_rich_test_run_text(s_fx.ctx, 0);
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t cp = 0;
    for (uint32_t i = 0; i < kept; i++) {
        nt_utf8_decode(&state, &cp, (uint8_t)t[i]);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_UTF8_ACCEPT, state, "kept truncated text is valid UTF-8 (ends on a codepoint boundary)");
}

static void test_parse_utf8_truncation_3byte_lead(void) {
    const char seq[3] = {(char)0xE2, (char)0x80, (char)0x99}; /* ’ U+2019 */
    utf8_truncation_lead_case(seq, 3U);
}

static void test_parse_utf8_truncation_4byte_lead(void) {
    const char seq[4] = {(char)0xF0, (char)0x9F, (char)0x98, (char)0x80}; /* 😀 U+1F600 */
    utf8_truncation_lead_case(seq, 4U);
}

/* ---- fail-early domain-input asserts (rich API) ---- */

/* Open a fresh rich builder session (no frame needed; the builder is scratch-only). */
static void rich_session_begin(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
}

/* (15) <img=.../> with a NULL base (no default_atlas source) is UNTRUSTED data (begin allows NULL
 * base for text-only markup) -> graceful: log + skip the image; the surrounding text "a"/"b" remain.
 * No trap, no IMAGE run. */
static void test_parse_img_null_base_graceful(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "a<img=icon/>b";
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, m, strlen(m)); /* no trap */
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    bool found_image = false;
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_IMAGE) {
            found_image = true;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(found_image, "<img> with NULL base produced NO image run (skipped); surrounding text remains");
    TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "surrounding text 'a'/'b' still produced run(s)");
}

/* (16) push_scale with a non-finite-or-non-positive multiplier -> NT_ASSERT (negative font size guard). */
static void test_push_scale_bad_mult_asserts(void) {
    rich_session_begin();
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_push_scale(s_fx.ctx, 0.0F));
    rich_session_begin();
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_push_scale(s_fx.ctx, -1.5F));
}

/* (17) rich_image with a non-positive scale -> NT_ASSERT (negative Clay fixed-size guard). */
static void test_rich_image_bad_scale_asserts(void) {
    rich_session_begin();
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, 0xDEADBEEFULL);
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 0.0F));
    rich_session_begin();
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, -2.0F));
}

/* (18) the public widget + markup entries with id==0 -> NT_ASSERT (id drives bbox/link origin). */
static void test_rich_text_zero_id_asserts(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, "x", 1);
    nt_ui_rich_end(s_fx.ctx);
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_text(s_fx.ctx, 0U, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL));
    nt_ui_end(s_fx.ctx);

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_text_markup(s_fx.ctx, 0U, NULL, NULL, &base, "x", 1U, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL));
    nt_ui_end(s_fx.ctx);
}

/* (layer) parser <layer=N> produces a run-list BYTE-IDENTICAL to the builder push_layer(N): same run
 * count, same per-run kind, and the same composed style.layer on every run. A <layer=5> wrap around mixed
 * text must mint the same intervening runs the builder does (the parser drives the SAME builder). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- two authoring fronts built then compared run-by-run
static void test_parse_layer_matches_builder(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U;
    const nt_font_t base_face = {.id = 7};
    for (uint32_t i = 0; i < 4U; i++) {
        base.font_id[i] = base_face;
    }

    /* BUILDER: "a " then <layer=5>"b "</layer> then "c". */
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, "a ", 2);
    nt_ui_rich_push_layer(s_fx.ctx, 5U);
    nt_ui_rich_text_n(s_fx.ctx, "b ", 2);
    nt_ui_rich_pop(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "c", 1);
    nt_ui_rich_end(s_fx.ctx);

    const uint32_t b_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3U, b_runs, "builder: a | <layer=5>b | c -> 3 runs");
    uint8_t b_layer[8];
    nt_rich_atom_kind_t b_kind[8];
    for (uint32_t i = 0; i < b_runs; i++) {
        b_layer[i] = nt_ui_rich_test_run_style(s_fx.ctx, i).layer;
        b_kind[i] = nt_ui_rich_test_run_kind(s_fx.ctx, i);
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_UI_RICH_LAYER_AUTO, b_layer[0], "run 0 outside <layer> is AUTO");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5U, b_layer[1], "run 1 inside <layer=5> is 5");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_UI_RICH_LAYER_AUTO, b_layer[2], "run 2 after </layer> is AUTO again");

    /* PARSER: byte-identical markup of the same builder sequence. */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "a <layer=5>b </layer>c";
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));

    const uint32_t p_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b_runs, p_runs, "parser run count == builder run count");
    for (uint32_t i = 0; i < p_runs; i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(b_kind[i], nt_ui_rich_test_run_kind(s_fx.ctx, i), "parser run kind == builder");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(b_layer[i], nt_ui_rich_test_run_style(s_fx.ctx, i).layer, "parser run layer == builder (byte-identity)");
    }
}

/* (layer) <layer=N> malformed/out-of-range -> graceful: log + AUTO (per-kind default), never a trap.
 * 255 (the AUTO sentinel), >254, empty, and non-numeric all degrade to AUTO; the text is preserved. */
static void test_parse_layer_out_of_range_graceful(void) {
    const char *cases[] = {"<layer=255>x</layer>", "<layer=300>x</layer>", "<layer=>x</layer>", "<layer=ab>x</layer>"};
    for (uint32_t c = 0; c < 4U; c++) {
        nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
        nt_mem_scratch_reset();
        s_fx.ctx->pending_rich = NULL;
        s_fx.ctx->rich_session_open = false;
        nt_ui_rich_parse(s_fx.ctx, NULL, &base, cases[c], strlen(cases[c])); /* no trap */
        const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
        TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "malformed <layer=N> -> runs, no trap");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_UI_RICH_LAYER_AUTO, nt_ui_rich_test_run_style(s_fx.ctx, 0).layer, "malformed <layer=N> degrades to AUTO");
    }
}

/* ---- log mechanism: nt_log_warn_unique dedup ---- */

/* (log-1) a malformed tag LOGS exactly ONE warn line; parsing the SAME bad string a SECOND time logs
 * NOTHING new (nt_log_warn_unique dedups by the produced message, program-wide). Uses a bad-hex color
 * UNIQUE to this process: the OPEN logs once (malformed hex) and the matching </color> is a KNOWN tag
 * that balances the (opaque-white) push silently -> exactly one warn line, not two. */
static void test_log_malformed_logs_once_per_unique(void) {
    sink_attach();
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();

    /* First sight of the bad-hex "#zqzqzq" -> exactly one warn line. */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<color=#zqzqzq>x</color>", 24U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_sink.warn_count, "first malformed parse logs exactly one warn line");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_sink.last_msg, "zqzqzq"), "the warn line carries the offending token");

    /* Re-parse the IDENTICAL bad string -> the unique dedup swallows it (still 1 total). */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<color=#zqzqzq>x</color>", 24U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_sink.warn_count, "re-parsing the same bad string logs nothing new (dedup by message)");
    sink_detach();
}

/* (log-2) two DISTINCT bad values each log ONCE -> two warn lines (the dedup keys on the MESSAGE,
 * which carries the offending token, so different tokens are different messages). Two process-unique
 * bad-hex strings so neither was logged by an earlier test, and each </color> balances silently. */
static void test_log_distinct_errors_each_log_once(void) {
    sink_attach();
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<color=#wqwqwq>x</color>", 24U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_sink.warn_count, "first distinct bad value -> one line");

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<color=#vqvqvq>x</color>", 24U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, s_sink.warn_count, "a SECOND distinct bad value -> a SECOND line (distinct message)");
    sink_detach();
}

/* (log-3) WELL-FORMED markup logs NOTHING and produces the SAME run-list as the equivalent builder
 * sequence (byte-identical happy path: no regression from the log+skip refactor). */
static void test_log_wellformed_silent_and_byte_identical(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U;

    /* BUILDER: "a " then <b>"bold"</b> then " c". */
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, "a ", 2);
    nt_ui_rich_push_bold(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "bold", 4);
    nt_ui_rich_pop(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, " c", 2);
    nt_ui_rich_end(s_fx.ctx);
    const uint32_t b_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    uint8_t b_variant[8] = {0};
    for (uint32_t i = 0; i < b_runs && i < 8U; i++) {
        b_variant[i] = nt_ui_rich_test_run_style(s_fx.ctx, i).variant;
    }

    /* PARSER: the byte-identical markup, with the sink attached to prove ZERO warn lines. */
    sink_attach();
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, "a <b>bold</b> c", 15U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, s_sink.warn_count, "well-formed markup logs NOTHING");
    const uint32_t p_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b_runs, p_runs, "well-formed parser run count == builder (byte-identical)");
    for (uint32_t i = 0; i < p_runs && i < 8U; i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(b_variant[i], nt_ui_rich_test_run_style(s_fx.ctx, i).variant, "well-formed parser run variant == builder");
    }
    sink_detach();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tagset_font_register_lookup);
    RUN_TEST(test_tagset_color_atlas_effect);
    RUN_TEST(test_tagset_custom_effect_fn);
    RUN_TEST(test_tagset_stock_effect_coexists);
    RUN_TEST(test_tagset_override_in_place);
    RUN_TEST(test_tagset_reset);
    RUN_TEST(test_tagset_empty_name_asserts);
    RUN_TEST(test_parse_unclosed_tag_graceful);
    RUN_TEST(test_parse_mismatched_close_graceful);
    RUN_TEST(test_parse_unknown_tag_graceful);
    RUN_TEST(test_parse_bad_hex_graceful);
    RUN_TEST(test_parse_orphan_close_graceful);
    RUN_TEST(test_parse_mismatched_link_close_graceful);
    RUN_TEST(test_parse_empty_scale_value_graceful);
    RUN_TEST(test_parse_fx_params_ok);
    RUN_TEST(test_parse_fx_bad_float_graceful);
    RUN_TEST(test_parse_fx_unknown_key_graceful);
    RUN_TEST(test_parse_fx_no_equals_graceful);
    RUN_TEST(test_parse_fx_params_on_custom_graceful);
    RUN_TEST(test_parse_fx_empty_value_graceful);
    RUN_TEST(test_parse_obj_self_close_emits_object_run);
    RUN_TEST(test_parse_obj_unknown_graceful);
    RUN_TEST(test_parse_obj_empty_name_graceful);
    RUN_TEST(test_parse_obj_null_tagset_graceful);
    RUN_TEST(test_parse_img_alias_null_tagset_graceful);
    RUN_TEST(test_parse_color_null_tagset_graceful);
    RUN_TEST(test_parse_font_null_tagset_graceful);
    RUN_TEST(test_parse_fx_null_tagset_graceful);
    RUN_TEST(test_parse_img_unknown_alias_graceful);
    RUN_TEST(test_parse_img_attrs_match_builder);
    RUN_TEST(test_parse_img_no_attrs_defaults);
    RUN_TEST(test_parse_img_bad_attr_float_graceful);
    RUN_TEST(test_parse_img_bad_valign_graceful);
    RUN_TEST(test_parse_img_unknown_attr_key_graceful);
    RUN_TEST(test_parse_over_deep_style_stack_graceful);
    RUN_TEST(test_parse_balanced_at_cap_stays_synced);
    RUN_TEST(test_parse_mixed_tag_stack_sync);
    RUN_TEST(test_parse_unknown_named_tags_graceful);
    RUN_TEST(test_parse_wellformed_nesting_pops_correctly);
    RUN_TEST(test_parse_img_zero_scale_graceful);
    RUN_TEST(test_parse_scale_nonpositive_graceful);
    RUN_TEST(test_parse_nested_link_graceful);
    RUN_TEST(test_parse_non_terminating_bounded);
    RUN_TEST(test_parse_len_bounded);
    RUN_TEST(test_parse_escape_literal_lt);
    RUN_TEST(test_parse_invalid_utf8_graceful);
    RUN_TEST(test_parse_utf8_truncation_codepoint_aware);
    RUN_TEST(test_parse_utf8_truncation_3byte_lead);
    RUN_TEST(test_parse_utf8_truncation_4byte_lead);
    RUN_TEST(test_parse_img_null_base_graceful);
    RUN_TEST(test_push_scale_bad_mult_asserts);
    RUN_TEST(test_rich_image_bad_scale_asserts);
    RUN_TEST(test_rich_text_zero_id_asserts);
    RUN_TEST(test_parse_layer_matches_builder);
    RUN_TEST(test_parse_layer_out_of_range_graceful);
    RUN_TEST(test_log_malformed_logs_once_per_unique);
    RUN_TEST(test_log_distinct_errors_each_log_once);
    RUN_TEST(test_log_wellformed_silent_and_byte_identical);
    return UNITY_END();
}
