#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "graphics/nt_gfx.h"
#include "renderers/nt_sprite_renderer.h"
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

/* two walks against same ctx+target produce identical probe state. */
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
    const uint32_t elements1 = nt_ui_get_last_walk_element_count(s_fx.ctx);
    /* Read but don't compare draw-call delta -- walking once already
     * incurs draw calls; a second walk will too, so the EXACT delta-
     * to-delta count is what we compare. */
    const uint32_t delta1 = nt_ui_get_last_walk_draw_calls(s_fx.ctx);

    nt_ui_walk(s_fx.ctx, &target);

    int vp2[4];
    nt_gfx_test_viewport_rect(vp2);
    const uint32_t elements2 = nt_ui_get_last_walk_element_count(s_fx.ctx);
    const uint32_t delta2 = nt_ui_get_last_walk_draw_calls(s_fx.ctx);

    TEST_ASSERT_EQUAL_INT_ARRAY(vp1, vp2, 4);
    TEST_ASSERT_EQUAL_UINT32(elements1, elements2);
    TEST_ASSERT_EQUAL_UINT32(delta1, delta2);
}

/* walker entry applies target->viewport via nt_gfx_set_viewport. */
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

/* walk without atlas set asserts. */
static void test_walk_without_atlas_asserts(void) {
    /* Mode flag was set before Unity invoked setUp -- the atlas setter
     * was skipped, so ctx->atlas.id == 0. */
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* death-test: walk without sprite material asserts. */
static void test_walk_without_sprite_material_asserts(void) {
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* walk without text material asserts at walker entry. */
static void test_walk_without_text_material_asserts(void) {
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* walk before begin/end must assert (frozen_cmds is zero-init). */
static void test_walk_without_end_asserts(void) {
    /* Fresh ctx so we don't inherit injected frozen_cmds. */
    alignas(NT_UI_ARENA_ALIGN) static uint8_t fresh_arena[NT_UI_DEFAULT_ARENA_SIZE];
    const nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    nt_ui_context_t *fresh = nt_ui_create_context(fresh_arena, sizeof fresh_arena, &desc);
    TEST_ASSERT_NOT_NULL(fresh);
    nt_ui_set_atlas_white_region(fresh, s_fx.atlas.handle, s_fx.atlas.white_region_idx);
    nt_ui_set_sprite_material(fresh, s_fx.sprite_material);
    nt_ui_set_text_material(fresh, s_fx.text_material);
    nt_ui_set_custom_handler(fresh, NULL, NULL);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(fresh, &target));
    nt_ui_destroy_context(fresh);
}

/* Negative origin is a caller bug ((int) cast would be UB). */
static void test_walk_negative_viewport_origin_asserts(void) {
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {-1.0F, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* Negative w/h is a caller bug; zero is the legitimate "small" value. */
static void test_walk_negative_viewport_size_asserts(void) {
    inject_frozen_cmds(0);
    nt_ui_target_t neg_w = {.viewport = {0, 0, -1.0F, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &neg_w));
    nt_ui_target_t neg_h = {.viewport = {0, 0, 800, -1.0F}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &neg_h));
}

/* Zero viewport is legitimate (minimized tab); walker silent no-ops. */
static void test_walk_zero_viewport_silent_noop(void) {
    inject_frozen_cmds(0);
    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t zero_w = {.viewport = {0.0F, 0.0F, 0.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &zero_w);
    nt_ui_target_t zero_h = {.viewport = {0.0F, 0.0F, 800.0F, 0.0F}};
    nt_ui_walk(s_fx.ctx, &zero_h);
    nt_ui_target_t zero_both = {.viewport = {0.0F, 0.0F, 0.0F, 0.0F}};
    nt_ui_walk(s_fx.ctx, &zero_both);
    TEST_ASSERT_EQUAL_UINT32(calls_before, nt_sprite_renderer_test_draw_call_count());
}

/* Zero-viewport walk must overwrite stats (not leave stale prior frame). */
static void test_walk_zero_viewport_resets_stats(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    s_test_cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    inject_frozen_cmds(1);
    nt_ui_target_t normal = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &normal);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_element_count(s_fx.ctx));

    nt_ui_target_t zero = {.viewport = {0, 0, 0.0F, 0.0F}};
    nt_ui_walk(s_fx.ctx, &zero);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_element_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* Set s_setup_bind BEFORE RUN_TEST: setUp fires first, must see the flag. */
#define RUN_TEST_WITH_BIND(bind, fn)                                                                                                                                                                   \
    do {                                                                                                                                                                                               \
        s_setup_bind = (bind);                                                                                                                                                                         \
        RUN_TEST(fn);                                                                                                                                                                                  \
    } while (0)

int main(void) {
    UNITY_BEGIN();
    s_setup_bind = UI_WALKER_FX_BIND_ALL;
    RUN_TEST(test_walk_without_end_asserts);
    s_setup_bind = UI_WALKER_FX_BIND_ALL;
    RUN_TEST(test_walk_negative_viewport_origin_asserts);
    s_setup_bind = UI_WALKER_FX_BIND_ALL;
    RUN_TEST(test_walk_negative_viewport_size_asserts);
    s_setup_bind = UI_WALKER_FX_BIND_ALL;
    RUN_TEST(test_walk_zero_viewport_silent_noop);
    s_setup_bind = UI_WALKER_FX_BIND_ALL;
    RUN_TEST(test_walk_zero_viewport_resets_stats);
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
