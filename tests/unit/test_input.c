#include "input/nt_input_feed.h"
#include "test_helpers/nt_input_test_frame.h"
#include "unity.h"

static bool float_near(float a, float b, float eps) { return (a - b) <= eps && (b - a) <= eps; }

void setUp(void) { nt_input_init(); }

void tearDown(void) { nt_input_shutdown(); }

/* ---- Publish: events apply to the next frame, the poll publishes it ---- */

void test_event_invisible_until_poll(void) {
    nt_input_feed(nt_test_key(NT_KEY_A, true));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
}

/* A press and a release between two polls: both edges are published once. */
void test_press_and_release_in_one_frame(void) {
    nt_input_feed(nt_test_key(NT_KEY_E, true));
    nt_input_feed(nt_test_key(NT_KEY_E, false));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_E));
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_E));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_E));
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_key_is_pressed(NT_KEY_E));
    TEST_ASSERT_FALSE(nt_input_key_is_released(NT_KEY_E));
}

/* ---- Key tests ---- */

void test_key_enum_count(void) { TEST_ASSERT_EQUAL_INT(69, NT_KEY_COUNT); }

void test_key_released_edge(void) {
    nt_input_feed(nt_test_key(NT_KEY_S, true));
    nt_input_poll();
    nt_input_feed(nt_test_key(NT_KEY_S, false));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_S));
}

void test_key_pressed_clears_next_frame(void) {
    nt_input_feed(nt_test_key(NT_KEY_D, true));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_D));
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_key_is_pressed(NT_KEY_D));
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_D));
}

void test_any_key_pressed(void) {
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_any_key_pressed());
    nt_input_feed(nt_test_key(NT_KEY_SPACE, true));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_any_key_pressed());
}

/* ---- Focus loss: applied in order with the events around it ---- */

void test_focus_lost_releases_held_keys(void) {
    nt_input_feed(nt_test_key(NT_KEY_W, true));
    nt_input_feed(nt_test_key(NT_KEY_A, true));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_W));
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_W));
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_A));
}

void test_events_after_focus_lost_apply_in_the_same_frame(void) {
    nt_input_feed(nt_test_key(NT_KEY_H, true));
    nt_input_feed(nt_test_contact_down(7, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_feed(nt_test_key(NT_KEY_A, true));
    nt_input_feed(nt_test_contact_down(8, 9.0F, 9.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_H));
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_A));
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_TRUE(g_nt_input.pointers[2].buttons[NT_BUTTON_LEFT].is_pressed);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].active);
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
}

/* A release and a new press of one button in one frame read pressed and released together. */
void test_focus_lost_then_press_same_key(void) {
    nt_input_feed(nt_test_key(NT_KEY_H, true));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_feed(nt_test_key(NT_KEY_H, true));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_H));
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_H));
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_H));
}

void test_focus_lost_then_press_same_button(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    const nt_button_state_t b = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(b.is_released && b.is_pressed && b.is_down);
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

void test_mouse_focus_lost_drops_pressure(void) {
    nt_input_feed(nt_test_mouse(10.0F, 10.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_TRUE(float_near(0.0F, g_nt_input.pointers[0].pressure, 1e-6F));
}

void test_init_forgets_unpublished_events(void) {
    nt_input_feed(nt_test_key(NT_KEY_A, true));
    nt_input_init();
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
}

/* The published frame never changes between polls, whatever arrives. */
void test_published_frame_stable_between_polls(void) {
    nt_input_feed(nt_test_key(NT_KEY_A, true));
    nt_input_feed(nt_test_contact_down(3, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_key(NT_KEY_A, false));
    nt_input_feed(nt_test_contact_up(3));
    nt_input_feed(nt_test_mouse(9.0F, 9.0F, 1));
    nt_input_feed(nt_test_wheel(0.0F, 1.0F));
    nt_input_feed(nt_test_focus_lost());
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_A));
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_down);
}

/* A click and a tap between two polls are both readable: pressed and released together. */
void test_click_and_tap_in_one_frame(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_feed(nt_test_contact_down(9, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_feed(nt_test_contact_up(9));
    nt_input_poll();
    const nt_button_state_t mouse = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    const nt_button_state_t tap = g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(mouse.is_pressed && mouse.is_released && !mouse.is_down);
    TEST_ASSERT_TRUE(tap.is_pressed && tap.is_released && !tap.is_down);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].active);
}

void test_mouse_deactivates_after_focus_lost(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active); /* the loss frame is still readable */
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
}

