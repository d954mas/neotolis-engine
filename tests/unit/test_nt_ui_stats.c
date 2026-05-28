/* Bridge test: nt_ui_get_last_walk_* -> nt_stats. nt_ui has no nt_stats dep. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "stats/nt_stats.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 16
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

void setUp(void) {
    nt_test_assert_install();
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    /* nt_stats is not init'd by the fixture (UI doesn't depend on it).
     * This test exercises the metrics-bridge pattern so we init it here. */
    nt_stats_init(NULL);
}

void tearDown(void) {
    nt_stats_shutdown();
    ui_walker_fixture_shutdown(&s_fx);
}

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* Canonical metrics-bridge pattern: walk -> read getter -> publish into
 * nt_stats. After this, nt_stats_format_lines must show the value. */
static void publish_ui_metrics_to_stats(const nt_ui_context_t *ctx) {
    nt_stats_count("ui_draw_calls", (uint64_t)nt_ui_get_last_walk_draw_calls(ctx));
    nt_stats_count("ui_command_count", (uint64_t)nt_ui_get_last_walk_command_count(ctx));
}

/*after a walk that emits a RECT, the public draw-call
 * getter reports >= 1 (the walker-exit flush issues at least one GL
 * draw call). */
static void test_get_last_walk_draw_calls_after_rect(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 0.0F, .b = 0.0F, .a = 255.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_GREATER_THAN_UINT32(0U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/*element count equals frozen_cmds.length exactly
 * (synthetic injected array, no Clay wrapper elements). */
static void test_get_last_walk_command_count_matches_frozen_cmds(void) {
    for (int i = 0; i < 3; ++i) {
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = (float)(i * 20), .y = 0.0F, .width = 10.0F, .height = 10.0F};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0.0F, .g = 255.0F, .b = 0.0F, .a = 255.0F};
    }
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_get_last_walk_command_count(s_fx.ctx));
}

/* Bridge pattern: app forwards getter values into nt_stats; both
 * counters appear in nt_stats_format_lines with the expected values. */
static void test_metrics_bridge_publishes_to_nt_stats(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 50.0F, .height = 50.0F};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);
    publish_ui_metrics_to_stats(s_fx.ctx);

    const uint32_t draw_calls = nt_ui_get_last_walk_draw_calls(s_fx.ctx);

    char buf[512];
    const uint32_t n = nt_stats_format_lines(buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, n);

    char expected_draw[64];
    (void)snprintf(expected_draw, sizeof expected_draw, "ui_draw_calls: %u", (unsigned)draw_calls);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, expected_draw), "bridge: ui_draw_calls value in nt_stats must match getter output");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "ui_command_count: 1"), "bridge: ui_command_count must equal frozen_cmds.length");
}

/* counters are SET per walk (not accumulated). Walk twice with
 * different command counts -- second getter call reflects second walk. */
static void test_getters_reflect_latest_walk_only(void) {
    /* First walk: 2 RECT commands. */
    for (int i = 0; i < 2; ++i) {
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 10.0F, .height = 10.0F};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    }
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_command_count(s_fx.ctx));

    /* Second walk: empty array. Getter must reflect THIS walk, not the
     * accumulated total. */
    inject_frozen_cmds(0);
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_command_count(s_fx.ctx));
}

// #region phase55_counter_tests
/* 3 RECTANGLE commands -> rect_command_count == 3, others 0. */
static void test_per_type_rect_command_count(void) {
    for (int i = 0; i < 3; ++i) {
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = (float)(i * 30), .y = 0.0F, .width = 20.0F, .height = 20.0F};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 0.0F, .b = 0.0F, .a = 255.0F};
    }
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_image_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_text_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_border_command_count(s_fx.ctx));
}

