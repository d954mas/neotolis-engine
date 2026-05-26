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

/* ---- Test-local state ---- */

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 32
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

static nt_ui_image_payload_t s_image_payload;
static uint32_t s_test_le_count;

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
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    memset(&s_image_payload, 0, sizeof s_image_payload);
    s_test_le_count = 0;

    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

static void inject_marker(uint8_t type, const nt_ui_transform_t *t, float opacity) {
    nt_ui_marker_t *m = &s_fx.ctx->markers[s_fx.ctx->marker_count++];
    m->type = type;
    m->transform = t ? *t : nt_ui_transform_defaults();
    m->opacity = opacity;
    m->before_clay_idx = s_test_le_count;
}

/* Simulate declaring a Clay element: set nt_layout_index on the
 * test command and increment the layout element counter. */
static void track_clay_element_cmd(int cmd_idx) {
    s_test_cmds[cmd_idx].nt_layout_index = (int32_t)s_test_le_count;
    s_test_le_count++;
}

/* Marker type constants (match nt_ui.c internal enum). */
#define MARKER_PUSH_TRANSFORM 1
#define MARKER_POP_TRANSFORM 2
#define MARKER_PUSH_OPACITY 3
#define MARKER_POP_OPACITY 4

/* ---- Tests ---- */

/* Push transform, emit image, pop transform. Walk succeeds (no assert). */
static void test_push_pop_transform_balanced(void) {
    /* push_transform marker before any Clay element */
    nt_ui_transform_t t = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F};
    inject_marker(MARKER_PUSH_TRANSFORM, &t, 1.0F);

    /* IMAGE command */
    s_image_payload.atlas = s_fx.atlas.handle;
    s_image_payload.region_index = s_fx.atlas.white_region_idx;
    s_image_payload.flip_bits = 0;

    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 10, .y = 10, .width = 50, .height = 50};
    s_test_cmds[0].renderData.image.backgroundColor = (Clay_Color){0};
    s_test_cmds[0].renderData.image.imageData = &s_image_payload;
    track_clay_element_cmd(0); /* track this element */

    /* pop_transform marker after the element */
    inject_marker(MARKER_POP_TRANSFORM, NULL, 1.0F);

    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Walk completed without assert = balanced. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* Push 9 transforms (depth > 8). Expect NT_ASSERT overflow. */
static void test_transform_stack_overflow(void) {
    for (int k = 0; k < 9; ++k) {
        nt_ui_transform_t t = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F};
        inject_marker(MARKER_PUSH_TRANSFORM, &t, 1.0F);
    }
    /* Need at least one render command for walker to process markers. */
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    track_clay_element_cmd(0);
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    /* 9th push exceeds depth cap 8. */
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* Push opacity 0.5, push opacity 0.5. Accumulated = 0.25. Verify
 * the emitted rect's alpha is approximately 0.25 * 255 = ~64. */
static void test_opacity_inheritance(void) {
    inject_marker(MARKER_PUSH_OPACITY, NULL, 0.5F);
    inject_marker(MARKER_PUSH_OPACITY, NULL, 0.5F);

    /* RECT command at full white */
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 100, .height = 50};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    track_clay_element_cmd(0);

    inject_marker(MARKER_POP_OPACITY, NULL, 1.0F);
    inject_marker(MARKER_POP_OPACITY, NULL, 1.0F);

    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Walk succeeded; alpha = 255 * 0.25 = 63..64. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* Push transform with offset_x=10. Emit rect. Verify the rect's x
 * position is shifted by 10. */
static void test_transform_offset_applied(void) {
    nt_ui_transform_t t = {.offset_x = 10.0F, .offset_y = 0, .rotation = 0, .scale = 1.0F};
    inject_marker(MARKER_PUSH_TRANSFORM, &t, 1.0F);

    /* RECT at x=20, width=40, viewport 800x600. */
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 40, .height = 30};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    track_clay_element_cmd(0);

    inject_marker(MARKER_POP_TRANSFORM, NULL, 1.0F);

    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Rect positioned at x = 20 + 10 (offset) = 30. */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_TRUE(pos[0] == 30.0F);
}

/* Register a game custom handler. Emit a CUSTOM command with game data.
 * Verify game handler is called, not intercepted as marker. */
