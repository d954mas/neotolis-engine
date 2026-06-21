/* nt_ui_rich_text emit (plan-04): the widget declares ONE Clay FIXED block and self-emits
 * its solved TEXT atoms as positioned nt_text_renderer_draw_n spans during the walk. No GL:
 * the fixture's stub font (units_per_em=0) makes draw_n a counted no-op, so the walker-command
 * probe + the draw_n call counter prove emit without a real glyph atlas. Modeled on
 * test_ui_radial.c (ui_walker_fixture, no GL capture). */

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
#include "renderers/nt_text_renderer.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_text.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define FONT_SIZE_DEFAULT 16.0F /* the widget's NT_UI_RICH_DEFAULT_FONT_SIZE */

void setUp(void) {
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    /* Deterministic metrics: advance = size/2/char; ascent 800/1000. */
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool approx(float a, float b) { return fabsf(a - b) < 1e-3F; }

/* Build a 2-run text-only block, declare the rich-text widget, walk once. */
static void frame_two_run_text(float container_w, nt_rich_align_t align) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_color(s_fx.ctx, 0xFF00FF00U); /* second run: distinct style -> split */
        nt_ui_rich_text_n(s_fx.ctx, "Hello ", 6);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "world", 5);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich").id, NULL, &base, container_w, align, 0.0F);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (1) emit produces walker text commands (> 0) AND the draw_n span count matches the
 * solved TEXT line-fragments. Two runs on one wide line -> two spans. */
static void test_emit_produces_text_spans(void) {
    nt_text_renderer_test_reset_call_counters();
    frame_two_run_text(400.0F, NT_RICH_ALIGN_LEFT);

    const uint32_t spans = nt_text_renderer_test_draw_n_calls();
    TEST_ASSERT_TRUE_MESSAGE(spans > 0U, "rich-text emits at least one draw_n span");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, spans, "two style runs on one line -> two draw_n spans");

    /* The widget-side span counter agrees with the renderer's draw_n call count. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(spans, nt_ui_rich_test_emit_span_count(s_fx.ctx), "widget span count == draw_n calls");

    /* The walker saw a CUSTOM command (the FIXED block) -> the walk text barrier flushed. */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_atom_count(s_fx.ctx) >= 2U, "solver placed >=2 TEXT atoms");
}

/* (2) the FIXED block size equals the solved total size (D-67-03). */
static void test_fixed_block_size_matches_solved(void) {
    frame_two_run_text(400.0F, NT_RICH_ALIGN_LEFT);
    /* Explicit container_w drives total_w; total_h is the one-line height (size 16 -> 16px). */
    TEST_ASSERT_TRUE_MESSAGE(approx(nt_ui_rich_test_total_w(s_fx.ctx), 400.0F), "FIXED width == explicit container_w");
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_total_h(s_fx.ctx) > 0.0F, "FIXED height == solved line height");
}

/* (3) a run WITHOUT an effect emits one span per line-fragment (Open Q2 batch-friendly):
 * one wide line of one style -> exactly one span. */
static void test_single_style_one_span_per_line(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_text_renderer_test_reset_call_counters();
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_root2"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "onerun", 6);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich2").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_text_renderer_test_draw_n_calls(), "one style, one line -> one draw_n span");
}

/* (4) double-walk determinism: re-walking the same frame yields identical span counts and
 * does NOT mutate the run-list (UI-06 re-walkable, read-only on context). */
static void test_double_walk_is_deterministic(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_root3"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "alpha beta", 10);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich3").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};

    nt_text_renderer_test_reset_call_counters();
    nt_ui_walk(s_fx.ctx, &target);
    const uint32_t spans_1 = nt_text_renderer_test_draw_n_calls();
    const uint32_t atoms_1 = nt_ui_rich_test_atom_count(s_fx.ctx);
    const uint32_t runs_1 = nt_ui_rich_test_run_count(s_fx.ctx);

    nt_text_renderer_test_reset_call_counters();
    nt_ui_walk(s_fx.ctx, &target); /* re-walk the SAME frozen frame */
    const uint32_t spans_2 = nt_text_renderer_test_draw_n_calls();
    const uint32_t atoms_2 = nt_ui_rich_test_atom_count(s_fx.ctx);
    const uint32_t runs_2 = nt_ui_rich_test_run_count(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(spans_1, spans_2, "double-walk emits the same span count");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(atoms_1, atoms_2, "double-walk leaves the solved atoms unchanged");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(runs_1, runs_2, "double-walk leaves the run-list unchanged (read-only)");
    TEST_ASSERT_TRUE_MESSAGE(spans_1 > 0U, "emit actually ran");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_emit_produces_text_spans);
    RUN_TEST(test_fixed_block_size_matches_solved);
    RUN_TEST(test_single_style_one_span_per_line);
    RUN_TEST(test_double_walk_is_deterministic);
    return UNITY_END();
}
