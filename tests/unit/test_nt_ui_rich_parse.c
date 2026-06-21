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

/* ---- parser malformed-markup death tests (MARK-67-01) ---- */
static void parse_lit(const char *m) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
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

/* (10) over-deep nesting beyond the tag-stack depth cap -> NT_ASSERT before overflow. */
static void test_parse_over_deep_nesting_asserts(void) {
    char buf[256];
    uint32_t n = 0;
    for (uint32_t i = 0; i < 40U; i++) { /* 40 > NT_UI_RICH_PARSE_TAG_DEPTH (32) */
        buf[n++] = '<';
        buf[n++] = 'b';
        buf[n++] = '>';
    }
    buf[n] = '\0';
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, n));
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
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, full, 2U)); /* only "<b" visible -> unterminated */
}

/* (13) a literal-< escape (\<) emits a real '<' in the text, not a tag start. */
static void test_parse_escape_literal_lt(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
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
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, bad, sizeof bad));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tagset_font_register_lookup);
    RUN_TEST(test_tagset_color_atlas_effect);
    RUN_TEST(test_tagset_override_in_place);
    RUN_TEST(test_tagset_reset);
    RUN_TEST(test_parse_unclosed_tag_asserts);
    RUN_TEST(test_parse_mismatched_close_asserts);
    RUN_TEST(test_parse_unknown_tag_asserts);
    RUN_TEST(test_parse_bad_hex_asserts);
    RUN_TEST(test_parse_orphan_close_asserts);
    RUN_TEST(test_parse_over_deep_nesting_asserts);
    RUN_TEST(test_parse_non_terminating_bounded);
    RUN_TEST(test_parse_len_bounded);
    RUN_TEST(test_parse_escape_literal_lt);
    RUN_TEST(test_parse_invalid_utf8_asserts);
    return UNITY_END();
}
