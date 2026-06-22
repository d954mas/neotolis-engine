/* SOLVER (plan-04): full word-wrap + baseline. Plan-03's three spike cases stay
 * green; plan-04 adds break-anywhere overflow, L/C/R alignment, and max-line-height
 * over asymmetric atoms. Deterministic via nt_font_test_set_metrics, no GL. The icon
 * is the 1x1 white region scaled up, so icon_w == scale exactly. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
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

#define FONT_SIZE 20.0F  /* advance = size/2 per char => 10 px/char */
#define ICON_SCALE 16.0F /* white region is 1x1 -> icon box 16x16 px */

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    /* units_per_em=1000, ascent=800, descent=-200: advance = size/2/char. */
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool approx(float a, float b) { return fabsf(a - b) < 1e-4F; }

static nt_atlas_region_ref_t icon_ref(void) { return nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx); }

/* Build [text "HP "][icon][text " full"] with the given icon valign + container. */
static void build_hp_icon_full(nt_rich_valign_t valign) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, "HP ", 3);
    nt_ui_rich_image(s_fx.ctx, icon_ref(), valign, 0.0F, ICON_SCALE);
    nt_ui_rich_text_n(s_fx.ctx, " full", 5);
    nt_ui_rich_end(s_fx.ctx);
}

/* (1) mixed-run x-advance: total line width == measure("HP ") + icon_w + measure(" full"). */
static void test_mixed_run_advance_sums(void) {
    build_hp_icon_full(NT_RICH_VALIGN_BASELINE);
    nt_ui_rich_test_solve(s_fx.ctx, 10000.0F, FONT_SIZE); /* wide: one line */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_line_count(s_fx.ctx));

    const float text1 = nt_font_measure_n(s_fx.stub_font, "HP ", 3, FONT_SIZE, 0.0F).width;
    const float text2 = nt_font_measure_n(s_fx.stub_font, " full", 5, FONT_SIZE, 0.0F).width;
    const float expect = text1 + ICON_SCALE + text2;

    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    float total = 0.0F;
    bool saw_icon = false;
    for (uint32_t i = 0; i < n; i++) {
        nt_ui_rich_test_atom_t a = nt_ui_rich_test_atom(s_fx.ctx, i);
        TEST_ASSERT_EQUAL_UINT32(0U, a.line);
        total += a.w;
        if (a.kind == NT_RICH_ATOM_IMAGE) {
            saw_icon = true;
            TEST_ASSERT_TRUE_MESSAGE(approx(a.w, ICON_SCALE), "icon box width == scale (1x1 region)");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_icon, "icon atom present");
    TEST_ASSERT_TRUE_MESSAGE(approx(total, expect), "sum of atom widths == measure(t1)+icon+measure(t2)");
}

/* Return the icon atom's solved y for a given valign (one wide line). */
static float icon_y_for_valign(nt_rich_valign_t valign) {
    build_hp_icon_full(valign);
    nt_ui_rich_test_solve(s_fx.ctx, 10000.0F, FONT_SIZE);
    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    for (uint32_t i = 0; i < n; i++) {
        nt_ui_rich_test_atom_t a = nt_ui_rich_test_atom(s_fx.ctx, i);
        if (a.kind == NT_RICH_ATOM_IMAGE) {
            return a.y;
        }
    }
    TEST_FAIL_MESSAGE("no icon atom");
    return 0.0F;
}

/* (2) asymmetric-icon baseline: valign=middle vs baseline differ by the expected delta.
 * baseline y = baseline_y - h; middle y = baseline_y - (h/2 + x_height/2);
 * so y_middle - y_baseline = (icon_h - x_height) / 2, x_height = ascent_px*0.5. */
static void test_icon_baseline_vs_middle_delta(void) {
    const float y_baseline = icon_y_for_valign(NT_RICH_VALIGN_BASELINE);

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const float y_middle = icon_y_for_valign(NT_RICH_VALIGN_MIDDLE);

    /* Deterministic line ascent: text ascent dominates (16px) vs icon middle. */
    const float ascent_px = (float)800 * (FONT_SIZE / 1000.0F); /* 16 */
    const float x_height = ascent_px * 0.5F;                    /* 8 */
    const float icon_h = ICON_SCALE;                            /* 16 */
    const float expect_delta = (icon_h - x_height) * 0.5F;

    TEST_ASSERT_FALSE_MESSAGE(approx(y_baseline, y_middle), "middle and baseline place the icon differently");
    TEST_ASSERT_TRUE_MESSAGE(approx(y_middle - y_baseline, expect_delta), "icon y delta == (x_height - icon_h)/2");
}

/* (3) forced wrap: a narrow container fits "HP " + icon but not " full" -> 2 lines,
 * icon stays on line 1. */
static void test_forced_wrap_keeps_icon_on_line1(void) {
    build_hp_icon_full(NT_RICH_VALIGN_BASELINE);
    /* "HP "=30, icon=16 -> 46 fits in 50; " "(10) overflows -> line 2. */
    nt_ui_rich_test_solve(s_fx.ctx, 50.0F, FONT_SIZE);

    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_rich_test_line_count(s_fx.ctx));

    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    bool icon_seen = false;
    for (uint32_t i = 0; i < n; i++) {
        nt_ui_rich_test_atom_t a = nt_ui_rich_test_atom(s_fx.ctx, i);
        if (a.kind == NT_RICH_ATOM_IMAGE) {
            icon_seen = true;
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, a.line, "icon stays on line 1 (index 0)");
        }
    }
    TEST_ASSERT_TRUE(icon_seen);
}

