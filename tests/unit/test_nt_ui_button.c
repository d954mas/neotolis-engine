/* GREEN tests for the nt_ui_button container widget (WIDGET-02/03/04, Plan 04).
 *
 * The button is a CONTAINER (panel clone + a Clay .id) whose content
 * (text / icon / icon+text) is composed as children via nt_ui_label /
 * nt_ui_image. This test focuses on:
 *   - Child composition: text-only / icon-only / icon+text -> count TEXT vs
 *     IMAGE render commands (the button bg is itself one IMAGE).
 *   - Stack balance: nt_ui_button_begin/end push+pop transform+opacity
 *     symmetrically on EVERY branch (incl. the disabled path), so the walker's
 *     transform/opacity stacks never underflow. Mirrors test_nt_ui_panel.c.
 *   - Leaf sugar nt_ui_button(...) = begin + centered label + end, returns
 *     clicked (false with no input).
 * (Click-fires-once is covered by test_nt_ui_interaction per the VALIDATION map;
 * here transition_speed=0 makes the per-state apply deterministic/instant.) */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Non-degenerate per-state visuals; transition_speed 0 = instant (deterministic
 * for a 1-frame test). idle/hover opaque; pressed shrinks; disabled dims. */
static const nt_ui_button_style_t s_btn_style = {
    .idle = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
    .hover = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.05F, .opacity = 1.0F},
    .pressed = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 0.95F, .opacity = 1.0F},
    .disabled = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 0.5F},
    .transition_speed = 0.0F,
};

static const nt_ui_label_style_t s_label_style = {
    .font_id = 0,
    .font_size = 14,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
};

static const nt_ui_image_style_t s_img_style = {
    .color_packed = 0xFFFFFFFF,
    .flip_bits = 0,
    .slice9_lrtb = {0, 0, 0, 0},
};

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Helpers mirror test_nt_ui_panel.c -- count the button's child render cmds. */
static const Clay_RenderCommand *find_first_image_cmd(const nt_ui_context_t *ctx) {
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            return c;
        }
    }
    return NULL;
}

static const Clay_RenderCommand *find_first_text_cmd(const nt_ui_context_t *ctx) {
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            return c;
        }
    }
    return NULL;
}

static uint32_t count_cmd_of_type(const nt_ui_context_t *ctx, Clay_RenderCommandType type) {
    uint32_t n = 0;
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        if (ctx->frozen_cmds.internalArray[i].commandType == type) {
            ++n;
        }
    }
    return n;
}

/* ---- Test 1: text-only button emits TEXT + background IMAGE ---- */
static void test_button_text_only_children(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        bool clicked = nt_ui_button_end(s_fx.ctx);
        TEST_ASSERT_FALSE(clicked); /* no input -> no click */
    }
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_NOT_NULL(find_first_image_cmd(s_fx.ctx)); /* button bg */
    TEST_ASSERT_NOT_NULL(find_first_text_cmd(s_fx.ctx));  /* label child */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_IMAGE));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_TEXT));
}

/* ---- Test 2: icon-only button emits IMAGE child (>=2 IMAGE, 0 TEXT) ---- */
static void test_button_icon_only_children(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, true);
        nt_ui_image(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_img_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* bg IMAGE + icon IMAGE = at least 2; no label -> 0 TEXT. */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_IMAGE));
    TEST_ASSERT_EQUAL_UINT32(0U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_TEXT));
}

/* ---- Test 3: icon+text button emits both child command types ---- */
static void test_button_icon_and_text_children(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, true);
        nt_ui_image(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_img_style);
        nt_ui_label(s_fx.ctx, NULL, "Save", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_IMAGE));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_TEXT));
}

/* ---- Test 4: begin/end transform+opacity stack balanced (enabled path) ---- */
static void test_button_stack_balanced(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, true);
        nt_ui_label(s_fx.ctx, NULL, "Balance", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx); /* walk must complete with no transform/opacity underflow */

    TEST_ASSERT_NOT_NULL(find_first_image_cmd(s_fx.ctx));
}

/* ---- Test 5: DISABLED path is balanced (begin still pushes, end still pops) ---- */
static void test_button_disabled_path_balanced(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, false);
        nt_ui_label(s_fx.ctx, NULL, "Off", &s_label_style);
        bool clicked = nt_ui_button_end(s_fx.ctx);
        TEST_ASSERT_FALSE(clicked); /* disabled never clicks */
    }
    nt_ui_end(s_fx.ctx); /* completes -> stacks balanced even on disabled branch */

    TEST_ASSERT_NOT_NULL(find_first_image_cmd(s_fx.ctx));
}

/* ---- Test 6: leaf sugar nt_ui_button(...) = bg IMAGE + label TEXT, !clicked ---- */
static void test_button_leaf_sugar(void) {
    nt_pointer_t mouse = {0};
    bool clicked = true;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { clicked = nt_ui_button(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, "Go", &s_label_style, &s_btn_style, true); }
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_FALSE(clicked); /* no press this frame */
    TEST_ASSERT_NOT_NULL(find_first_image_cmd(s_fx.ctx));
    TEST_ASSERT_NOT_NULL(find_first_text_cmd(s_fx.ctx));
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* ---- Test 7: button_begin with id 0 (no-widget sentinel) asserts ---- */
static void test_button_id_zero_asserts(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_button_begin(s_fx.ctx, NULL, 0U, s_fx.atlas.handle, &s_btn_style, true)); }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_button_text_only_children);
    RUN_TEST(test_button_icon_only_children);
    RUN_TEST(test_button_icon_and_text_children);
    RUN_TEST(test_button_stack_balanced);
    RUN_TEST(test_button_disabled_path_balanced);
    RUN_TEST(test_button_leaf_sugar);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_button_id_zero_asserts);
#endif
    return UNITY_END();
}
