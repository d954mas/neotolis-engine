#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/ui_walker_fixture.h"
#include "time/nt_time.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
alignas(NT_UI_ARENA_ALIGN) static uint8_t s_other_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;
static nt_ui_context_t *s_other_ctx;
static uint32_t s_clock_reads;
static double s_clock_seconds;
static double s_clock_step;
static const nt_ui_target_t s_target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};

/* Exact binary fractions keep timing assertions independent of wall-clock noise. */
double nt_time_now(void) {
    ++s_clock_reads;
    s_clock_seconds += s_clock_step;
    return s_clock_seconds;
}

void setUp(void) {
    nt_test_assert_install();
    s_clock_step = 0.125;
    s_clock_seconds = 0.0;
    s_other_ctx = NULL;
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    s_clock_reads = 0U;
    nt_gfx_fake_draw_trace_reset(true);
}

void tearDown(void) {
    if (s_other_ctx != NULL) {
        nt_ui_destroy_context(s_other_ctx);
    }
    ui_walker_fixture_shutdown(&s_fx);
}

static void build_rect(nt_ui_context_t *ctx) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("timed_rect"), .layout = {.sizing = {CLAY_SIZING_FIXED(97.0F), CLAY_SIZING_FIXED(43.0F)}}, .backgroundColor = {255.0F, 64.0F, 32.0F, 255.0F}}) {}
    s_clock_reads = 0U;
    nt_ui_end(ctx);
}

static void assert_timings(const nt_ui_context_t *ctx, int32_t end_ms, int32_t walk_ms) {
#if !NT_UI_TIMING_ENABLED
    end_ms = 0;
    walk_ms = 0;
#endif
    TEST_ASSERT_TRUE(nt_ui_get_last_layout_ms(ctx) == (float)end_ms);
    TEST_ASSERT_TRUE(nt_ui_get_last_build_tree_ms(ctx) == (float)end_ms);
    TEST_ASSERT_TRUE(nt_ui_get_last_walk_ms(ctx) == (float)walk_ms);
}

static void test_end_and_main_walk_collect_only_selected_timing(void) {
    build_rect(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &s_target);

#if NT_UI_TIMING_ENABLED
    TEST_ASSERT_EQUAL_UINT32(6U, s_clock_reads);
#else
    TEST_ASSERT_EQUAL_UINT32(0U, s_clock_reads);
#endif
    assert_timings(s_fx.ctx, 125, 125);
    const nt_ui_bbox_t bbox = nt_ui_get_bbox(s_fx.ctx, nt_ui_id("timed_rect"));
    TEST_ASSERT_TRUE(bbox.found);
    TEST_ASSERT_EQUAL_INT32(97, (int32_t)bbox.width);
    TEST_ASSERT_EQUAL_INT32(43, (int32_t)bbox.height);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(6U, nt_gfx_fake_draw_trace_at(0U).num_indices);
}

static void test_repeated_frame_replaces_timing(void) {
    build_rect(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &s_target);
    assert_timings(s_fx.ctx, 125, 125);

    s_clock_step = 0.25;
    build_rect(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &s_target);
    assert_timings(s_fx.ctx, 250, 250);
#if NT_UI_TIMING_ENABLED
    TEST_ASSERT_EQUAL_UINT32(6U, s_clock_reads);
#else
    TEST_ASSERT_EQUAL_UINT32(0U, s_clock_reads);
#endif
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_fake_draw_trace_count());
}

static void test_degenerate_main_walk_resets_only_walk_timing(void) {
    build_rect(s_fx.ctx);
    const nt_ui_target_t targets[] = {
        {.viewport = {0.0F, 0.0F, 0.0F, 600.0F}},
        {.viewport = {0.0F, 0.0F, 800.0F, 0.0F}},
        {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}, .fb_size = {800.0F, 0.0F}},
    };
    for (size_t i = 0U; i < sizeof targets / sizeof targets[0]; ++i) {
        nt_ui_walk(s_fx.ctx, &s_target);
        assert_timings(s_fx.ctx, 125, 125);
        s_clock_reads = 0U;
        nt_gfx_fake_draw_trace_reset(true);
        nt_ui_walk(s_fx.ctx, &targets[i]);
        TEST_ASSERT_EQUAL_UINT32(0U, s_clock_reads);
        TEST_ASSERT_EQUAL_UINT32(0U, nt_gfx_fake_draw_trace_count());
        TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_command_count(s_fx.ctx));
        TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
        assert_timings(s_fx.ctx, 125, 0);
    }
}

