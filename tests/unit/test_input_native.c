#include "input/nt_input_internal.h"
#include "unity.h"
#include "window/nt_window.h"

static bool float_near(float a, float b, float eps) { return (a - b) <= eps && (b - a) <= eps; }

void setUp(void) {
    nt_input_init();
    g_nt_window.dpr = 1.0F;
}

void tearDown(void) { nt_input_shutdown(); }

/* Held H and mouse; B, a move and a wheel staged before the loss; then GLFW's synthetic H release,
   A and a mouse press. */
static void lose_focus_between_events(void) {
    nt_input_buffer_key(NT_KEY_H, true);
    nt_input_buffer_mouse(10.0, 10.0, 1);
    nt_input_poll();
    nt_input_buffer_key(NT_KEY_B, true);
    nt_input_buffer_char_event('b');
    nt_input_buffer_mouse(30.0, 30.0, 0);
    nt_input_buffer_wheel(0.0F, 1.0F);
    nt_input_buffer_focus_lost();
    nt_input_buffer_key(NT_KEY_H, false);
    nt_input_buffer_key(NT_KEY_A, true);
    nt_input_buffer_char_event('a');
    nt_input_buffer_mouse(20.0, 20.0, 1);
    nt_input_poll();
}

void test_focus_lost_poll_only_releases(void) {
    lose_focus_between_events();
    uint32_t cp = 0;
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_H));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

void test_events_after_focus_lost_arrive_next_poll(void) {
    lose_focus_between_events();
    nt_input_poll();
    uint32_t cp = 0;
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_A));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_B));
    TEST_ASSERT_FALSE(nt_input_key_is_released(NT_KEY_H));
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_TRUE(float_near(20.0F, g_nt_input.pointers[0].x, 1e-6F));
    TEST_ASSERT_TRUE(float_near(0.0F, g_nt_input.pointers[0].wheel_dy, 1e-6F));
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32('a', cp);
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

/* The window callback drops GLFW's synthetic release, so the loss itself must forget the held mask. */
void test_press_and_move_after_focus_lost_keep_delta(void) {
    nt_input_buffer_mouse(10.0, 10.0, 1);
    nt_input_poll();
    nt_input_buffer_focus_lost();
    nt_input_poll();
    nt_input_poll();
    nt_input_buffer_mouse(10.0, 10.0, 1);
    nt_input_buffer_mouse(20.0, 10.0, 1);
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
    TEST_ASSERT_TRUE(float_near(10.0F, g_nt_input.pointers[0].dx, 1e-6F));
}

void test_shutdown_drops_staged_events(void) {
    nt_input_buffer_key(NT_KEY_A, true);
    nt_input_shutdown();
    nt_input_init();
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_focus_lost_poll_only_releases);
    RUN_TEST(test_events_after_focus_lost_arrive_next_poll);
    RUN_TEST(test_press_and_move_after_focus_lost_keep_delta);
    RUN_TEST(test_shutdown_drops_staged_events);
    return UNITY_END();
}
