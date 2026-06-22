/* Rich-text markup parser + tagset vocabulary. No GL: the parser writes a frame-scratch
 * run-list (read back via the build probes); the tagset is a plain owned struct.
 * Task 1 covers the tagset register/lookup; Tasks 2-3 add parser + malformed death tests. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "hash/nt_hash.h"
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

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

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
 * parsers guard vlen>0) -> fail-early NT_ASSERT in every register_* (M11). */
static void test_tagset_empty_name_asserts(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    const nt_font_t fam[4] = {{.id = 1}, {.id = 2}, {.id = 3}, {.id = 4}};
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_font(&ts, "", fam));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_atlas(&ts, "", (nt_resource_t){.id = 1U}));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_color(&ts, "", 0xFFFFFFFFU));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_effect(&ts, "", 1U));
}

/* ---- parser malformed-markup death tests (MARK-67-01) ---- */
static void parse_lit(const char *m) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, m, strlen(m));
}

/* (5) unclosed tag at end of string -> NT_ASSERT. */
static void test_parse_unclosed_tag_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<b>HP")); }

/* (6) mismatched close (<b>..</i>) -> NT_ASSERT. */
static void test_parse_mismatched_close_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<b>HP</i>")); }

/* (7) unknown CORE-shaped tag -> NT_ASSERT. */
static void test_parse_unknown_tag_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<bogus>x</bogus>")); }

/* (8) malformed hex in <color=#..> -> NT_ASSERT. */
static void test_parse_bad_hex_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<color=#zzz>x</color>")); }

/* (9) a close tag with no matching open -> NT_ASSERT. */
static void test_parse_orphan_close_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("HP</b>")); }

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

/* (8c) a bad float value in an fx param -> NT_ASSERT (fail-early). */
static void test_parse_fx_bad_float_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_fx("<fx=wave amp=8x>hi</fx>")); }

/* (8d) an unknown fx param key -> NT_ASSERT. */
static void test_parse_fx_unknown_key_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_fx("<fx=wave bogus=3>hi</fx>")); }

/* (8e) a param token with no '=' -> NT_ASSERT. */
static void test_parse_fx_no_equals_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_fx("<fx=wave amp>hi</fx>")); }

/* (8f) k=v params on a CUSTOM-fn effect name -> NT_ASSERT (params apply to STOCK effects only). */
static void test_parse_fx_params_on_custom_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_fx("<fx=fade amp=8>hi</fx>")); }

/* (10) over-deep <b> nesting -> NT_ASSERT before overflow. NOTE: <b> pushes the BUILDER style
 * stack (NT_UI_RICH_STACK_DEPTH), which is checked first, so this exercises the BUILDER cap.
 * The parser's own tag-stack cap is covered separately by test_parse_tag_stack_overflow. */
static void test_parse_over_deep_style_stack_asserts(void) {
    char buf[256];
    uint32_t n = 0;
    for (uint32_t i = 0; i < 40U; i++) { /* 40 > NT_UI_RICH_STACK_DEPTH (32) */
        buf[n++] = '<';
        buf[n++] = 'b';
        buf[n++] = '>';
    }
    buf[n] = '\0';
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, n));
}

/* (10b) nested <link> is rejected (HTML's no-nested-anchor rule): pending_link is a single scalar,
 * so an inner </link> would zero the outer's id and silently drop the enclosing link. A SINGLE link
 * around text parses fine; a second link opened inside the first traps in rich_open_tag. */
static void test_parse_nested_link_asserts(void) {
    /* One link parses fine. */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, "<link=a>hi</link>", 17U);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) == 1U, "single <link> around text -> one run");

    /* A nested <link> inside an open <link> -> NT_ASSERT (loud, not silently wrong). */
    NT_TEST_EXPECT_ASSERT(parse_lit("<link=a><link=b>hi</link></link>"));
}

/* ---- bounded-scan robustness (MARK-67-05) ---- */

/* (11) a pathological non-terminating '<' string asserts on the unterminated tag and does NOT
 * loop forever or read past `len` -- the test completing proves the scan is bounded. */
static void test_parse_non_terminating_bounded(void) {
    char buf[64];
    memset(buf, '<', sizeof buf); /* "<<<<...<" -- no '>' anywhere */
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, sizeof buf));
}

/* (12) the scan respects `len`: a '<' at the very end with bytes BEYOND len must not be read.
 * We pass a len that stops before any '>' so the parser asserts on the bounded buffer, never
 * scanning into the trailing (out-of-range) bytes. */
static void test_parse_len_bounded(void) {
    /* The full buffer is well-formed, but we lie about the length: only "<b" is in-bounds. */
    const char *full = "<b>ok</b>";
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, full, 2U)); /* only "<b" visible -> unterminated */
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

/* (14) invalid UTF-8 in a literal text segment -> NT_ASSERT (T-67-06-04). */
static void test_parse_invalid_utf8_asserts(void) {
    const char bad[] = {'h', 'i', (char)0xFF, 'x'}; /* 0xFF is never valid UTF-8 */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, bad, sizeof bad));
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

/* (15) <img=.../> with a NULL base (no default_atlas source) -> NT_ASSERT in the parser. */
static void test_parse_img_null_base_asserts(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "<img=icon/>";
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, m, strlen(m)));
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tagset_font_register_lookup);
    RUN_TEST(test_tagset_color_atlas_effect);
    RUN_TEST(test_tagset_custom_effect_fn);
    RUN_TEST(test_tagset_stock_effect_coexists);
    RUN_TEST(test_tagset_override_in_place);
    RUN_TEST(test_tagset_reset);
    RUN_TEST(test_tagset_empty_name_asserts);
    RUN_TEST(test_parse_unclosed_tag_asserts);
    RUN_TEST(test_parse_mismatched_close_asserts);
    RUN_TEST(test_parse_unknown_tag_asserts);
    RUN_TEST(test_parse_bad_hex_asserts);
    RUN_TEST(test_parse_orphan_close_asserts);
    RUN_TEST(test_parse_fx_params_ok);
    RUN_TEST(test_parse_fx_bad_float_asserts);
    RUN_TEST(test_parse_fx_unknown_key_asserts);
    RUN_TEST(test_parse_fx_no_equals_asserts);
    RUN_TEST(test_parse_fx_params_on_custom_asserts);
    RUN_TEST(test_parse_over_deep_style_stack_asserts);
    RUN_TEST(test_parse_nested_link_asserts);
    RUN_TEST(test_parse_non_terminating_bounded);
    RUN_TEST(test_parse_len_bounded);
    RUN_TEST(test_parse_escape_literal_lt);
    RUN_TEST(test_parse_invalid_utf8_asserts);
    RUN_TEST(test_parse_img_null_base_asserts);
    RUN_TEST(test_push_scale_bad_mult_asserts);
    RUN_TEST(test_rich_image_bad_scale_asserts);
    RUN_TEST(test_rich_text_zero_id_asserts);
    return UNITY_END();
}
