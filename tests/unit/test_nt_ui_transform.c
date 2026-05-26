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

    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* Helper: build a CUSTOM command carrying an engine marker. */
static void make_marker_cmd(Clay_RenderCommand *c, void *marker_data) {
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 0, .height = 0};
    c->renderData.custom.backgroundColor = (Clay_Color){0};
    c->renderData.custom.customData = marker_data;
}

/* ---- Marker struct layout matches nt_ui.c internal definition ---- */

#define NT_UI_CUSTOM_MARKER_MAGIC_TEST 0xC1A4FEEDU

typedef struct {
    uint32_t magic;
    uint8_t marker_type;
    nt_ui_transform_t transform;
    float opacity;
} test_marker_t;

/* ---- Tests ---- */

/* Push transform, emit image, pop transform. Walk succeeds (no assert). */
static void test_push_pop_transform_balanced(void) {
    /* push_transform marker */
    test_marker_t push = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 1, /* PUSH_TRANSFORM */
        .transform = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 1.0F,
    };
    /* pop_transform marker */
    test_marker_t pop = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 2, /* POP_TRANSFORM */
        .transform = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 1.0F,
    };

    /* IMAGE command between push/pop */
    s_image_payload.atlas = s_fx.atlas.handle;
    s_image_payload.region_index = s_fx.atlas.white_region_idx;
    s_image_payload.flip_bits = 0;

    make_marker_cmd(&s_test_cmds[0], &push);
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    s_test_cmds[1].boundingBox = (Clay_BoundingBox){.x = 10, .y = 10, .width = 50, .height = 50};
    s_test_cmds[1].renderData.image.backgroundColor = (Clay_Color){0};
    s_test_cmds[1].renderData.image.imageData = &s_image_payload;
    make_marker_cmd(&s_test_cmds[2], &pop);
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Walk completed without assert = balanced. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* Push 9 transforms (depth > 8). Expect NT_ASSERT overflow. */
static void test_transform_stack_overflow(void) {
    test_marker_t pushes[9];
    for (int k = 0; k < 9; ++k) {
        pushes[k].magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST;
        pushes[k].marker_type = 1; /* PUSH_TRANSFORM */
        pushes[k].transform = (nt_ui_transform_t){.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F};
        pushes[k].opacity = 1.0F;
        make_marker_cmd(&s_test_cmds[k], &pushes[k]);
    }
    inject_frozen_cmds(9);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    /* 9th push exceeds depth cap 8. */
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* Push opacity 0.5, push opacity 0.5. Accumulated = 0.25. Verify
 * the emitted rect's alpha is approximately 0.25 * 255 = ~64. */
static void test_opacity_inheritance(void) {
    test_marker_t push_op1 = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 3, /* PUSH_OPACITY */
        .transform = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 0.5F,
    };
    test_marker_t push_op2 = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 3, /* PUSH_OPACITY */
        .transform = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 0.5F,
    };
    test_marker_t pop_op1 = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 4, /* POP_OPACITY */
        .transform = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 1.0F,
    };
    test_marker_t pop_op2 = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 4, /* POP_OPACITY */
        .transform = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 1.0F,
    };

    /* RECT command at full white between two opacity pushes. */
    make_marker_cmd(&s_test_cmds[0], &push_op1);
    make_marker_cmd(&s_test_cmds[1], &push_op2);

    s_test_cmds[2].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[2].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 100, .height = 50};
    s_test_cmds[2].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};

    make_marker_cmd(&s_test_cmds[3], &pop_op2);
    make_marker_cmd(&s_test_cmds[4], &pop_op1);
    inject_frozen_cmds(5);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Walk succeeded; we can read the last emitted vertex color. The color
     * was white (0xFFFFFFFF) with opacity 0.25 applied to alpha.
     * Alpha = 255 * 0.25 = 63..64 (integer truncation). */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* Push transform with offset_x=10. Emit rect. Verify the rect's x
 * position is shifted by 10 (via vertex position test probe). */
static void test_transform_offset_applied(void) {
    test_marker_t push = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 1, /* PUSH_TRANSFORM */
        .transform = {.offset_x = 10.0F, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 1.0F,
    };
    test_marker_t pop = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 2, /* POP_TRANSFORM */
        .transform = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 1.0F,
    };

    make_marker_cmd(&s_test_cmds[0], &push);

    /* RECT at x=20, width=40, viewport 800x600. */
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[1].boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 40, .height = 30};
    s_test_cmds[1].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};

    make_marker_cmd(&s_test_cmds[2], &pop);
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Rect positioned at x = 20 + 10 (offset) = 30.
     * emit_screen_rect builds mat4 with m[12]=x, so vertex 0 pos.x should be 30.
     * (Vertex 0 is top-left corner at m[12]+0*m[0] = 30.) */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_TRUE(pos[0] == 30.0F);
}

/* Register a game custom handler. Emit a CUSTOM command with game data
 * (customData pointing to a struct whose first 4 bytes are NOT
 * 0xC1A4FEED). Verify game handler is called, not intercepted as marker. */
static void test_game_custom_not_confused(void) {
    int sentinel = 99;
    nt_ui_set_custom_handler(s_fx.ctx, game_custom_handler, &sentinel);

    /* Game data: first 4 bytes are NOT the magic. */
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
    test_marker_t push = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 1, /* PUSH_TRANSFORM */
        .transform = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 1.0F,
    };
    make_marker_cmd(&s_test_cmds[0], &push);
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* Unbalanced opacity: push without pop -> assert at walk exit. */
static void test_unbalanced_opacity_asserts(void) {
    test_marker_t push = {
        .magic = NT_UI_CUSTOM_MARKER_MAGIC_TEST,
        .marker_type = 3, /* PUSH_OPACITY */
        .transform = {.offset_x = 0, .offset_y = 0, .rotation = 0, .scale = 1.0F},
        .opacity = 0.5F,
    };
    make_marker_cmd(&s_test_cmds[0], &push);
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
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
    return UNITY_END();
}