void test_mouse_move_after_focus_lost_keeps_mouse(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_feed(nt_test_mouse(6.0F, 5.0F, 0));
    nt_input_poll();
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
}

/* ---- Mouse: pointers[0], kept across a release ---- */

void test_mouse_move_activates_slot_0(void) {
    nt_input_feed(nt_test_mouse(50.0F, 60.0F, 0));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    TEST_ASSERT_EQUAL_UINT8(NT_POINTER_MOUSE, g_nt_input.pointers[0].type);
    TEST_ASSERT_TRUE(float_near(50.0F, g_nt_input.pointers[0].x, 1e-3F));
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].active);
}

void test_mouse_release_keeps_mouse(void) {
    nt_input_feed(nt_test_mouse(1.0F, 1.0F, 1));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
    nt_input_feed(nt_test_mouse(1.0F, 1.0F, 0));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
}

void test_mouse_leave_deactivates_next_poll(void) {
    nt_input_feed(nt_test_mouse(1.0F, 1.0F, 0));
    nt_input_poll();
    nt_input_feed(nt_test_leave());
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
}

void test_mouse_leave_ignored_while_held(void) {
    nt_input_feed(nt_test_mouse(1.0F, 1.0F, 1));
    nt_input_feed(nt_test_leave());
    nt_input_poll();
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* Leave and come back in one frame: the release before the leave stays readable. */
void test_mouse_back_before_leave_keeps_release(void) {
    nt_input_feed(nt_test_mouse(1.0F, 1.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(1.0F, 1.0F, 0));
    nt_input_feed(nt_test_leave());
    nt_input_feed(nt_test_mouse(2.0F, 2.0F, 0));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
}

/* Out at one edge and in at another within one frame: no jump in the delta. */
void test_mouse_back_before_leave_no_jump(void) {
    nt_input_feed(nt_test_mouse(10.0F, 10.0F, 0));
    nt_input_poll();
    nt_input_feed(nt_test_leave());
    nt_input_feed(nt_test_mouse(500.0F, 10.0F, 0));
    nt_input_poll();
    TEST_ASSERT_TRUE(float_near(0.0F, g_nt_input.pointers[0].dx, 1e-3F));
}

void test_mouse_delta(void) {
    nt_input_feed(nt_test_mouse(100.0F, 200.0F, 0));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(110.0F, 210.0F, 0));
    nt_input_feed(nt_test_mouse(120.0F, 230.0F, 0));
    nt_input_poll();
    TEST_ASSERT_TRUE(float_near(20.0F, g_nt_input.pointers[0].dx, 1e-3F));
    TEST_ASSERT_TRUE(float_near(30.0F, g_nt_input.pointers[0].dy, 1e-3F));
    nt_input_poll();
    TEST_ASSERT_TRUE(float_near(0.0F, g_nt_input.pointers[0].dx, 1e-3F));
}

void test_wheel_needs_mouse_over_window(void) {
    nt_input_feed(nt_test_wheel(0.0F, 1.0F));
    nt_input_poll();
    TEST_ASSERT_TRUE(float_near(0.0F, g_nt_input.pointers[0].wheel_dy, 1e-3F));
    nt_input_feed(nt_test_mouse(1.0F, 1.0F, 0));
    nt_input_feed(nt_test_wheel(0.0F, 1.0F));
    nt_input_feed(nt_test_wheel(0.0F, 0.5F));
    nt_input_poll();
    TEST_ASSERT_TRUE(float_near(1.5F, g_nt_input.pointers[0].wheel_dy, 1e-3F));
}

/* ---- Contacts: slots 1.., from down to up ---- */

void test_contact_never_takes_slot_0(void) {
    nt_input_feed(nt_test_contact_down(0, 100.0F, 200.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].active);
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_input.pointers[1].id);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_pressed);
}

