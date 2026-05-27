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

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 4
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

/* Custom-handler observers. */
static int s_custom_calls;
static Clay_BoundingBox s_custom_received_bbox;
static void *s_custom_received_user;

static void test_custom_handler(const void *clay_cmd, void *userdata) {
    s_custom_calls++;
    s_custom_received_bbox = ((const Clay_RenderCommand *)clay_cmd)->boundingBox;
    s_custom_received_user = userdata;
}

void setUp(void) {
    nt_test_assert_install();
    s_custom_calls = 0;
    s_custom_received_bbox = (Clay_BoundingBox){0};
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

/* registered handler is called with (clay_cmd, userdata). */
static void test_custom_handler_invoked(void) {
    int sentinel = 42;
    nt_ui_set_custom_handler(s_fx.ctx, test_custom_handler, &sentinel);

    nt_ui_custom_data_t cd = {.type = NT_UI_CUSTOM_TYPE_GAME, .data = NULL};
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 5, .y = 5, .width = 50, .height = 50};
    c->renderData.custom.backgroundColor = (Clay_Color){0};
    c->renderData.custom.customData = &cd;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(1, s_custom_calls);
    /* Handler receives a local copy with bbox.y Y-flipped to GL world space:
     * world_y = vy + vh - bb.y - bb.h = 0 + 600 - 5 - 50 = 545. */
    TEST_ASSERT_EQUAL_INT(5, (int)s_custom_received_bbox.x);
    TEST_ASSERT_EQUAL_INT(545, (int)s_custom_received_bbox.y);
    TEST_ASSERT_EQUAL_INT(50, (int)s_custom_received_bbox.width);
    TEST_ASSERT_EQUAL_INT(50, (int)s_custom_received_bbox.height);
    TEST_ASSERT_EQUAL_PTR(&sentinel, s_custom_received_user);
}

/* NULL handler = silent skip (no crash, no warning). */
static void test_null_custom_handler_silent_skip(void) {
    nt_ui_set_custom_handler(s_fx.ctx, NULL, NULL);

    nt_ui_custom_data_t cd = {.type = NT_UI_CUSTOM_TYPE_GAME, .data = NULL};
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c->renderData.custom.customData = &cd;
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
