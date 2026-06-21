/* Rich-text run-list build + style-stack composition + bold/italic variant select.
 * No GL: the builder writes a frame-scratch SoA; we read it back via the test probes. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
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

static bool approx(float a, float b) { return fabsf(a - b) < 1e-5F; }

/* A four-member family with distinct ids so variant select is observable. */
static void family(nt_font_t out[4], uint32_t r, uint32_t b, uint32_t i, uint32_t bi) {
    out[0] = (nt_font_t){.id = r};
    out[1] = (nt_font_t){.id = b};
    out[2] = (nt_font_t){.id = i};
    out[3] = (nt_font_t){.id = bi};
}

/* (1) push_scale(1.5) then push_scale(2.0) -> composed scale 3.0 (x multiplies). */
static void test_scale_multiplies(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_scale(s_fx.ctx, 1.5F);
    nt_ui_rich_push_scale(s_fx.ctx, 2.0F);
    nt_ui_rich_text_n(s_fx.ctx, "x", 1);
    nt_ui_rich_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_run_count(s_fx.ctx));
    nt_ui_rich_style_t s = nt_ui_rich_test_run_style(s_fx.ctx, 0);
    TEST_ASSERT_TRUE_MESSAGE(approx(s.scale, 3.0F), "1.5 * 2.0 == 3.0 (multiplicative)");
}

/* (2) innermost color override wins; pop restores the previous. */
static void test_color_override_and_pop(void) {
    const uint32_t color_a = 0xFF112233U;
    const uint32_t color_b = 0xFFAABBCCU;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_color(s_fx.ctx, color_a);
    nt_ui_rich_push_color(s_fx.ctx, color_b);
    nt_ui_rich_text_n(s_fx.ctx, "inner", 5); /* run 0: color B */
    nt_ui_rich_pop(s_fx.ctx);                /* back to color A */
    nt_ui_rich_text_n(s_fx.ctx, "outer", 5); /* run 1: color A */
    nt_ui_rich_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_rich_test_run_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_HEX32(color_b, nt_ui_rich_test_run_style(s_fx.ctx, 0).color_abgr);
    TEST_ASSERT_EQUAL_HEX32(color_a, nt_ui_rich_test_run_style(s_fx.ctx, 1).color_abgr);
}

/* (3a) bold selects font_id[1]; bold+italic selects font_id[3]. */
static void test_variant_selects_family_member(void) {
    nt_font_t fam[4];
    family(fam, 10, 11, 12, 13);
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_font(s_fx.ctx, fam);

    nt_ui_rich_push_bold(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "B", 1); /* run 0: bold -> font_id[1] */
    nt_ui_rich_push_italic(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "BI", 2); /* run 1: bold+italic -> font_id[3] */
    nt_ui_rich_pop(s_fx.ctx);             /* drop italic */
    nt_ui_rich_pop(s_fx.ctx);             /* drop bold */
    nt_ui_rich_text_n(s_fx.ctx, "R", 1);  /* run 2: regular -> font_id[0] */
    nt_ui_rich_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_rich_test_run_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(11U, nt_ui_rich_test_run_font(s_fx.ctx, 0).id);
    TEST_ASSERT_EQUAL_UINT32(13U, nt_ui_rich_test_run_font(s_fx.ctx, 1).id);
    TEST_ASSERT_EQUAL_UINT32(10U, nt_ui_rich_test_run_font(s_fx.ctx, 2).id);
}

/* (3b) a family MISSING the bold-italic member falls back B->R, and italic with no
 * italic member raises the synthetic-shear flag. */
static void test_variant_fallback_and_synth_italic(void) {
    nt_font_t fam[4];
    family(fam, 10, 11, 0, 0); /* only R + B; no italic, no bold-italic */
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_font(s_fx.ctx, fam);

    nt_ui_rich_push_bold(s_fx.ctx);
    nt_ui_rich_push_italic(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "BI", 2); /* BI missing -> fall back to B (11), shear flagged */
    nt_ui_rich_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_run_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(11U, nt_ui_rich_test_run_font(s_fx.ctx, 0).id); /* B fallback */
    TEST_ASSERT_TRUE_MESSAGE((nt_ui_rich_test_run_flags(s_fx.ctx, 0) & NT_UI_RICH_RUN_SYNTH_ITALIC) != 0U, "missing italic member -> synthetic shear flag");
}

/* (4) two adjacent text_n with the SAME composed style share ONE run (dedup); a
 * color change between them starts a NEW run. */
static void test_dedup_and_split_on_style_change(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, "ab", 2);
    nt_ui_rich_text_n(s_fx.ctx, "cd", 2); /* same style -> extends run 0 */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_run_count(s_fx.ctx));

    nt_ui_rich_push_color(s_fx.ctx, 0xFF00FF00U);
    nt_ui_rich_text_n(s_fx.ctx, "ef", 2); /* color change -> new run */
    nt_ui_rich_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_rich_test_run_count(s_fx.ctx));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_scale_multiplies);
    RUN_TEST(test_color_override_and_pop);
    RUN_TEST(test_variant_selects_family_member);
    RUN_TEST(test_variant_fallback_and_synth_italic);
    RUN_TEST(test_dedup_and_split_on_style_change);
    return UNITY_END();
}
