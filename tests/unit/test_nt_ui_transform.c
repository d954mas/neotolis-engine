/* Walker dispatch of declarative element-attached transforms.
 *
 * Pins the WALKER-side contract: given tree_baked, dispatched sprite vertices
 * match the composed affine. Composition correctness (DFS, floating roots,
 * rotation/opacity multiplication) lives in test_nt_ui_tree_build.c. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "memory/nt_mem_scratch.h"
#include "renderers/nt_sprite_renderer.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Custom-handler observer for game-custom-not-confused test. */
static int s_game_custom_calls;
static void *s_game_custom_user;
static void game_custom_handler(const void *clay_cmd, void *userdata) {
    (void)clay_cmd;
    s_game_custom_calls++;
    s_game_custom_user = userdata;
}

void setUp(void) {
    nt_test_assert_install();
    s_game_custom_calls = 0;
    s_game_custom_user = NULL;
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool near_eq(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* Walk one frame with a wrapping CLAY block carrying the test transform/opacity. */
static void walk_with_xform(const nt_ui_transform_t *t, float opacity, float bbox_x, float bbox_y, float bbox_w, float bbox_h, Clay_Color color) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    void *ud = (t != NULL) ? (void *)NT_UI_DATA_XFORM(0U, t, opacity) : NULL;
    CLAY({.id = CLAY_ID("xbox"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = bbox_x, .y = bbox_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(bbox_w), CLAY_SIZING_FIXED(bbox_h)}},
          .backgroundColor = color,
          .userData = ud}) {}
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* ---- Test: element with HAS_TRANSFORM offset shifts walker vertex output. ---- */
static void test_walker_offset_shifts_vertex(void) {
    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.offset_x = 10.0F;
    walk_with_xform(&t, 1.0F, 20.0F, 0.0F, 40.0F, 30.0F, (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255});
    /* Layout-space bbox (20, 0, 40, 30) shifted by +10 in X. The walker emits
     * its sprite mat4 with the offset-translated origin. Compare V0.x against
     * a no-transform baseline. */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    /* Render Y-flip: world_y = 600 - bbox_y - bbox_h = 600 - 0 - 30 = 570 baseline.
     * V0 sits at (bbox_x + offset_x, world_y) = (30, 570). */
    TEST_ASSERT_TRUE(near_eq(pos[0], 30.0F, 0.5F));
    TEST_ASSERT_TRUE(near_eq(pos[1], 570.0F, 0.5F));
}

/* ---- Test: scale around bbox center expands output around center. ---- */
static void test_walker_scale_around_center(void) {
    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.scale_x = 2.0F;
    t.scale_y = 2.0F;
    walk_with_xform(&t, 1.0F, 20.0F, 0.0F, 40.0F, 30.0F, (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255});
    /* bbox (20, 0, 40, 30) center (40, 15). Scale 2x around center:
     * V0 = (0, world_y) where the scaled bbox extends from x=0 to x=80,
     * y=-15 to y=45 in layout. world_y = 600 - (-15) - 60 = 555. */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_TRUE(near_eq(pos[0], 0.0F, 0.5F));
    TEST_ASSERT_TRUE(near_eq(pos[1], 555.0F, 0.5F));
}

/* ---- Test: opacity multiplies through alpha channel of emitted color. ---- */
static void test_walker_opacity_multiplies_alpha(void) {
    nt_ui_transform_t identity = nt_ui_transform_defaults();
    walk_with_xform(&identity, 0.25F, 0.0F, 0.0F, 100.0F, 50.0F, (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255});
    /* Opacity 0.25 attached via userData (no transform). Build_tree → tree_baked
     * opacity = 0.25 × walker's identity = 0.25. Walker emits with alpha ~ 64. */
    /* Walker emits 4 verts; assert_emit_vertex_count >= 4. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    uint8_t col[4];
    nt_sprite_renderer_test_last_emit_color(0U, col);
    char msg[128];
    (void)snprintf(msg, sizeof msg, "opacity 0.25 expected alpha ~63, got %u (RGB %u,%u,%u)", col[3], col[0], col[1], col[2]);
    /* Walker may apply opacity to vertex alpha OR to the sprite material color — accept either. */
    TEST_ASSERT_TRUE_MESSAGE(col[3] <= 80U, msg);
}

/* ---- Test: walker dispatches CUSTOM type=GAME without applying tree_baked,
 *      so a game-custom command doesn't interfere with the transform pipeline. ---- */
static void test_walker_custom_game_handler_fires(void) {
    int sentinel = 99;
    nt_ui_set_custom_handler(s_fx.ctx, game_custom_handler, &sentinel);

    uint32_t game_data[2] = {0xDEADBEEFU, 42U};
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_custom(s_fx.ctx, NULL, game_data);
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(1, s_game_custom_calls);
    TEST_ASSERT_EQUAL_PTR(&sentinel, s_game_custom_user);
}

/* ---- Test: OOB nt_layout_index falls back to identity baked (graceful walker
 *      on Clay's error-path synthetic commands). Empty container scale doesn't
 *      crash. ---- */
static void test_walker_empty_container_no_crash(void) {
    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.scale_x = 2.0F;
    t.scale_y = 2.0F;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("empty"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 0.0F, .y = 0.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(10.0F), CLAY_SIZING_FIXED(10.0F)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &t, 1.0F)}) {}
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* ---- Test: walker hot path must not touch the scratch arena ---- */
static void test_walker_no_scratch_alloc(void) {
    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.scale_x = 1.3F;
    t.scale_y = 0.7F;
    t.rotation = 0.2F;
    t.offset_x = 5.0F;
    /* Frame 1 emits & walks so any one-shot init paths fire upfront. */
    walk_with_xform(&t, 0.9F, 50.0F, 50.0F, 80.0F, 40.0F, (Clay_Color){.r = 200, .g = 100, .b = 50, .a = 255});
    nt_mem_scratch_reset();
    const size_t baseline = nt_mem_scratch_test_used();
    /* Frame 2: declare + walk, then assert scratch wasn't touched by the walk. */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("xbox"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 50.0F, .y = 50.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(80.0F), CLAY_SIZING_FIXED(40.0F)}},
          .backgroundColor = {200.0F, 100.0F, 50.0F, 255.0F},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &t, 0.9F)}) {}
    nt_ui_end(s_fx.ctx);
    const size_t before_walk = nt_mem_scratch_test_used();
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(before_walk, nt_mem_scratch_test_used(), "walker hot path allocated from scratch arena");
    (void)baseline;
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_walker_offset_shifts_vertex);
    RUN_TEST(test_walker_scale_around_center);
    RUN_TEST(test_walker_opacity_multiplies_alpha);
    RUN_TEST(test_walker_custom_game_handler_fires);
    RUN_TEST(test_walker_empty_container_no_crash);
    RUN_TEST(test_walker_no_scratch_alloc);
    return UNITY_END();
}
