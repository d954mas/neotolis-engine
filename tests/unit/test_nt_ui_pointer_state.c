#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "input/nt_input.h"
#include "test_helpers/ui_test_arena.h"
#include "ui/nt_ui.h"
#include "unity.h"
/* clang-format on */

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static const nt_ui_create_desc_t s_ui_desc = {.max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT};

void setUp(void) {
    /* gfx pulled in transitively via nt_font/nt_resource; stub is fine. */
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 4, .max_pipelines = 4, .max_buffers = 4, .max_textures = 4, .max_meshes = 4});
    nt_ui_module_init();
}

void tearDown(void) {
    nt_ui_module_shutdown();
    nt_gfx_shutdown();
}

/* Compare two known-finite floats with integer-equivalence: the test
 * inputs (100.0f, 200.0f, 50.0f, 75.0f) are exact float representations
 * so the round-trip through Clay's identity-store should be bit-stable. */
static bool float_eq_bits(float a, float b) {
    uint32_t pa;
    uint32_t pb;
    memcpy(&pa, &a, sizeof pa);
    memcpy(&pb, &b, sizeof pb);
    return pa == pb;
}

/* x/y are forwarded literally; is_down=true is reflected as a
 * pressed-family Clay pointer state. */
static void test_pointer_state_set_from_nt_pointer(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);
    mouse.x = 100.0F;
    mouse.y = 200.0F;
    mouse.buttons[NT_BUTTON_LEFT].is_down = true;

    nt_ui_begin(ctx, 800.0F, 600.0F, 0.0F, &mouse);

    /* Probe Clay's pointer state via NT_TEST_ACCESS getters. */
    TEST_ASSERT_TRUE(float_eq_bits(100.0F, nt_ui_test_clay_pointer_x(ctx)));
    TEST_ASSERT_TRUE(float_eq_bits(200.0F, nt_ui_test_clay_pointer_y(ctx)));
    TEST_ASSERT_EQUAL_INT(1, nt_ui_test_clay_pointer_down(ctx));

    nt_ui_end(ctx);
    nt_ui_destroy_context(ctx);
}

/* is_down=false yields released-family state, position still
 * forwarded literally. */
static void test_pointer_state_button_released(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);
    mouse.x = 50.0F;
    mouse.y = 75.0F;
    mouse.buttons[NT_BUTTON_LEFT].is_down = false;

    nt_ui_begin(ctx, 800.0F, 600.0F, 0.0F, &mouse);

    TEST_ASSERT_TRUE(float_eq_bits(50.0F, nt_ui_test_clay_pointer_x(ctx)));
    TEST_ASSERT_TRUE(float_eq_bits(75.0F, nt_ui_test_clay_pointer_y(ctx)));
    TEST_ASSERT_EQUAL_INT(0, nt_ui_test_clay_pointer_down(ctx));

    nt_ui_end(ctx);
    nt_ui_destroy_context(ctx);
}

/* right and middle button state are NOT forwarded.
 * Setting right/middle is_down=true while left is_down=false should
 * still produce a released Clay state -- only the left button matters. */
static void test_pointer_state_only_left_button_consumed(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);
    mouse.x = 10.0F;
    mouse.y = 20.0F;
    mouse.buttons[NT_BUTTON_LEFT].is_down = false;
    mouse.buttons[NT_BUTTON_RIGHT].is_down = true;
    mouse.buttons[NT_BUTTON_MIDDLE].is_down = true;

    nt_ui_begin(ctx, 800.0F, 600.0F, 0.0F, &mouse);

    TEST_ASSERT_EQUAL_INT(0, nt_ui_test_clay_pointer_down(ctx));

    nt_ui_end(ctx);
    nt_ui_destroy_context(ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pointer_state_set_from_nt_pointer);
    RUN_TEST(test_pointer_state_button_released);
    RUN_TEST(test_pointer_state_only_left_button_consumed);
    return UNITY_END();
}
