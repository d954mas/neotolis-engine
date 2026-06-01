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
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, NULL, true);
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
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, NULL, true);
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
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, NULL, true);
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
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, NULL, true);
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
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, NULL, false);
        nt_ui_label(s_fx.ctx, NULL, "Off", &s_label_style);
        bool clicked = nt_ui_button_end(s_fx.ctx);
        TEST_ASSERT_FALSE(clicked); /* disabled never clicks */
    }
    nt_ui_end(s_fx.ctx); /* completes -> stacks balanced even on disabled branch */

    TEST_ASSERT_NOT_NULL(find_first_image_cmd(s_fx.ctx));
}

/* ---- Test 6: begin + centered label + end (was leaf sugar -- now inline) ---- */
static void test_button_begin_label_end_inline(void) {
    nt_pointer_t mouse = {0};
    bool clicked = true;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "Go", &s_label_style);
        clicked = nt_ui_button_end(s_fx.ctx);
    }
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
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_button_begin(s_fx.ctx, NULL, 0U, s_fx.atlas.handle, &s_btn_style, NULL, true)); }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test: decl override contract -- each of the 4 caller-clean fields asserts ----
 *      Sub-cases: non-zero id, non-NULL imageData, non-zero bg alpha, non-NULL userData.
 *      Each fails the override contract assertion (Phase 56 ext, P3-2). */
static void test_button_decl_asserts_caller_clean(void) {
    nt_pointer_t mouse = {0};

    /* Sub 1: decl->id.id != 0 */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        Clay_ElementDeclaration bad_id = {.id = (Clay_ElementId){.id = 0x1234U}};
        NT_TEST_EXPECT_ASSERT(nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, &bad_id, true));
    }
    nt_ui_end(s_fx.ctx);

    /* Sub 2: decl->image.imageData != NULL */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        int dummy = 0;
        Clay_ElementDeclaration bad_img = {.image = {.imageData = &dummy}};
        NT_TEST_EXPECT_ASSERT(nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, &bad_img, true));
    }
    nt_ui_end(s_fx.ctx);

    /* Sub 3: decl->backgroundColor.a != 0 */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        Clay_ElementDeclaration bad_bg = {.backgroundColor = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 255.0F}};
        NT_TEST_EXPECT_ASSERT(nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, &bad_bg, true));
    }
    nt_ui_end(s_fx.ctx);

    /* Sub 4: decl->userData != NULL */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        int dummy = 0;
        Clay_ElementDeclaration bad_user = {.userData = &dummy};
        NT_TEST_EXPECT_ASSERT(nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, &bad_user, true));
    }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

/* ---- Test: decl with FIXED sizing drives the hit-test bbox.
 *      Without the decl param, button_begin opens FIT IMAGE which shrinks to
 *      the label (~50x20). With decl.layout.sizing = FIXED(320, 180), Clay
 *      lays the button out at 320x180 and the hit-test honors that bbox.
 *      Two-frame setup: frame 1 declares so Clay has a prev-frame bbox;
 *      frame 2 queries get_interaction at the test points. ---- */
static void test_button_decl_fixed_size_hit_test(void) {
    static const Clay_ElementDeclaration fixed_decl = {
        .layout =
            {
                .sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(180)},
                .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
            },
    };

    /* Frame 1: declare so Clay caches the bbox for frame 2's hit-test. */
    nt_pointer_t f1 = {.x = 0.0F, .y = 0.0F, .active = true};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("fxbtn"), s_fx.atlas.handle, &s_btn_style, &fixed_decl, true);
        nt_ui_label(s_fx.ctx, NULL, "Hit", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Frame 2: pointer INSIDE the 320x180 bbox -- hovered. */
    nt_pointer_t f2 = {.x = 200.0F, .y = 100.0F, .active = true};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("fxbtn"), s_fx.atlas.handle, &s_btn_style, &fixed_decl, true);
        nt_ui_label(s_fx.ctx, NULL, "Hit", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    nt_ui_interaction_t inside = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("fxbtn"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(inside.hovered, "decl.layout.sizing FIXED(320,180) -- point (200,100) inside bbox must hover");

    /* Frame 3: pointer OUTSIDE the bbox -- not hovered. */
    nt_pointer_t f3 = {.x = 400.0F, .y = 100.0F, .active = true};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f3, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("fxbtn"), s_fx.atlas.handle, &s_btn_style, &fixed_decl, true);
        nt_ui_label(s_fx.ctx, NULL, "Hit", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    nt_ui_interaction_t outside = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("fxbtn"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE_MESSAGE(outside.hovered, "decl.layout.sizing FIXED(320,180) -- point (400,100) outside bbox must NOT hover");
}

/* ---- Test 8: nt_ui_begin clears pending_button.active (dev-mode recovery).
 *      Simulates the dev-build scenario where a previous frame asserted
 *      mid-button (leaving the field true). Without the reset every subsequent
 *      button_begin would assert "nested buttons unsupported". Pins the
 *      review §2 fix. ---- */
static void test_button_recovers_after_simulated_mid_button_state(void) {
    /* Wedge the field manually -- mimic the post-assert leftover state. */
    s_fx.ctx->pending_button.active = true;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    /* The reset must run unconditionally in nt_ui_begin. */
    TEST_ASSERT_FALSE_MESSAGE(s_fx.ctx->pending_button.active, "nt_ui_begin must clear pending_button.active so a previously-wedged dev session recovers");

    /* And a normal begin/end must not assert. */
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_button_text_only_children);
    RUN_TEST(test_button_icon_only_children);
    RUN_TEST(test_button_icon_and_text_children);
    RUN_TEST(test_button_stack_balanced);
    RUN_TEST(test_button_disabled_path_balanced);
    RUN_TEST(test_button_begin_label_end_inline);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_button_id_zero_asserts);
    RUN_TEST(test_button_decl_asserts_caller_clean);
#endif
    RUN_TEST(test_button_decl_fixed_size_hit_test);
    RUN_TEST(test_button_recovers_after_simulated_mid_button_state);
    return UNITY_END();
}
