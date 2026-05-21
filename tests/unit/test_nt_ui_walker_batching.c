#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "renderers/nt_sprite_renderer.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_DEFAULT_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 16
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];
static const char s_text[] = "X";

void setUp(void) {
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* Static-lifetime layer descriptors (compound literal pointer must outlive
 * the walk; file-scope static is the safest for tests). */
static const nt_ui_element_data_t k_layer_sprite = {.layer = 0U};
static const nt_ui_element_data_t k_layer_text = {.layer = 1U};

static void make_rect(int idx, int16_t z, float x) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->zIndex = z;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 10, .height = 10};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    c->userData = (void *)&k_layer_sprite;
}

static void make_text(int idx, int16_t z, float x) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    c->zIndex = z;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 10, .height = 10};
    c->renderData.text.stringContents = (Clay_StringSlice){.length = 1, .chars = s_text, .baseChars = s_text};
    c->renderData.text.textColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    c->renderData.text.fontId = 0;
    c->renderData.text.fontSize = 14;
    c->userData = (void *)&k_layer_text;
}

static int s_custom_calls;
static void custom_cb(const void *cmd, void *user) {
    (void)cmd;
    (void)user;
    /* When the walker invokes a custom callback, sprite_renderer must be
     * flushed first -- callback may bind its own pipeline. Verify staging
     * is empty at the moment of entry. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_sprite_renderer_test_vertex_count());
    ++s_custom_calls;
}

/* Interleaved RTRTRT @ z=0 with RECTs on layer 0 + TEXTs on layer 1
 * collapses to 1 sprite dc -- layer sort emits all RECTs first (single
 * sprite batch), then TEXTs flush sprite once on first emit_text entry. */
static void test_same_z_rect_text_batches(void) {
    make_rect(0, 0, 0);
    make_text(1, 0, 20);
    make_rect(2, 0, 40);
    make_text(3, 0, 60);
    make_rect(4, 0, 80);
    make_text(5, 0, 100);
    inject_frozen_cmds(6);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* Each scissor transition force-flushes to preserve clip scope. */
static void test_scissor_is_hard_barrier(void) {
    make_rect(0, 0, 0);
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[1].zIndex = 0;
    s_test_cmds[1].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
    s_test_cmds[1].renderData.clip.horizontal = true;
    s_test_cmds[1].renderData.clip.vertical = true;
    make_rect(2, 0, 20);
    s_test_cmds[3].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    s_test_cmds[3].zIndex = 0;
    make_rect(4, 0, 40);
    inject_frozen_cmds(5);

    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(calls_before + 3U, nt_sprite_renderer_test_draw_call_count());
}

/* CUSTOM callback sees clean renderer state (assert inside cb). */
static void test_custom_is_hard_barrier(void) {
    s_custom_calls = 0;
    nt_ui_set_custom_handler(s_fx.ctx, custom_cb, NULL);

    make_rect(0, 0, 0);
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    s_test_cmds[1].zIndex = 0;
    make_rect(2, 0, 20);
    inject_frozen_cmds(3);

    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(1, s_custom_calls);
    TEST_ASSERT_EQUAL_UINT32(calls_before + 2U, nt_sprite_renderer_test_draw_call_count());
}

/* z=0 RECT, z=2 TEXT, z=5 RECT -- TEXT between forces sprite flush. */
static void test_multi_z_preserves_painter_order(void) {
    make_rect(0, 0, 0);
    make_text(1, 2, 20);
    make_rect(2, 5, 40);
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* NULL userData defaults to layer 0 -- unlayered count exposes how many
 * segmentable commands fell through to the default layer. */
static void test_unlayered_count_tracks_null_userdata(void) {
    /* 3 commands without userData (NULL) + 0 with explicit data.
     * Use the bare commandType setup so userData stays NULL. */
    Clay_RenderCommand *c0 = &s_test_cmds[0];
    c0->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c0->zIndex = 0;
    c0->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c0->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    c0->userData = NULL;
    Clay_RenderCommand *c1 = &s_test_cmds[1];
    c1->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c1->zIndex = 0;
    c1->boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 10, .height = 10};
    c1->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0, .g = 255, .b = 0, .a = 255};
    c1->userData = NULL;
    Clay_RenderCommand *c2 = &s_test_cmds[2];
    c2->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c2->zIndex = 0;
    c2->boundingBox = (Clay_BoundingBox){.x = 40, .y = 0, .width = 10, .height = 10};
    c2->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0, .g = 0, .b = 255, .a = 255};
    c2->userData = (void *)&k_layer_sprite; /* explicit layer 0 */
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* 2 of 3 commands were NULL-userData. */
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_test_last_walk_unlayered_count(s_fx.ctx));
}

/* Layer 1 cmd declared FIRST but renders AFTER layer 0 cmd declared SECOND. */
static void test_layer_sort_overrides_declaration_order(void) {
    Clay_RenderCommand *c0 = &s_test_cmds[0];
    c0->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c0->zIndex = 0;
    c0->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c0->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    c0->userData = (void *)&k_layer_text; /* layer 1, declared first */

    Clay_RenderCommand *c1 = &s_test_cmds[1];
    c1->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c1->zIndex = 0;
    c1->boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 10, .height = 10};
    c1->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0, .g = 255, .b = 0, .a = 255};
    c1->userData = (void *)&k_layer_sprite; /* layer 0, declared second */
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Layer 0 emits FIRST (declared second), layer 1 emits SECOND (declared first).
     * The last_emit captures the most recent emit -- which must be layer 1's rect (x=0). */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    /* emit_screen_rect's transform places vertex 0 at exactly the rect's (x, y). */
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)pos[0]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_same_z_rect_text_batches);
    RUN_TEST(test_scissor_is_hard_barrier);
    RUN_TEST(test_custom_is_hard_barrier);
    RUN_TEST(test_multi_z_preserves_painter_order);
    RUN_TEST(test_unlayered_count_tracks_null_userdata);
    RUN_TEST(test_layer_sort_overrides_declaration_order);
    return UNITY_END();
}