/* 1 RECT + 1 TEXT + 1 BORDER + 1 RECT -> rect=2, text=1, border=1, image=0. */
static void test_per_type_mixed_commands(void) {
    /* cmd 0: RECTANGLE */
    {
        Clay_RenderCommand *c = &s_test_cmds[0];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 0.0F, .b = 0.0F, .a = 255.0F};
    }
    /* cmd 1: TEXT */
    {
        Clay_RenderCommand *c = &s_test_cmds[1];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
        c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 20.0F};
        c->renderData.text.stringContents = (Clay_StringSlice){.chars = "test", .length = 4};
        c->renderData.text.textColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
        c->renderData.text.fontSize = 16;
        c->renderData.text.fontId = 0;
    }
    /* cmd 2: BORDER */
    {
        Clay_RenderCommand *c = &s_test_cmds[2];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_BORDER;
        c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F};
        c->renderData.border.color = (Clay_Color){.r = 255.0F, .g = 0.0F, .b = 0.0F, .a = 255.0F};
        c->renderData.border.width = (Clay_BorderWidth){.left = 2, .right = 2, .top = 2, .bottom = 2};
    }
    /* cmd 3: RECTANGLE */
    {
        Clay_RenderCommand *c = &s_test_cmds[3];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = 120.0F, .y = 0.0F, .width = 50.0F, .height = 50.0F};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0.0F, .g = 255.0F, .b = 0.0F, .a = 255.0F};
    }
    inject_frozen_cmds(4);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_image_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_text_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_border_command_count(s_fx.ctx));
}

/* SCISSOR_START + RECT + SCISSOR_END -> scissor_command_count=1, max_depth=1. */
static void test_scissor_command_count_and_depth(void) {
    /* cmd 0: SCISSOR_START */
    {
        Clay_RenderCommand *c = &s_test_cmds[0];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
        c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 400.0F, .height = 300.0F};
        c->renderData.clip.horizontal = true;
        c->renderData.clip.vertical = true;
    }
    /* cmd 1: RECTANGLE */
    {
        Clay_RenderCommand *c = &s_test_cmds[1];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = 10.0F, .y = 10.0F, .width = 80.0F, .height = 80.0F};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0.0F, .g = 0.0F, .b = 255.0F, .a = 255.0F};
    }
    /* cmd 2: SCISSOR_END */
    {
        Clay_RenderCommand *c = &s_test_cmds[2];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    }
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_scissor_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_max_scissor_depth(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
}

/* Walk with 2 RECTs, assert rect=2. Walk again with 0 cmds, assert all 0. */
static void test_counters_reset_each_walk(void) {
    for (int i = 0; i < 2; ++i) {
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = (float)(i * 30), .y = 0.0F, .width = 20.0F, .height = 20.0F};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    }
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));

    /* Second walk: empty. All per-type counters must be zero. */
    inject_frozen_cmds(0);
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_image_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_text_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_border_command_count(s_fx.ctx));
}

/* walk_ms is written every walk: non-negative after a real walk (monotonic
 * clock), and reset to 0.0F on the zero-viewport early-return path. The reset
 * is the deterministic half -- it fails loudly if a future edit forgets to
 * zero walk_ms in that early-return. (layout_ms is owned by nt_ui_end, which
 * the walker fixture never calls, so its timing is covered in
 * test_nt_ui_begin_end.c where a real begin/end cycle runs.) */
static void test_walk_ms_set_then_reset_on_early_return(void) {
    for (int i = 0; i < 3; ++i) {
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = (float)(i * 20), .y = 0.0F, .width = 10.0F, .height = 10.0F};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 0.0F, .b = 0.0F, .a = 255.0F};
    }
    inject_frozen_cmds(3);

    /* Sentinel: a removed walk-exit write leaves -1.0F and trips the assert. */
    s_fx.ctx->last_walk_ms = -1.0F;
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_TRUE(nt_ui_get_last_walk_ms(s_fx.ctx) >= 0.0F);

    /* Zero-width viewport hits the early return, which must zero walk_ms.
     * Value is non-negative, so "not positive" pins it to exactly 0 without a
     * float-equality compare. */
    nt_ui_target_t zero_target = {.viewport = {0.0F, 0.0F, 0.0F, 0.0F}};
    nt_ui_walk(s_fx.ctx, &zero_target);
    TEST_ASSERT_FALSE(nt_ui_get_last_walk_ms(s_fx.ctx) > 0.0F);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_last_walk_draw_calls_after_rect);
    RUN_TEST(test_get_last_walk_command_count_matches_frozen_cmds);
    RUN_TEST(test_metrics_bridge_publishes_to_nt_stats);
    RUN_TEST(test_getters_reflect_latest_walk_only);
    RUN_TEST(test_per_type_rect_command_count);
    RUN_TEST(test_per_type_mixed_commands);
    RUN_TEST(test_scissor_command_count_and_depth);
    RUN_TEST(test_counters_reset_each_walk);
    RUN_TEST(test_walk_ms_set_then_reset_on_early_return);
    return UNITY_END();
}