static void test_unbound_atlas_resets_only_walk_timing(void) {
    build_rect(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &s_target);
    assert_timings(s_fx.ctx, 125, 125);
    s_fx.ctx->atlas = (nt_resource_t){0};
    s_clock_reads = 0U;
    nt_gfx_fake_draw_trace_reset(true);

    nt_ui_walk(s_fx.ctx, &s_target);

    TEST_ASSERT_EQUAL_UINT32(0U, s_clock_reads);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
    assert_timings(s_fx.ctx, 125, 0);
}

#if NT_UI_DEBUG_TOOLS
static void test_inspector_walk_preserves_main_timing(void) {
    build_rect(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &s_target);
    assert_timings(s_fx.ctx, 125, 125);

    s_clock_step = 0.25;
    s_fx.ctx->frozen_cmds.internalArray[0].userData = (void *)NT_UI_DATA_LAYER(NT_UI_LAYER_DEBUG_PANEL_BG);
    s_clock_reads = 0U;
    nt_gfx_fake_draw_trace_reset(true);
    nt_ui_debug_inspector_walk(s_fx.ctx, &s_target);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_fake_draw_trace_count());
    assert_timings(s_fx.ctx, 125, 125);
#if !NT_UI_TIMING_ENABLED
    TEST_ASSERT_EQUAL_UINT32(0U, s_clock_reads);
#endif

    const nt_ui_target_t zero_target = {0};
    s_clock_reads = 0U;
    nt_ui_debug_inspector_walk(s_fx.ctx, &zero_target);
    TEST_ASSERT_EQUAL_UINT32(0U, s_clock_reads);
    assert_timings(s_fx.ctx, 125, 125);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}
#endif

static void test_context_timing_is_independent(void) {
    build_rect(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &s_target);
    const nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    s_other_ctx = nt_ui_create_context(s_other_arena, sizeof s_other_arena, &desc);
    TEST_ASSERT_NOT_NULL(s_other_ctx);
    nt_ui_set_atlas_white_region(s_other_ctx, s_fx.atlas.handle, s_fx.atlas.white_region_idx);
    nt_ui_set_sprite_material(s_other_ctx, s_fx.sprite_material);
    nt_ui_set_text_material(s_other_ctx, s_fx.text_material);
    assert_timings(s_other_ctx, 0, 0);

    s_clock_step = 0.25;
    build_rect(s_other_ctx);
    nt_ui_walk(s_other_ctx, &s_target);
    assert_timings(s_other_ctx, 250, 250);
    assert_timings(s_fx.ctx, 125, 125);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_other_ctx));
#if !NT_UI_TIMING_ENABLED
    TEST_ASSERT_EQUAL_UINT32(0U, s_clock_reads);
#endif
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
static void test_timing_getters_require_context(void) {
    NT_TEST_EXPECT_ASSERT((void)nt_ui_get_last_layout_ms(NULL));
    NT_TEST_EXPECT_ASSERT((void)nt_ui_get_last_build_tree_ms(NULL));
    NT_TEST_EXPECT_ASSERT((void)nt_ui_get_last_walk_ms(NULL));
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_end_and_main_walk_collect_only_selected_timing);
    RUN_TEST(test_repeated_frame_replaces_timing);
    RUN_TEST(test_degenerate_main_walk_resets_only_walk_timing);
    RUN_TEST(test_unbound_atlas_resets_only_walk_timing);
#if NT_UI_DEBUG_TOOLS
    RUN_TEST(test_inspector_walk_preserves_main_timing);
#endif
    RUN_TEST(test_context_timing_is_independent);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_timing_getters_require_context);
#endif
    return UNITY_END();
}