void test_contact_move_and_delta(void) {
    nt_input_feed(nt_test_contact_down(42, 100.0F, 200.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_contact_move(42, 120.0F, 230.0F, 0.5F, 1));
    nt_input_poll();
    TEST_ASSERT_TRUE(float_near(120.0F, g_nt_input.pointers[1].x, 1e-3F));
    TEST_ASSERT_TRUE(float_near(20.0F, g_nt_input.pointers[1].dx, 1e-3F));
    TEST_ASSERT_TRUE(float_near(30.0F, g_nt_input.pointers[1].dy, 1e-3F));
}

void test_contact_up_deactivates_next_poll(void) {
    nt_input_feed(nt_test_contact_down(42, 100.0F, 200.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_contact_up(42));
    nt_input_poll();
    /* Slot stays active for one frame so game can read is_released */
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].active);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_released);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].active);
}

/* Lift and a new down with the same id in one frame: the lifted contact keeps its release edge. */
void test_contact_same_id_down_after_up_new_slot(void) {
    nt_input_feed(nt_test_contact_down(4, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_contact_up(4));
    nt_input_feed(nt_test_contact_down(4, 9.0F, 9.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_TRUE(g_nt_input.pointers[2].buttons[NT_BUTTON_LEFT].is_pressed);
}

void test_contact_move_without_down_ignored(void) {
    nt_input_feed(nt_test_contact_move(5, 50.0F, 60.0F, 0.0F, 0)); /* a hovering pen */
    nt_input_poll();
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        TEST_ASSERT_FALSE(g_nt_input.pointers[i].active);
    }
}

void test_mouse_convenience(void) {
    nt_input_feed(nt_test_mouse(100.0F, 200.0F, 1));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    nt_input_feed(nt_test_contact_down(3, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_feed(nt_test_mouse(100.0F, 200.0F, 0));
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT)); /* reads the mouse, never a contact */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_event_invisible_until_poll);
    RUN_TEST(test_press_and_release_in_one_frame);
    RUN_TEST(test_key_enum_count);
    RUN_TEST(test_key_released_edge);
    RUN_TEST(test_key_pressed_clears_next_frame);
    RUN_TEST(test_any_key_pressed);
    RUN_TEST(test_focus_lost_releases_held_keys);
    RUN_TEST(test_events_after_focus_lost_apply_in_the_same_frame);
    RUN_TEST(test_focus_lost_then_press_same_key);
    RUN_TEST(test_focus_lost_then_press_same_button);
    RUN_TEST(test_mouse_focus_lost_drops_pressure);
    RUN_TEST(test_init_forgets_unpublished_events);
    RUN_TEST(test_published_frame_stable_between_polls);
    RUN_TEST(test_click_and_tap_in_one_frame);
    RUN_TEST(test_mouse_deactivates_after_focus_lost);
    RUN_TEST(test_mouse_move_after_focus_lost_keeps_mouse);
    RUN_TEST(test_mouse_move_activates_slot_0);
    RUN_TEST(test_mouse_release_keeps_mouse);
    RUN_TEST(test_mouse_leave_deactivates_next_poll);
    RUN_TEST(test_mouse_leave_ignored_while_held);
    RUN_TEST(test_mouse_back_before_leave_keeps_release);
    RUN_TEST(test_mouse_back_before_leave_no_jump);
    RUN_TEST(test_mouse_delta);
    RUN_TEST(test_wheel_needs_mouse_over_window);
    RUN_TEST(test_contact_never_takes_slot_0);
    RUN_TEST(test_contact_move_and_delta);
    RUN_TEST(test_contact_up_deactivates_next_poll);
    RUN_TEST(test_contact_same_id_down_after_up_new_slot);
    RUN_TEST(test_contact_move_without_down_ignored);
    RUN_TEST(test_mouse_convenience);
    return UNITY_END();
}
