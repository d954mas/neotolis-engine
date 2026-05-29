/* RED scaffold for the nt_ui_button container widget (WIDGET-03).
 *
 * Wave 0 (Plan 01): registration + RED status only. The real assertions land
 * in Plan 04 once nt_ui_button_begin/end + the leaf sugar exist. The button is
 * a CONTAINER (panel clone + an .id) whose content (text / icon) is composed as
 * children via nt_ui_label / nt_ui_image. Each test body is a TEST_FAIL_MESSAGE
 * placeholder.
 *
 * Test design (RESEARCH 56 New Work Item 4 + Validation Architecture):
 *   - Child composition: text-only / icon-only / icon+text → count TEXT vs
 *     IMAGE render commands via the find_first_*_cmd helpers (mirrors
 *     test_nt_ui_panel.c).
 *   - Stack balance: nt_ui_button_begin/end must push+pop transform+opacity
 *     symmetrically on EVERY branch (incl. the disabled path), or the walker's
 *     transform/opacity stacks underflow. Mirrors test_nt_ui_panel.c balanced
 *     test (the walk must succeed). */

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
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Helpers copied from test_nt_ui_panel.c -- Plan 04 uses these to count the
 * button's child render commands (TEXT for label, IMAGE for icon + bg). */
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

/* ---- Test 1: text-only button emits TEXT + background IMAGE ---- */
static void test_button_text_only_children(void) {
    /* Plan 04: nt_ui_button(ctx, id, "Click", &style) → one TEXT (label) and
     * one IMAGE (slice9 bg). References nt_ui_button + find_first_*_cmd. */
    (void)find_first_image_cmd;
    (void)find_first_text_cmd;
    TEST_FAIL_MESSAGE("Plan 04 (Wave 2): nt_ui_button not implemented");
}

/* ---- Test 2: icon-only button emits IMAGE child (no TEXT) ---- */
static void test_button_icon_only_children(void) {
    /* Plan 04: button_begin + nt_ui_image child + button_end → IMAGE present,
     * TEXT absent. References nt_ui_button_begin/end. */
    TEST_FAIL_MESSAGE("Plan 04 (Wave 2): nt_ui_button_begin/end not implemented");
}

/* ---- Test 3: icon+text button emits both child command types ---- */
static void test_button_icon_and_text_children(void) {
    /* Plan 04: button_begin + image + label + button_end → both IMAGE and TEXT
     * present. References nt_ui_button_begin/end. */
    TEST_FAIL_MESSAGE("Plan 04 (Wave 2): nt_ui_button_begin/end not implemented");
}

/* ---- Test 4: begin/end transform+opacity stack balanced (no underflow) ---- */
static void test_button_stack_balanced(void) {
    /* Plan 04: button_begin/end must push+pop transform+opacity symmetrically
     * so nt_ui_walk succeeds (mirror test_nt_ui_panel.c balance test).
     * References nt_ui_button_begin/end. */
    TEST_FAIL_MESSAGE("Plan 04 (Wave 2): nt_ui_button_begin/end not implemented");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_button_text_only_children);
    RUN_TEST(test_button_icon_only_children);
    RUN_TEST(test_button_icon_and_text_children);
    RUN_TEST(test_button_stack_balanced);
    return UNITY_END();
}
