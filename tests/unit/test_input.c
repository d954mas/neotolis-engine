#include "input/nt_input.h"
#include "unity.h"
#include "window/nt_window.h"

#include <math.h>

static bool float_near(float a, float b, float epsilon) { return fabsf(a - b) <= epsilon; }

void setUp(void) {
    g_nt_window = (nt_window_t){.max_dpr = 2.0F, .dpr = 1.0F};
    nt_input_init();
}

void tearDown(void) { nt_input_shutdown(); }

/* ---- Key tests ---- */

void test_key_enum_count(void) {
    /* NT_KEY_COUNT should be 69 (A-Z=26 + 0-9=10 + arrows=4 +
       Space,Enter,Escape,Tab,Backspace=5 + LShift..RAlt=6 +
       F1-F12=12 + Delete,Insert,Home,End,PageUp,PageDown=6 = 69) */
    TEST_ASSERT_EQUAL_INT(69, NT_KEY_COUNT);
}

void test_key_down_after_set(void) {
    nt_input_set_key(NT_KEY_A, true);
    /* Need poll to update previous, but is_down reads current */
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
}

void test_key_pressed_edge(void) {
    nt_input_set_key(NT_KEY_W, true);
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_W));
}

void test_key_released_edge(void) {
    nt_input_set_key(NT_KEY_S, true);
    nt_input_poll(); /* clears pressed edge */
    nt_input_set_key(NT_KEY_S, false);
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_S));
}

void test_key_pressed_clears_next_frame(void) {
    nt_input_set_key(NT_KEY_D, true);
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_D));
    /* Next frame: poll clears edges, key still held */
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_key_is_pressed(NT_KEY_D));
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_D));
}

void test_any_key_pressed(void) {
    TEST_ASSERT_FALSE(nt_input_any_key_pressed());
    nt_input_set_key(NT_KEY_SPACE, true);
    TEST_ASSERT_TRUE(nt_input_any_key_pressed());
}

/* ---- Pointer tests ---- */

void test_pointer_slot_alloc(void) {
    g_nt_window.dpr = 1.0F;
    nt_input_pointer_down(42, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    TEST_ASSERT_EQUAL_UINT32(42, g_nt_input.pointers[0].id);
    TEST_ASSERT_EQUAL_UINT8(NT_POINTER_MOUSE, g_nt_input.pointers[0].type);
}

void test_pointer_slot_find_by_id(void) {
    g_nt_window.dpr = 1.0F;
    nt_input_pointer_down(42, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    nt_input_pointer_move(42, 150.0F, 250.0F, 0.5F, 1);
    TEST_ASSERT_TRUE(float_near(150.0F, g_nt_input.pointers[0].x, 1e-3F));
    TEST_ASSERT_TRUE(float_near(250.0F, g_nt_input.pointers[0].y, 1e-3F));
}

void test_pointer_slot_dealloc(void) {
    g_nt_window.dpr = 1.0F;
    nt_input_pointer_down(42, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    /* Verify pointer was activated first */
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    nt_input_pointer_up(42);
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
}

void test_pointer_delta(void) {
    g_nt_window.dpr = 1.0F;
    nt_input_pointer_down(1, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    nt_input_poll(); /* clears initial dx/dy */
    nt_input_pointer_move(1, 120.0F, 230.0F, 0.5F, 1);
    /* Delta computed immediately in pointer_move */
    TEST_ASSERT_TRUE(float_near(20.0F, g_nt_input.pointers[0].dx, 1e-3F));
    TEST_ASSERT_TRUE(float_near(30.0F, g_nt_input.pointers[0].dy, 1e-3F));
}

void test_pointer_button_pressed(void) {
    g_nt_window.dpr = 1.0F;
    nt_input_pointer_down(1, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT].is_pressed);
}

void test_pointer_button_released(void) {
    g_nt_window.dpr = 1.0F;
    nt_input_pointer_down(1, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    nt_input_poll(); /* clears pressed edge */
    nt_input_pointer_up(1);
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT].is_released);
}

/* ---- Coordinate mapping tests ---- */

void test_coord_mapping_1x(void) {
    g_nt_window.dpr = 1.0F;
    nt_input_pointer_down(1, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    TEST_ASSERT_TRUE(float_near(100.0F, g_nt_input.pointers[0].x, 1e-3F));
    TEST_ASSERT_TRUE(float_near(200.0F, g_nt_input.pointers[0].y, 1e-3F));
}

void test_coord_mapping_2x(void) {
    g_nt_window.dpr = 2.0F;
    nt_input_pointer_down(1, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    TEST_ASSERT_TRUE(float_near(200.0F, g_nt_input.pointers[0].x, 1e-3F));
    TEST_ASSERT_TRUE(float_near(400.0F, g_nt_input.pointers[0].y, 1e-3F));
}

void test_coord_mapping_fractional(void) {
    g_nt_window.dpr = 1.5F;
    nt_input_pointer_down(1, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    TEST_ASSERT_TRUE(float_near(150.0F, g_nt_input.pointers[0].x, 1e-3F));
    TEST_ASSERT_TRUE(float_near(300.0F, g_nt_input.pointers[0].y, 1e-3F));
}

/* ---- Mouse convenience test ---- */

void test_mouse_convenience(void) {
    g_nt_window.dpr = 1.0F;
    nt_input_pointer_down(1, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_key_enum_count);
    RUN_TEST(test_key_down_after_set);
    RUN_TEST(test_key_pressed_edge);
    RUN_TEST(test_key_released_edge);
    RUN_TEST(test_key_pressed_clears_next_frame);
    RUN_TEST(test_any_key_pressed);
    RUN_TEST(test_pointer_slot_alloc);
    RUN_TEST(test_pointer_slot_find_by_id);
    RUN_TEST(test_pointer_slot_dealloc);
    RUN_TEST(test_pointer_delta);
    RUN_TEST(test_pointer_button_pressed);
    RUN_TEST(test_pointer_button_released);
    RUN_TEST(test_coord_mapping_1x);
    RUN_TEST(test_coord_mapping_2x);
    RUN_TEST(test_coord_mapping_fractional);
    RUN_TEST(test_mouse_convenience);
    return UNITY_END();
}
