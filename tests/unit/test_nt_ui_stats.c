/* tests/unit/test_nt_ui_stats.c -- Plan 52-05
 *
 * Covers WALK-09 / D-52-20: ui_draw_calls + ui_element_count user counters
 * are routed through nt_stats_count at nt_ui_walk exit.
 *
 * nt_stats has no public read-back-by-name accessor for user counters --
 * verification goes through (a) nt_ui_test_last_walk_* probes (per-walk
 * statics that Plan 04 captures and Plan 05 routes to nt_stats_count) and
 * (b) nt_stats_format_lines substring match (covers the wiring through to
 * nt_stats' user-counter table).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "stats/nt_stats.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

static uint64_t s_arena[NT_UI_DEFAULT_ARENA_SIZE / 8U];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 8
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

void setUp(void) {
    nt_test_assert_install();
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* WALK-09 / D-52-20: after a walk that emits a RECT, the walker's per-walk
 * draw-call delta probe is > 0 (at least the walker-exit flush ticked the
 * gfx draw-call counter) AND nt_stats_format_lines surfaces the
 * "ui_draw_calls" line (proving the value was routed into nt_stats). */
static void test_ui_draw_calls_counter_set(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 0.0F, .b = 0.0F, .a = 255.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Per-walk delta probe: at least the walker-exit flush ticked one draw call. */
    const uint32_t delta = nt_ui_test_last_walk_draw_call_delta(s_fx.ctx);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, delta);

    /* nt_stats wiring: the counter must surface in format_lines. */
    char buf[512];
    uint32_t n = nt_stats_format_lines(buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, n);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "ui_draw_calls:"), "nt_stats must contain ui_draw_calls counter after walk");

    /* The delta value must match what nt_stats holds -- verify via
     * substring of the formatted counter line. */
    char expected[64];
    (void)snprintf(expected, sizeof expected, "ui_draw_calls: %u", (unsigned)delta);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, expected), "ui_draw_calls value in nt_stats must match per-walk delta probe");
}

/* WALK-09 / D-52-20: ui_element_count equals frozen_cmds.length and is
 * routed into nt_stats. */
static void test_ui_element_count_counter_set(void) {
    /* 3 RECT commands -> walker iterates 3 elements. */
    for (int i = 0; i < 3; ++i) {
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = (float)(i * 20), .y = 0.0F, .width = 10.0F, .height = 10.0F};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0.0F, .g = 255.0F, .b = 0.0F, .a = 255.0F};
    }
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Element count probe matches frozen_cmds.length exactly (no Clay
     * wrapper elements -- frozen_cmds is the injected synthetic array). */
    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_test_last_walk_element_count(s_fx.ctx));

    /* nt_stats wiring. */
    char buf[512];
    (void)nt_stats_format_lines(buf, sizeof buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "ui_element_count: 3"), "nt_stats ui_element_count must equal frozen_cmds.length");
}

/* WALK-09 / D-52-20: counters are SET per walk, not accumulated. Walk twice
 * with different command counts -- the second walk's counter must reflect
 * the second declaration. */
static void test_counters_reset_per_walk(void) {
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

    const uint32_t count1 = nt_ui_test_last_walk_element_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(2U, count1);

    /* Second walk: empty command array. */
    inject_frozen_cmds(0);
    nt_ui_walk(s_fx.ctx, &target);

    const uint32_t count2 = nt_ui_test_last_walk_element_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(0U, count2);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(count1, count2, "second walk's element count must reflect the second declaration, not accumulate");

    /* nt_stats also reflects the second walk's value, not the first. */
    char buf[512];
    (void)nt_stats_format_lines(buf, sizeof buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "ui_element_count: 0"), "nt_stats ui_element_count must reflect second walk (=0), not first (=2)");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "ui_element_count: 2"), "old (first-walk) value must be overwritten in nt_stats");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ui_draw_calls_counter_set);
    RUN_TEST(test_ui_element_count_counter_set);
    RUN_TEST(test_counters_reset_per_walk);
    return UNITY_END();
}
