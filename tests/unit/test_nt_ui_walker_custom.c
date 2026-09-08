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
static void *s_custom_received_payload;
static int s_call_order[12];
static uint32_t s_call_order_count;

typedef struct {
    uint32_t calls;
    void *payload;
    nt_ui_custom_frame_t frame;
    Clay_BoundingBox bbox;
} callback_observer_t;

static callback_observer_t s_callback_a;
static callback_observer_t s_callback_b;

static void record_callback(callback_observer_t *observer, int marker, const nt_ui_custom_frame_t *frame, void *payload) {
    TEST_ASSERT_LESS_THAN_UINT32(12U, s_call_order_count);
    s_call_order[s_call_order_count++] = marker;
    observer->calls++;
    observer->payload = payload;
    observer->frame = *frame;
    observer->bbox = ((const Clay_RenderCommand *)frame->clay_cmd)->boundingBox;
}

static void callback_a(const nt_ui_custom_frame_t *frame, void *payload) { record_callback(&s_callback_a, 1, frame, payload); }

static void callback_b(const nt_ui_custom_frame_t *frame, void *payload) { record_callback(&s_callback_b, 2, frame, payload); }

static void test_custom_handler(const nt_ui_custom_frame_t *frame, void *userdata) {
    s_custom_calls++;
    s_custom_received_bbox = ((const Clay_RenderCommand *)frame->clay_cmd)->boundingBox;
    s_custom_received_user = userdata;
    const Clay_RenderCommand *cmd = frame->clay_cmd;
    const nt_ui_custom_data_t *cd = cmd->renderData.custom.customData;
    s_custom_received_payload = cd->data;
    TEST_ASSERT_LESS_THAN_UINT32(12U, s_call_order_count);
    s_call_order[s_call_order_count++] = 3;
}

void setUp(void) {
    nt_test_assert_install();
    s_custom_calls = 0;
    s_custom_received_bbox = (Clay_BoundingBox){0};
    s_custom_received_user = NULL;
    s_custom_received_payload = NULL;
    s_call_order_count = 0U;
    memset(s_call_order, 0, sizeof s_call_order);
    memset(&s_callback_a, 0, sizeof s_callback_a);
    memset(&s_callback_b, 0, sizeof s_callback_b);
    memset(s_test_cmds, 0, sizeof s_test_cmds);

    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) { ui_walker_fixture_inject_cmds(&s_fx, s_test_cmds, count, MAX_TEST_CMDS); }

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
    /* Handler receives the bbox in LAYOUT (Y-down) space; world_mat4 carries
     * the Y-flip + composed parent chain, so a rotated CUSTOM viewport
     * survives. Handler does the transform itself. */
    TEST_ASSERT_EQUAL_INT(5, (int)s_custom_received_bbox.x);
    TEST_ASSERT_EQUAL_INT(5, (int)s_custom_received_bbox.y);
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

static void test_callbacks_game_and_none_keep_dispatch_and_repeat_order(void) {
    int payload_a = 11;
    int payload_b = 22;
    int game_payload = 33;
    int game_user = 44;
    nt_ui_set_custom_handler(s_fx.ctx, test_custom_handler, &game_user);

    nt_ui_custom_data_t data[4] = {
        {.type = NT_UI_CUSTOM_TYPE_CALLBACK, .data = &payload_a, .emit = callback_a},
        {.type = NT_UI_CUSTOM_TYPE_GAME, .data = &game_payload, .emit = NULL},
        {.type = NT_UI_CUSTOM_TYPE_NONE, .data = &payload_a, .emit = callback_a},
        {.type = NT_UI_CUSTOM_TYPE_CALLBACK, .data = &payload_b, .emit = callback_b},
    };
    for (uint32_t i = 0U; i < 4U; i++) {
        s_test_cmds[i].commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
        s_test_cmds[i].boundingBox = (Clay_BoundingBox){.x = (float)(10U * i), .y = 20.0F, .width = 30.0F, .height = 40.0F};
        s_test_cmds[i].renderData.custom.customData = &data[i];
    }
    inject_frozen_cmds(4);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    const nt_ui_custom_frame_t first_frame = s_callback_a.frame;
    nt_ui_walk(s_fx.ctx, &target);

    const int expected_order[] = {1, 3, 2, 1, 3, 2};
    TEST_ASSERT_EQUAL_UINT32(6U, s_call_order_count);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected_order, s_call_order, 6U);
    TEST_ASSERT_EQUAL_UINT32(2U, s_callback_a.calls);
    TEST_ASSERT_EQUAL_UINT32(2U, s_callback_b.calls);
    TEST_ASSERT_EQUAL_INT(2, s_custom_calls);
    TEST_ASSERT_EQUAL_PTR(&payload_a, s_callback_a.payload);
    TEST_ASSERT_EQUAL_PTR(&payload_b, s_callback_b.payload);
    TEST_ASSERT_EQUAL_PTR(&game_payload, s_custom_received_payload);
    TEST_ASSERT_EQUAL_PTR(&game_user, s_custom_received_user);
    TEST_ASSERT_EQUAL_PTR(s_fx.ctx, s_callback_a.frame.ctx);
    TEST_ASSERT_EQUAL_PTR(&s_test_cmds[0], s_callback_a.frame.clay_cmd);
    TEST_ASSERT_EQUAL_PTR(&s_test_cmds[3], s_callback_b.frame.clay_cmd);
    TEST_ASSERT_EQUAL_MEMORY(first_frame.world_mat4, s_callback_a.frame.world_mat4, sizeof first_frame.world_mat4);
    TEST_ASSERT_TRUE(first_frame.opacity == s_callback_a.frame.opacity);
}

