#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "graphics/nt_gfx.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
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
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* walker exit flushes both sprite and text renderers. After
 * emitting 1 RECT into the sprite renderer's staging, the staging vertex
 * count is non-zero. After nt_ui_walk returns, the walker's exit-flush
 * MUST drain that staging back to 0. */
static void test_walker_exit_flushes_sprite_and_text(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 50, .height = 50};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 128, .g = 128, .b = 128, .a = 255};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* After flush, staging vertex_count resets to 0. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_sprite_renderer_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_vertex_count());
}

/* SCISSOR_START/END flushes both renderers before changing
 * scissor state. Sequence: RECT (accumulates) -> SCISSOR_START (must
 * flush sprite) -> RECT (accumulates again) -> SCISSOR_END (must flush
 * sprite) -> walk-exit flush. */
static void test_flush_on_scissor_transition(void) {
    Clay_RenderCommand *cmds = s_test_cmds;
    cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    cmds[1].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
    cmds[1].renderData.clip.horizontal = true;
    cmds[1].renderData.clip.vertical = true;
    cmds[2].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    cmds[2].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    cmds[2].renderData.rectangle.backgroundColor = (Clay_Color){.r = 0, .g = 255, .b = 0, .a = 255};
    cmds[3].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(4);

    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* scissor_push flushes the first RECT (1 draw call), then scissor_pop
     * flushes the second RECT (1 more). The walker-exit flush adds 0 more
     * because pop already drained staging. So delta == 2. */
    TEST_ASSERT_EQUAL_UINT32(calls_before + 2U, nt_sprite_renderer_test_draw_call_count());
}

/* RECT -> TEXT transition flushes sprite renderer before text
 * emit begins. We verify by checking that one draw call happened mid-
 * walk (sprite flush at TEXT boundary), even though the test font is
 * NOT registered (so the actual text path early-returns). The sprite
 * flush is unconditional at the top of emit_text. */
static void test_flush_on_rect_to_text_transition(void) {
    Clay_RenderCommand *cmds = s_test_cmds;
    cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};

    static const char *kText = "X";
    cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    cmds[1].boundingBox = (Clay_BoundingBox){.x = 20, .y = 20, .width = 10, .height = 10};
    cmds[1].renderData.text.stringContents = (Clay_StringSlice){.length = 1, .chars = kText, .baseChars = kText};
    cmds[1].renderData.text.textColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    cmds[1].renderData.text.fontId = 0;
    cmds[1].renderData.text.fontSize = 14;
    inject_frozen_cmds(2);

    const uint32_t sprite_calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* emit_text always flushes sprite at the top. That flush drains the
     * RECT staging into one draw call. Walker-exit flush adds nothing
     * (already drained). So delta == 1. */
    TEST_ASSERT_EQUAL_UINT32(sprite_calls_before + 1U, nt_sprite_renderer_test_draw_call_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_walker_exit_flushes_sprite_and_text);
    RUN_TEST(test_flush_on_scissor_transition);
    RUN_TEST(test_flush_on_rect_to_text_transition);
    return UNITY_END();
}