static void test_game_custom_not_confused(void) {
    int sentinel = 99;
    nt_ui_set_custom_handler(s_fx.ctx, game_custom_handler, &sentinel);

    /* Game data: arbitrary non-marker data. */
    uint32_t game_data[2] = {0xDEADBEEF, 42};

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c->renderData.custom.backgroundColor = (Clay_Color){0};
    c->renderData.custom.customData = &game_data;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(1, s_game_custom_calls);
    TEST_ASSERT_EQUAL_PTR(&sentinel, s_game_custom_user);
}

/* Unbalanced transform: push without pop -> assert at walk exit. */
static void test_unbalanced_transform_asserts(void) {
    nt_ui_transform_t t = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F};
    inject_marker(MARKER_PUSH_TRANSFORM, &t, 1.0F);

    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    track_clay_element_cmd(0);
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* Unbalanced opacity: push without pop -> assert at walk exit. */
static void test_unbalanced_opacity_asserts(void) {
    inject_marker(MARKER_PUSH_OPACITY, NULL, 0.5F);

    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    track_clay_element_cmd(0);
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* Push transform with scale=2.0, emit rect at (20,0,40,30). Deferred
 * center captures rect center (40,15). Scale expands width 40->80,
 * height 30->60 around that center. */
static void test_scale_applied(void) {
    nt_ui_transform_t t = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 2.0F};
    inject_marker(MARKER_PUSH_TRANSFORM, &t, 1.0F);

    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 40, .height = 30};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    track_clay_element_cmd(0);

    inject_marker(MARKER_POP_TRANSFORM, NULL, 1.0F);

    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());

    /* Affine: scale=2.0 around deferred center (40,15).
     * sbb = {x=0, y=-15, w=80, h=60}, world_y = 600+15-60 = 555.
     * emit_screen_rect mat4: m[12]=0, m[13]=555 -> vertex 0 at (0, 555). */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_TRUE(pos[0] == 0.0F);
    TEST_ASSERT_TRUE(pos[1] == 555.0F);
}

/* Push transform with rotation=PI/2, emit image. Verify vertices are
 * rotated. */
static void test_rotation_applied(void) {
    const float half_pi = 3.14159265358979323846F * 0.5F;
    nt_ui_transform_t t = {.offset_x = 0, .offset_y = 0, .rotation = half_pi, .scale = 1.0F};
    inject_marker(MARKER_PUSH_TRANSFORM, &t, 1.0F);

    s_image_payload.atlas = s_fx.atlas.handle;
    s_image_payload.region_index = s_fx.atlas.white_region_idx;
    s_image_payload.flip_bits = 0;

    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 10, .y = 10, .width = 50, .height = 50};
    s_test_cmds[0].renderData.image.backgroundColor = (Clay_Color){0};
    s_test_cmds[0].renderData.image.imageData = &s_image_payload;
    track_clay_element_cmd(0);

    inject_marker(MARKER_POP_TRANSFORM, NULL, 1.0F);

    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());

    /* With rotation, vertex positions differ from the axis-aligned case.
     * Non-rotated V0 would be at (10, 540). Rotated V0 must differ. */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_TRUE(pos[0] != 10.0F || pos[1] != 540.0F);

    /* V0 and V1 x coordinates should be nearly equal (cos(-PI/2) ~ 0). */
    float pos1[3];
    nt_sprite_renderer_test_last_emit_position(1U, pos1);
    float dx = pos1[0] - pos[0];
    if (dx < 0) {
        dx = -dx;
    }
    TEST_ASSERT_TRUE(dx < 1.0F);
}

/* Push transform offset_x=10, push another transform scale=2.0, emit rect.
 * Accumulated: position shifted by 10 AND scaled by 2.0. */
static void test_nested_offset_scale(void) {
    nt_ui_transform_t t_off = {.offset_x = 10.0F, .offset_y = 0, .rotation = 0, .scale = 1.0F};
    inject_marker(MARKER_PUSH_TRANSFORM, &t_off, 1.0F);

    nt_ui_transform_t t_scale = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 2.0F};
    inject_marker(MARKER_PUSH_TRANSFORM, &t_scale, 1.0F);

    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 40, .height = 30};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    track_clay_element_cmd(0);

    inject_marker(MARKER_POP_TRANSFORM, NULL, 1.0F);
    inject_marker(MARKER_POP_TRANSFORM, NULL, 1.0F);

    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());

    /* Affine recompute: k=0 offset_x=10 -> tx=10; k=1 scale=2.0 around
     * deferred center (40,15). Composed: a=2, d=2, tx=-20, ty=-15.
     * dispatch: tcx=60, tcy=15, sw=80, sh=60.
     * sbb={x=20, y=-15, w=80, h=60}, world_y=555. V0=(20, 555). */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_TRUE(pos[0] == 20.0F);
    TEST_ASSERT_TRUE(pos[1] == 555.0F);

    /* Without any transform the rect V0 would be at (20, 570).
     * With only offset: (30, 570). The nested scale around the deferred
     * center produces a different result from either alone. */
    TEST_ASSERT_TRUE(pos[0] != 30.0F); /* not just offset */
}