static void test_callback_receives_layout_bbox_and_composed_frame(void) {
    int payload = 77;
    nt_ui_custom_data_t cd = {.type = NT_UI_CUSTOM_TYPE_CALLBACK, .data = &payload, .emit = callback_a};
    nt_ui_transform_t parent_transform = nt_ui_transform_defaults();
    parent_transform.offset_x = 7.0F;
    parent_transform.offset_y = 11.0F;
    const nt_ui_transform_t child_transform = nt_ui_transform_defaults();

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("callback_parent"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 20.0F, .y = 30.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(100)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &parent_transform, 0.5F)}) {
        CLAY({.id = CLAY_ID("callback_child"),
              .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(50)}},
              .userData = (void *)NT_UI_DATA_XFORM(0U, &child_transform, 0.5F),
              .custom = {.customData = &cd}}) {}
    }
    nt_ui_end(s_fx.ctx);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(1U, s_callback_a.calls);
    TEST_ASSERT_EQUAL_INT(0, s_custom_calls);
    TEST_ASSERT_EQUAL_PTR(&payload, s_callback_a.payload);
    TEST_ASSERT_EQUAL_PTR(s_fx.ctx, s_callback_a.frame.ctx);
    TEST_ASSERT_TRUE(s_callback_a.bbox.x == 20.0F);
    TEST_ASSERT_TRUE(s_callback_a.bbox.y == 30.0F);
    TEST_ASSERT_TRUE(s_callback_a.bbox.width == 40.0F);
    TEST_ASSERT_TRUE(s_callback_a.bbox.height == 50.0F);
    TEST_ASSERT_TRUE(s_callback_a.frame.opacity == 0.25F);
    const float expected_world[16] = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 7, 589, 0, 1};
    for (uint32_t i = 0U; i < 16U; i++) {
        TEST_ASSERT_TRUE(s_callback_a.frame.world_mat4[i] == expected_world[i]);
    }

    /* A new frame must not retain the preceding frame's command callback. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_end(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(1U, s_callback_a.calls);
}

#if NT_TEST_CLAY_DEBUG_VIEW
static void test_clay_debug_view_after_begin_reserves_visible_sidebar(void) {
    nt_pointer_t mouse = {0};
    for (uint32_t frame = 0U; frame < 3U; frame++) {
        nt_ui_begin(s_fx.ctx, 1000.0F, 800.0F, 0.0F, &mouse, 1);
        TEST_ASSERT_FALSE(Clay_IsDebugModeEnabled());
        if (frame != 1U) {
            Clay_SetDebugModeEnabled(true);
        }
        if (frame == 2U) {
            Clay_SetDebugModeEnabled(false);
        }
        const uint32_t content_id = CLAY_ID("debug_content").id;
        const uint32_t view_id = CLAY_ID("Clay__DebugView").id;
        CLAY({.id = CLAY_ID("debug_content"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {}
        nt_ui_end(s_fx.ctx);

        const nt_ui_bbox_t content = nt_ui_get_bbox(s_fx.ctx, content_id);
        TEST_ASSERT_EQUAL_INT(frame == 0U ? 600 : 1000, (int)content.width);
        if (frame == 0U) {
            const nt_ui_bbox_t view = nt_ui_get_bbox(s_fx.ctx, view_id);
            TEST_ASSERT_EQUAL_INT(600, (int)view.x);
            TEST_ASSERT_EQUAL_INT(400, (int)view.width);
            TEST_ASSERT_EQUAL_INT(1000, (int)(view.x + view.width));
        }
        const nt_ui_target_t target = {.viewport = {0, 0, 1000, 800}};
        nt_ui_walk(s_fx.ctx, &target);
    }
}
#endif

static void test_null_command_callback_asserts_without_game_fallback(void) {
    int game_user = 99;
    nt_ui_set_custom_handler(s_fx.ctx, test_custom_handler, &game_user);
    nt_ui_custom_data_t cd = {.type = NT_UI_CUSTOM_TYPE_CALLBACK, .data = NULL, .emit = NULL};
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.width = 40.0F, .height = 50.0F};
    s_test_cmds[0].renderData.custom.customData = &cd;
    inject_frozen_cmds(1);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
    TEST_ASSERT_EQUAL_INT(0, s_custom_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, s_call_order_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_custom_handler_invoked);
    RUN_TEST(test_null_custom_handler_silent_skip);
    RUN_TEST(test_callbacks_game_and_none_keep_dispatch_and_repeat_order);
    RUN_TEST(test_callback_receives_layout_bbox_and_composed_frame);
    RUN_TEST(test_null_command_callback_asserts_without_game_fallback);
#if NT_TEST_CLAY_DEBUG_VIEW
    RUN_TEST(test_clay_debug_view_after_begin_reserves_visible_sidebar);
#endif
    return UNITY_END();
}
