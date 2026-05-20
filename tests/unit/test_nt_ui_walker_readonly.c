/* tests/unit/test_nt_ui_walker_readonly.c -- Plan 52-04
 *
 * Covers UI-06 (two walks against same ctx+target produce identical state)
 * and UI-07 (walker entry applies target->viewport via nt_gfx_set_viewport).
 * Three death-tests for pre-walk asserts (D-52-06 atlas, D-52-19 sprite +
 * text material). All death-tests use NT_TEST_EXPECT_ASSERT (Revision
 * Issue 3) -- no TEST_IGNORE fallback.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "graphics/nt_gfx.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_DEFAULT_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 8
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

/* Death-tests need a partially-bound walker (one of atlas / sprite material /
 * text material omitted). The next setUp reads this and forwards it to
 * ui_walker_fixture_init's bind mask. */
static ui_walker_fx_bind_t s_setup_bind = UI_WALKER_FX_BIND_ALL;

void setUp(void) {
    nt_test_assert_install();
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, s_setup_bind);
}

void tearDown(void) {
    ui_walker_fixture_shutdown(&s_fx);
    /* Reset to all-bound; per-test mode is re-armed via RUN_TEST_WITH_BIND. */
    s_setup_bind = UI_WALKER_FX_BIND_ALL;
}

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* UI-06: two walks against same ctx+target produce identical probe state. */
static void test_second_walk_identical(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 10, .y = 20, .width = 30, .height = 40};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 200, .g = 200, .b = 200, .a = 255};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    int vp1[4];
    nt_gfx_test_viewport_rect(vp1);
    const uint32_t elements1 = nt_ui_test_last_walk_element_count(s_fx.ctx);
    /* Read but don't compare draw-call delta -- walking once already
     * incurs draw calls; a second walk will too, so the EXACT delta-
     * to-delta count is what we compare. */
    const uint32_t delta1 = nt_ui_test_last_walk_draw_call_delta(s_fx.ctx);

    nt_ui_walk(s_fx.ctx, &target);

    int vp2[4];
    nt_gfx_test_viewport_rect(vp2);
    const uint32_t elements2 = nt_ui_test_last_walk_element_count(s_fx.ctx);
    const uint32_t delta2 = nt_ui_test_last_walk_draw_call_delta(s_fx.ctx);

    TEST_ASSERT_EQUAL_INT_ARRAY(vp1, vp2, 4);
    TEST_ASSERT_EQUAL_UINT32(elements1, elements2);
    TEST_ASSERT_EQUAL_UINT32(delta1, delta2);
}

/* UI-07: walker entry applies target->viewport via nt_gfx_set_viewport. */
static void test_viewport_applied(void) {
    inject_frozen_cmds(0);

    nt_ui_target_t target = {.viewport = {100.0F, 200.0F, 640.0F, 480.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    int vp[4];
    nt_gfx_test_viewport_rect(vp);
    TEST_ASSERT_EQUAL_INT(100, vp[0]);
    TEST_ASSERT_EQUAL_INT(200, vp[1]);
    TEST_ASSERT_EQUAL_INT(640, vp[2]);
    TEST_ASSERT_EQUAL_INT(480, vp[3]);
}

/* D-52-06 death-test: walk without atlas set asserts. */
static void test_walk_without_atlas_asserts(void) {
    /* Mode flag was set before Unity invoked setUp -- the atlas setter
     * was skipped, so g_nt_ui_atlas.id == 0. */
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* D-52-19 / Revision Issue 1 death-test: walk without sprite material asserts. */
static void test_walk_without_sprite_material_asserts(void) {
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* D-52-19 death-test: walk without text material asserts. */
static void test_walk_without_text_material_asserts(void) {
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* Macro to set s_setup_bind BEFORE RUN_TEST invokes setUp -- Unity runs
 * setUp first, then the test function, so the mode flag must be written
 * by the caller of RUN_TEST, not from inside the test body. */
#define RUN_TEST_WITH_BIND(bind, fn)                                                                                                                                                                   \
    do {                                                                                                                                                                                               \
        s_setup_bind = (bind);                                                                                                                                                                         \
        RUN_TEST(fn);                                                                                                                                                                                  \
    } while (0)

int main(void) {
    UNITY_BEGIN();
    s_setup_bind = UI_WALKER_FX_BIND_ALL;
    RUN_TEST(test_second_walk_identical);
    s_setup_bind = UI_WALKER_FX_BIND_ALL;
    RUN_TEST(test_viewport_applied);
    /* Death-tests last so a partial bind doesn't leak into the happy-path
     * runs above. tearDown resets s_setup_bind to ALL each time, so each
     * death-test re-arms its own mode here. */
    RUN_TEST_WITH_BIND(UI_WALKER_FX_BIND_ALL & ~UI_WALKER_FX_BIND_ATLAS, test_walk_without_atlas_asserts);
    RUN_TEST_WITH_BIND(UI_WALKER_FX_BIND_ALL & ~UI_WALKER_FX_BIND_SPRITE_MATERIAL, test_walk_without_sprite_material_asserts);
    RUN_TEST_WITH_BIND(UI_WALKER_FX_BIND_ALL & ~UI_WALKER_FX_BIND_TEXT_MATERIAL, test_walk_without_text_material_asserts);
    return UNITY_END();
}