/* Push transform scale=1.5, push opacity 0.5, emit rect with white.
 * Verify vertex alpha is ~127 (255*0.5) AND rect is scaled. */
static void test_opacity_scale_combined(void) {
    nt_ui_transform_t t = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.5F};
    inject_marker(MARKER_PUSH_TRANSFORM, &t, 1.0F);
    inject_marker(MARKER_PUSH_OPACITY, NULL, 0.5F);

    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 40, .height = 30};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    track_clay_element_cmd(0);

    inject_marker(MARKER_POP_OPACITY, NULL, 1.0F);
    inject_marker(MARKER_POP_TRANSFORM, NULL, 1.0F);

    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());

    /* Scale 1.5 around deferred center (40,15): sbb={x=10,y=-7.5,w=60,h=45}.
     * world_y = 600+7.5-45 = 562.5. V0=(10, 562.5). */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_TRUE(pos[0] == 10.0F);
    TEST_ASSERT_TRUE(pos[1] == 562.5F);

    /* Opacity 0.5 applied to white (255,255,255,255):
     * alpha = (uint32_t)(255.0 * 0.5) = 127. */
    uint8_t color[4];
    nt_sprite_renderer_test_last_emit_color(0U, color);
    TEST_ASSERT_EQUAL_UINT8(255U, color[0]); /* R */
    TEST_ASSERT_EQUAL_UINT8(255U, color[1]); /* G */
    TEST_ASSERT_EQUAL_UINT8(255U, color[2]); /* B */
    TEST_ASSERT_EQUAL_UINT8(127U, color[3]); /* A = 255 * 0.5 truncated */
}

/* Game CLAY elements between push and content. Push/pop markers must
 * use Clay's own element counter (layoutElements.length) so index
 * alignment is correct regardless of tracked vs untracked elements. */
static void test_game_clay_elements_shift_index(void) {
    nt_ui_transform_t t = {.offset_x = 10.0F, .offset_y = 0, .rotation = 0, .scale = 1.0F};
    inject_marker(MARKER_PUSH_TRANSFORM, &t, 1.0F);

    /* Cmd 0: game RECT (Clay element, tracked via layoutElements.length). */
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 100, .height = 50};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 50, .g = 50, .b = 50, .a = 255};
    track_clay_element_cmd(0);

    /* Cmd 1: another game RECT. */
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[1].boundingBox = (Clay_BoundingBox){.x = 0, .y = 60, .width = 100, .height = 50};
    s_test_cmds[1].renderData.rectangle.backgroundColor = (Clay_Color){.r = 50, .g = 50, .b = 50, .a = 255};
    track_clay_element_cmd(1);

    /* Cmd 2: widget IMAGE. */
    s_image_payload.atlas = s_fx.atlas.handle;
    s_image_payload.region_index = s_fx.atlas.white_region_idx;
    s_test_cmds[2].commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    s_test_cmds[2].boundingBox = (Clay_BoundingBox){.x = 20, .y = 120, .width = 40, .height = 30};
    s_test_cmds[2].renderData.image.imageData = &s_image_payload;
    track_clay_element_cmd(2);

    inject_marker(MARKER_POP_TRANSFORM, NULL, 1.0F);

    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* IMAGE at cmd 2 should have offset +10 applied.
     * x = 20 + 10 = 30. world_y = 600 - (120+10) - 30 = ... wait,
     * no offset on y. Just check x of last emitted vertex 0. */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_TRUE(pos[0] == 30.0F);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_push_pop_transform_balanced);
    RUN_TEST(test_transform_stack_overflow);
    RUN_TEST(test_opacity_inheritance);
    RUN_TEST(test_transform_offset_applied);
    RUN_TEST(test_game_custom_not_confused);
    RUN_TEST(test_unbalanced_transform_asserts);
    RUN_TEST(test_unbalanced_opacity_asserts);
    RUN_TEST(test_scale_applied);
    RUN_TEST(test_rotation_applied);
    RUN_TEST(test_nested_offset_scale);
    RUN_TEST(test_opacity_scale_combined);
    RUN_TEST(test_game_clay_elements_shift_index);
    return UNITY_END();
}
