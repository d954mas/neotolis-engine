/* tests/unit/test_nt_ui_walker_custom.c -- Plan 52-04
 *
 * Covers WALK-05 / D-52-09: CUSTOM command -> registered handler called
 * with (cmd, userdata); NULL handler is a silent skip.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

static uint64_t s_arena[NT_UI_DEFAULT_ARENA_SIZE / 8u];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 4
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

/* Custom-handler observers. */
static int s_custom_calls;
static const void *s_custom_received_cmd;
static void *s_custom_received_user;

static void test_custom_handler(const void *clay_cmd, void *userdata) {
    s_custom_calls++;
    s_custom_received_cmd = clay_cmd;
    s_custom_received_user = userdata;
}

void setUp(void) {
    nt_test_assert_install();
    s_custom_calls = 0;
    s_custom_received_cmd = NULL;
    s_custom_received_user = NULL;
    memset(s_test_cmds, 0, sizeof s_test_cmds);

    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* WALK-05: registered handler is called with (clay_cmd, userdata). */
static void test_custom_handler_invoked(void) {
    int sentinel = 42;
    nt_ui_set_custom_handler(s_fx.ctx, test_custom_handler, &sentinel);

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 5, .y = 5, .width = 50, .height = 50};
    c->renderData.custom.backgroundColor = (Clay_Color){0};
    c->renderData.custom.customData = NULL;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(1, s_custom_calls);
    /* D-52-09 Option A: handler receives clay_cmd as const void * (opaque),
     * which is the same pointer that's in our cmds array slot 0. */
    TEST_ASSERT_EQUAL_PTR(c, s_custom_received_cmd);
    TEST_ASSERT_EQUAL_PTR(&sentinel, s_custom_received_user);
}

/* WALK-05 / D-52-09: NULL handler = silent skip (no crash, no warning). */
static void test_null_custom_handler_silent_skip(void) {
    nt_ui_set_custom_handler(s_fx.ctx, NULL, NULL);

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    /* Must NOT crash. */
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(0, s_custom_calls);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_custom_handler_invoked);
    RUN_TEST(test_null_custom_handler_silent_skip);
    return UNITY_END();
}
