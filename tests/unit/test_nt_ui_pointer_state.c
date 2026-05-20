/* tests/unit/test_nt_ui_pointer_state.c -- Plan 52-03
 *
 * Covers CLAY-04 / D-52-16: nt_ui_begin forwards pointer position and
 * left-button state to Clay_SetPointerState. Right/middle buttons and
 * wheel are intentionally NOT consumed (D-52-16 left-button only).
 *
 * Verification strategy: NT_UI_TEST_ACCESS probes nt_ui_test_clay_pointer_
 * x/y/down read ctx->clay->pointerInfo from inside nt_ui.c (where Clay's
 * struct definition is in scope via CLAY_IMPLEMENTATION). Tests pump the
 * pointer through nt_ui_begin, then snapshot the Clay-side state to
 * compare against the input nt_pointer_t.
 *
 * UNITY_EXCLUDE_FLOAT is set globally for tests; float comparisons go
 * through bit-stable uint32 conversion when the inputs are integer-valued
 * floats (matches the test_font.c precedent).
 */

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "input/nt_input.h"
#include "ui/nt_ui.h"
#include "unity.h"
/* clang-format on */

static uint64_t s_arena[NT_UI_DEFAULT_ARENA_SIZE / 8u];

void setUp(void) {
    /* gfx is required because the nt_ui transitive link surface pulls it
     * in via nt_font/nt_resource even though pointer-state tests don't
     * touch GPU state directly. Stub backend is fine. */
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 4, .max_pipelines = 4, .max_buffers = 4, .max_textures = 4, .max_meshes = 4});
}

void tearDown(void) { nt_gfx_shutdown(); }

/* Compare two known-finite floats with integer-equivalence: the test
 * inputs (100.0f, 200.0f, 50.0f, 75.0f) are exact float representations
 * so the round-trip through Clay's identity-store should be bit-stable. */
static bool float_eq_bits(float a, float b) {
    uint32_t pa, pb;
    memcpy(&pa, &a, sizeof pa);
    memcpy(&pb, &b, sizeof pb);
    return pa == pb;
}

/* CLAY-04: x/y are forwarded literally; is_down=true is reflected as a
 * pressed-family Clay pointer state. */
static void test_pointer_state_set_from_nt_pointer(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);
    mouse.x = 100.0f;
    mouse.y = 200.0f;
    mouse.buttons[NT_BUTTON_LEFT].is_down = true;

    nt_ui_begin(ctx, 800.0f, 600.0f, &mouse);

    /* Probe Clay's pointer state via NT_UI_TEST_ACCESS getters. */
    TEST_ASSERT_TRUE(float_eq_bits(100.0f, nt_ui_test_clay_pointer_x(ctx)));
    TEST_ASSERT_TRUE(float_eq_bits(200.0f, nt_ui_test_clay_pointer_y(ctx)));
    TEST_ASSERT_EQUAL_INT(1, nt_ui_test_clay_pointer_down(ctx));

    nt_ui_end(ctx);
    nt_ui_destroy_context(ctx);
}

/* CLAY-04: is_down=false yields released-family state, position still
 * forwarded literally. */
static void test_pointer_state_button_released(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);
    mouse.x = 50.0f;
    mouse.y = 75.0f;
    mouse.buttons[NT_BUTTON_LEFT].is_down = false;

    nt_ui_begin(ctx, 800.0f, 600.0f, &mouse);

    TEST_ASSERT_TRUE(float_eq_bits(50.0f, nt_ui_test_clay_pointer_x(ctx)));
    TEST_ASSERT_TRUE(float_eq_bits(75.0f, nt_ui_test_clay_pointer_y(ctx)));
    TEST_ASSERT_EQUAL_INT(0, nt_ui_test_clay_pointer_down(ctx));

    nt_ui_end(ctx);
    nt_ui_destroy_context(ctx);
}

/* CLAY-04 + D-52-16: right and middle button state are NOT forwarded.
 * Setting right/middle is_down=true while left is_down=false should
 * still produce a released Clay state -- only the left button matters. */
static void test_pointer_state_only_left_button_consumed(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);
    mouse.x = 10.0f;
    mouse.y = 20.0f;
    mouse.buttons[NT_BUTTON_LEFT].is_down = false;
    mouse.buttons[NT_BUTTON_RIGHT].is_down = true;
    mouse.buttons[NT_BUTTON_MIDDLE].is_down = true;

    nt_ui_begin(ctx, 800.0f, 600.0f, &mouse);

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
