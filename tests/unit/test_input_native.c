#include "input/nt_input_internal.h"
#include "unity.h"
#include "window/nt_window.h"

void setUp(void) {
    nt_input_init();
    g_nt_window.dpr = 1.0F;
}

void tearDown(void) { nt_input_shutdown(); }

/* Held H and mouse; B staged before the loss, A and a mouse press staged after it. */
static void lose_focus_between_events(void) {
    nt_input_buffer_key(NT_KEY_H, true);
    nt_input_buffer_mouse(10.0, 10.0, 1);
    nt_input_poll();
    nt_input_buffer_key(NT_KEY_B, true);
    nt_input_buffer_char_event('b');
    nt_input_buffer_focus_lost();
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
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32('a', cp);
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_focus_lost_poll_only_releases);
    RUN_TEST(test_events_after_focus_lost_arrive_next_poll);
    return UNITY_END();
}
