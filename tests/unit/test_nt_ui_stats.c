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

#define MAX_TEST_CMDS 8
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_last_walk_draw_calls_after_rect);
    RUN_TEST(test_get_last_walk_command_count_matches_frozen_cmds);
    RUN_TEST(test_metrics_bridge_publishes_to_nt_stats);
    RUN_TEST(test_getters_reflect_latest_walk_only);
    return UNITY_END();
}