/* ---- plan-04: full solver generalizations ---- */

#define RICH_ID 0x21C4U

/* Build a single text run from one font; helper for the wrap/align cases. */
static void build_text(const char *utf8, size_t len) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, utf8, len);
    nt_ui_rich_end(s_fx.ctx);
}

static void reset_call(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
}

/* (4) break-anywhere: a single word longer than container_w breaks mid-glyph so no
 * atom's x-extent exceeds the box. "AAAAAAAA" = 8 chars * 10px = 80px in a 30px box. */
static void test_break_anywhere_no_atom_exceeds_box(void) {
    const float container_w = 30.0F; /* fits 3 chars (30px) per line */
    build_text("AAAAAAAA", 8);
    nt_ui_rich_test_solve_ex(s_fx.ctx, RICH_ID, container_w, FONT_SIZE, NT_RICH_ALIGN_LEFT);

    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(n > 1U, "over-long word split into multiple atoms");
    for (uint32_t i = 0; i < n; i++) {
        nt_ui_rich_test_atom_t a = nt_ui_rich_test_atom(s_fx.ctx, i);
        TEST_ASSERT_TRUE_MESSAGE(a.x + a.w <= container_w + 1e-3F, "no atom x-extent exceeds container_w");
    }
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_line_count(s_fx.ctx) >= 3U, "80px over 30px box wraps to >=3 lines");
}

/* Solve a two-word text "AAA BBB" at the given align and return line 0's leftmost x. */
static float line0_left_x(nt_rich_align_t align, float container_w) {
    build_text("AAA BBB", 7);
    nt_ui_rich_test_solve_ex(s_fx.ctx, RICH_ID, container_w, FONT_SIZE, align);
    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    float min_x = 1e9F;
    for (uint32_t i = 0; i < n; i++) {
        nt_ui_rich_test_atom_t a = nt_ui_rich_test_atom(s_fx.ctx, i);
        if (a.line == 0U && a.x < min_x) {
            min_x = a.x;
        }
    }
    return min_x;
}

/* (5) L/C/R align offsets: a line narrower than the box shifts by the align formula.
 * "AAA BBB" = two word atoms "AAA "(40px) + "BBB"(30px) = 70px line in a 200px box. */
static void test_align_lcr_offsets(void) {
    const float container_w = 200.0F;
    const float line_w = nt_font_measure_n(s_fx.stub_font, "AAA ", 4, FONT_SIZE, 0.0F).width + nt_font_measure_n(s_fx.stub_font, "BBB", 3, FONT_SIZE, 0.0F).width;

    const float left_x = line0_left_x(NT_RICH_ALIGN_LEFT, container_w);
    TEST_ASSERT_TRUE_MESSAGE(approx(left_x, 0.0F), "LEFT: line starts at 0");

    reset_call();
    const float center_x = line0_left_x(NT_RICH_ALIGN_CENTER, container_w);
    TEST_ASSERT_TRUE_MESSAGE(approx(center_x, (container_w - line_w) * 0.5F), "CENTER: line offset == (box - line)/2");

    reset_call();
    const float right_x = line0_left_x(NT_RICH_ALIGN_RIGHT, container_w);
    TEST_ASSERT_TRUE_MESSAGE(approx(right_x, container_w - line_w), "RIGHT: line offset == box - line");
}

/* (6) max-line-height: a tall icon on a line of small text grows the line's height to
 * the icon box. line_h == max over atoms; the icon (16px) > text line-box drives it. */
static void test_max_line_height_over_atoms(void) {
    /* Tiny text (size 4 -> ascent 3.2 + descent 0.8 = 4px line) + a 16px icon on one line. */
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_image(s_fx.ctx, icon_ref(), NT_RICH_VALIGN_TOP, 0.0F, ICON_SCALE);
    nt_ui_rich_text_n(s_fx.ctx, "x", 1);
    nt_ui_rich_end(s_fx.ctx);
    nt_ui_rich_test_solve_ex(s_fx.ctx, RICH_ID, 10000.0F, 4.0F, NT_RICH_ALIGN_LEFT);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_line_count(s_fx.ctx));
    /* The solved total height must be at least the icon box (16px), not the 4px text. */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_total_h(s_fx.ctx) >= ICON_SCALE - 1e-3F, "line height grows to the tall icon (max over atoms)");
}

/* (7) total width == container_w when explicit; the Clay FIXED block hosts it. */
static void test_total_width_is_container(void) {
    build_text("AAA BBB", 7);
    nt_ui_rich_test_solve_ex(s_fx.ctx, RICH_ID, 200.0F, FONT_SIZE, NT_RICH_ALIGN_LEFT);
    TEST_ASSERT_TRUE_MESSAGE(approx(nt_ui_rich_test_total_w(s_fx.ctx), 200.0F), "explicit container_w drives the FIXED block width");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mixed_run_advance_sums);
    RUN_TEST(test_icon_baseline_vs_middle_delta);
    RUN_TEST(test_forced_wrap_keeps_icon_on_line1);
    RUN_TEST(test_break_anywhere_no_atom_exceeds_box);
    RUN_TEST(test_align_lcr_offsets);
    RUN_TEST(test_max_line_height_over_atoms);
    RUN_TEST(test_total_width_is_container);
    return UNITY_END();
}
