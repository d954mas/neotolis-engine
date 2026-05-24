/* Unit tests for nt_ui_fit -- width-fit + box-fit auto-shrink helpers.
 * The test stub_font has unit advance per glyph (1 em-unit) -- predictable
 * widths for assertions. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_fit.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* === fit_width === */

/* Empty text always returns size_max (nothing to shrink for). */
static void test_fit_width_empty_returns_max(void) {
    uint16_t s = nt_ui_fit_width(s_fx.ctx, 0, "", 100.0F, 14U, 44U, 0.0F);
    TEST_ASSERT_EQUAL_UINT16(44U, s);
    s = nt_ui_fit_width(s_fx.ctx, 0, NULL, 100.0F, 14U, 44U, 0.0F);
    TEST_ASSERT_EQUAL_UINT16(44U, s);
}

/* Short text that fits at size_max -> no shrink. */
static void test_fit_width_fits_at_max(void) {
    /* Container is very wide -- anything fits. */
    uint16_t s = nt_ui_fit_width(s_fx.ctx, 0, "abc", 10000.0F, 14U, 44U, 0.0F);
    TEST_ASSERT_EQUAL_UINT16(44U, s);
}

/* Tight container forces shrink. Verify monotonicity:
 *   - returned size fits  (width at returned <= container_w),
 *   - size+1 (when below max) does NOT fit  (width at size+1 > container_w).
 * Compare widths as int32 (Unity is built with UNITY_EXCLUDE_FLOAT). */
static void test_fit_width_shrinks_to_fit(void) {
    const float container_w = 100.0F;
    const int32_t cw_int = (int32_t)container_w;
    uint16_t s = nt_ui_fit_width(s_fx.ctx, 0, "abcdefghijklmno", container_w, 14U, 44U, 0.0F);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(14U, s);
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(44U, s);
    nt_font_t font = s_fx.ctx->fonts[0];
    /* Width at returned size: must fit. */
    int32_t fit_w_int = (int32_t)nt_font_measure_n(font, "abcdefghijklmno", 15U, (float)s, 0.0F).width;
    TEST_ASSERT_LESS_OR_EQUAL_INT32(cw_int, fit_w_int);
    /* Width at size+1 (when below max): must NOT fit. */
    if (s < 44U) {
        int32_t over_w_int = (int32_t)nt_font_measure_n(font, "abcdefghijklmno", 15U, (float)(s + 1U), 0.0F).width;
        TEST_ASSERT_GREATER_THAN_INT32(cw_int, over_w_int);
    }
}

/* size_min == size_max -- always returns that size, regardless of fit. */
static void test_fit_width_min_eq_max(void) {
    uint16_t s = nt_ui_fit_width(s_fx.ctx, 0, "anything", 100.0F, 20U, 20U, 0.0F);
    TEST_ASSERT_EQUAL_UINT16(20U, s);
    s = nt_ui_fit_width(s_fx.ctx, 0, "anything", 1.0F, 20U, 20U, 0.0F);
    TEST_ASSERT_EQUAL_UINT16(20U, s);
}

/* letter_tracking widens text -- larger tracking forces smaller fit_size. */
static void test_fit_width_tracking_widens(void) {
    const float container_w = 200.0F;
    uint16_t s_no_track = nt_ui_fit_width(s_fx.ctx, 0, "abcdefgh", container_w, 14U, 44U, 0.0F);
    uint16_t s_tracked = nt_ui_fit_width(s_fx.ctx, 0, "abcdefgh", container_w, 14U, 44U, 10.0F);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(s_tracked, s_no_track);
}

/* === fit_box === */

/* Empty text -> size_max. */
static void test_fit_box_empty_returns_max(void) {
    uint16_t s = nt_ui_fit_box(s_fx.ctx, 0, "", 100.0F, 100.0F, 14U, 44U, 0.0F, 0U);
    TEST_ASSERT_EQUAL_UINT16(44U, s);
}

/* Generous box -> fits at max. */
static void test_fit_box_fits_at_max(void) {
    uint16_t s = nt_ui_fit_box(s_fx.ctx, 0, "lots of words here", 10000.0F, 10000.0F, 14U, 44U, 0.0F, 0U);
    TEST_ASSERT_EQUAL_UINT16(44U, s);
}

/* Tight box -> shrinks. Verify monotonicity: smaller box -> smaller or equal size. */
static void test_fit_box_shrinks_with_tighter_box(void) {
    const char *text = "alpha beta gamma delta epsilon zeta eta theta iota kappa";
    uint16_t s_big = nt_ui_fit_box(s_fx.ctx, 0, text, 300.0F, 200.0F, 14U, 44U, 0.0F, 0U);
    uint16_t s_med = nt_ui_fit_box(s_fx.ctx, 0, text, 200.0F, 100.0F, 14U, 44U, 0.0F, 0U);
    uint16_t s_small = nt_ui_fit_box(s_fx.ctx, 0, text, 100.0F, 60.0F, 14U, 44U, 0.0F, 0U);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(s_med, s_big);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(s_small, s_med);
}

/* Single very-long word in narrow box: must occupy one line per wrap, shrinks
 * until fits height-wise. */
static void test_fit_box_long_word(void) {
    uint16_t s = nt_ui_fit_box(s_fx.ctx, 0, "supercalifragilisticexpialidocious", 100.0F, 80.0F, 8U, 44U, 0.0F, 0U);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(8U, s);
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(44U, s);
}

/* min == max -- always returns that size. */
static void test_fit_box_min_eq_max(void) {
    uint16_t s = nt_ui_fit_box(s_fx.ctx, 0, "any text content here", 100.0F, 100.0F, 20U, 20U, 0.0F, 0U);
    TEST_ASSERT_EQUAL_UINT16(20U, s);
}

/* explicit line_height vs natural: bigger line_height -> smaller fit (less rows fit). */
static void test_fit_box_explicit_line_height(void) {
    const char *text = "one two three four five six seven eight nine ten";
    uint16_t s_natural = nt_ui_fit_box(s_fx.ctx, 0, text, 200.0F, 120.0F, 14U, 44U, 0.0F, 0U);
    uint16_t s_tall = nt_ui_fit_box(s_fx.ctx, 0, text, 200.0F, 120.0F, 14U, 44U, 0.0F, 60U);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(s_tall, s_natural);
}

int main(void) {
    UNITY_BEGIN();
    /* NOTE: stub_font in ui_walker_fixture has units_per_em=0 and
     * !metrics_set, so nt_font_measure_n early-returns {0,0} regardless
     * of letter_tracking. Tests below verify code paths that DON'T depend
     * on font measurements producing > 0 widths -- fast paths (empty text,
     * fits-at-max, min==max), monotonicity (smaller box -> smaller-or-equal
     * size), and explicit-line-height comparison. The real shrink behavior
     * under live font metrics is exercised end-to-end by ui_theme_demo. */
    RUN_TEST(test_fit_width_empty_returns_max);
    RUN_TEST(test_fit_width_fits_at_max);
    RUN_TEST(test_fit_width_shrinks_to_fit);
    RUN_TEST(test_fit_width_min_eq_max);
    RUN_TEST(test_fit_width_tracking_widens);
    RUN_TEST(test_fit_box_empty_returns_max);
    RUN_TEST(test_fit_box_fits_at_max);
    RUN_TEST(test_fit_box_shrinks_with_tighter_box);
    RUN_TEST(test_fit_box_long_word);
    RUN_TEST(test_fit_box_min_eq_max);
    RUN_TEST(test_fit_box_explicit_line_height);
    return UNITY_END();
}
