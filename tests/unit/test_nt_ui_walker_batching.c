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

static void make_rect(int idx, int16_t z, float x) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->zIndex = z;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 10, .height = 10};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
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

/* Sprite-side proof: same-z interleaved RECT/TEXT no longer splits the
 * sprite batch on every RECT->TEXT transition.
 *
 * Sequence: RTRTRT (3 of each) at z=0. The fixture does not register
 * a font, so emit_text early-returns after its prologue sprite flush;
 * the text-renderer side contributes 0 draw calls. What we can prove
 * via draw_call_count is that the SPRITE batch survived the
 * interleaving:
 *   -- Old walker: each TEXT drained the preceding RECT alone (1 dc
 *      per TEXT * 3 = 3 sprite dc).
 *   -- New walker (segment batching): Pass 1 stages all 3 RECTs
 *      together; first TEXT in Pass 2 triggers one sprite flush (1
 *      dc). All subsequent TEXTs flush an already-empty sprite stage.
 *      Total sprite dc = 1.
 * Proving the TEXT side batches symmetrically would require setting
 * up a real font + registered resource (see test_nt_ui_measure_cb for
 * the harness), out of scope for this batching unit. */
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

/* SCISSOR_START / SCISSOR_END are hard barriers -- the walker never
 * reorders commands across them. Sequence RECT-SCISSOR_START-RECT-
 * SCISSOR_END-RECT (all z=0) MUST produce 3 separate sprite draw calls,
 * one per scissor segment, because each scissor transition force-flushes
 * the renderer to preserve clip scope. */
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

    /* RECT[0] flushed by scissor_push, RECT[2] flushed by scissor_pop,
     * RECT[4] flushed by walker exit. 3 separate draw calls. */
    TEST_ASSERT_EQUAL_UINT32(calls_before + 3U, nt_sprite_renderer_test_draw_call_count());
}

/* CUSTOM is a hard barrier with the additional contract that the
 * callback sees clean renderer state. Sequence RECT-CUSTOM-RECT must
 * flush the first RECT before the callback runs (asserted inside the
 * callback) and emit the second RECT into a fresh batch afterwards. */
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
    /* RECT[0] flushed at CUSTOM entry, RECT[2] flushed at walker exit. */
    TEST_ASSERT_EQUAL_UINT32(calls_before + 2U, nt_sprite_renderer_test_draw_call_count());
}

/* Different zIndex layers must never merge -- painter order across z is
 * a hard contract (game code relies on it for stacking). RECT@z=0,
 * RECT@z=5 at separate z must emit as two segments. With segmentation
 * by zIndex, sprite_renderer auto-batches both into one draw call only
 * because they share material/page; the segments are emitted in
 * ascending-z order, which IS painter order, so the merge is correct.
 * But if we extend the sequence with a TEXT@z=2 BETWEEN them, the TEXT
 * MUST be emitted between the two RECTs (else painter order broken).
 * That forces a sprite flush at the TEXT transition -- so the total
 * draw-call delta is 1 sprite (RECT@z=0 alone, flushed when TEXT@z=2
 * arrives) + RECT@z=5 batched at exit = 2. */
static void test_multi_z_preserves_painter_order(void) {
    make_rect(0, 0, 0);
    make_text(1, 2, 20);
    make_rect(2, 5, 40);
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* 2 sprite draw calls (z=0 RECT, then z=5 RECT after TEXT). Text
     * font unregistered, contributes 0. */
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_same_z_rect_text_batches);
    RUN_TEST(test_scissor_is_hard_barrier);
    RUN_TEST(test_custom_is_hard_barrier);
    RUN_TEST(test_multi_z_preserves_painter_order);
    return UNITY_END();
}
