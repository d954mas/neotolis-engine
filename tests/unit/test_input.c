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

void test_key_enum_count(void) { TEST_ASSERT_EQUAL_INT(98, NT_KEY_COUNT); }

void test_key_repeat_is_no_new_press(void) {
    nt_input_feed(nt_test_key(NT_KEY_K, true));
    nt_input_poll();
    nt_input_feed(nt_test_key(NT_KEY_K, true)); /* GLFW_REPEAT */
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_K));
    TEST_ASSERT_FALSE(nt_input_key_is_pressed(NT_KEY_K));
}

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

/* A release and a new press of one key in one frame read pressed and released together. */
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

/* A pointer button shows the release first: the press that followed it waits for the next frame. */
void test_focus_lost_then_press_same_button(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_button_state_t b = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(b.is_released && !b.is_pressed && !b.is_down);
    nt_input_poll();
    b = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    TEST_ASSERT_TRUE(b.is_pressed && b.is_down && !b.is_released);
}

/* Up at one point, a hover and down at another between two polls: the release is published where it
   happened; the press where it happened and the motion up to it one frame later. Motion after the waiting
   press is dropped (the next move reports the pointer), and motion in the press frame is published after it. */
void test_release_then_press_takes_two_frames(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(6.0F, 5.0F, 0));
    nt_input_feed(nt_test_mouse(50.0F, 20.0F, 0));
    nt_input_feed(nt_test_mouse(90.0F, 40.0F, 1));
    nt_input_feed(nt_test_mouse(92.0F, 40.0F, 1));
    nt_input_poll();
    const nt_pointer_t *m = &g_nt_input.pointers[0];
    TEST_ASSERT_TRUE(m->buttons[NT_BUTTON_LEFT].is_released && !m->buttons[NT_BUTTON_LEFT].is_pressed && !m->buttons[NT_BUTTON_LEFT].is_down);
    TEST_ASSERT_EQUAL_INT(6, (int)m->x);
    TEST_ASSERT_EQUAL_INT(5, (int)m->y);
    TEST_ASSERT_EQUAL_INT(1, (int)m->dx);
    nt_input_feed(nt_test_mouse(95.0F, 40.0F, 1));
    nt_input_poll();
    TEST_ASSERT_TRUE(m->buttons[NT_BUTTON_LEFT].is_pressed && m->buttons[NT_BUTTON_LEFT].is_down && !m->buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_EQUAL_INT(90, (int)m->x);
    TEST_ASSERT_EQUAL_INT(40, (int)m->y);
    TEST_ASSERT_EQUAL_INT(84, (int)m->dx);
    TEST_ASSERT_EQUAL_INT(35, (int)m->dy);
    nt_input_poll();
    TEST_ASSERT_TRUE(m->buttons[NT_BUTTON_LEFT].is_down && !m->buttons[NT_BUTTON_LEFT].is_pressed);
    TEST_ASSERT_EQUAL_INT(95, (int)m->x);
    TEST_ASSERT_EQUAL_INT(5, (int)m->dx);
}

/* A press and a move before the poll: the press frame shows the pointer where it pressed; the move is
   published the frame after, as motion. */
void test_press_is_published_where_it_happened(void) {
    nt_input_feed(nt_test_mouse(20.0F, 50.0F, 0));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(20.0F, 50.0F, 1));
    nt_input_feed(nt_test_mouse(180.0F, 60.0F, 1));
    nt_input_poll();
    const nt_pointer_t *m = &g_nt_input.pointers[0];
    TEST_ASSERT_TRUE(m->buttons[NT_BUTTON_LEFT].is_pressed && m->buttons[NT_BUTTON_LEFT].is_down);
    TEST_ASSERT_EQUAL_INT(20, (int)m->x);
    TEST_ASSERT_EQUAL_INT(50, (int)m->y);
    TEST_ASSERT_EQUAL_INT(0, (int)m->dx);
    nt_input_poll();
    TEST_ASSERT_TRUE(m->buttons[NT_BUTTON_LEFT].is_down && !m->buttons[NT_BUTTON_LEFT].is_pressed);
    TEST_ASSERT_EQUAL_INT(180, (int)m->x);
    TEST_ASSERT_EQUAL_INT(160, (int)m->dx);
    TEST_ASSERT_EQUAL_INT(10, (int)m->dy);
}

/* A contact's down and its first move before the poll: the down is published where it landed. */
void test_contact_down_is_published_where_it_landed(void) {
    nt_input_feed(nt_test_contact_down(42, 20.0F, 50.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_feed(nt_test_contact_move(42, 180.0F, 50.0F, 0.5F, 1));
    nt_input_poll();
    const nt_pointer_t *c = &g_nt_input.pointers[1];
    TEST_ASSERT_TRUE(c->buttons[NT_BUTTON_LEFT].is_pressed);
    TEST_ASSERT_EQUAL_INT(20, (int)c->x);
    TEST_ASSERT_EQUAL_INT(0, (int)c->dx);
    nt_input_poll();
    TEST_ASSERT_EQUAL_INT(180, (int)c->x);
    TEST_ASSERT_EQUAL_INT(160, (int)c->dx);
}

/* A bot's button event has no position: a waiting press keeps the one it was made at. */
void test_waiting_press_keeps_its_position_through_a_buttons_event(void) {
    nt_input_inject(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_inject(nt_test_mouse_buttons(0));
    nt_input_inject(nt_test_mouse(90.0F, 40.0F, 1));
    nt_input_inject(nt_test_mouse_buttons(0));
    nt_input_poll();
    nt_input_poll();
    const nt_pointer_t *m = &g_nt_input.pointers[0];
    TEST_ASSERT_TRUE(m->buttons[NT_BUTTON_LEFT].is_pressed && m->buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_EQUAL_INT(90, (int)m->x);
    TEST_ASSERT_EQUAL_INT(40, (int)m->y);
}

/* A waiting tap is published where it was made, not where the mouse went after it. */
void test_waiting_tap_keeps_its_point(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_feed(nt_test_mouse(90.0F, 40.0F, 1));
    nt_input_feed(nt_test_mouse(90.0F, 40.0F, 0));
    nt_input_feed(nt_test_mouse(300.0F, 200.0F, 0));
    nt_input_poll();
    nt_input_poll();
    const nt_pointer_t *m = &g_nt_input.pointers[0];
    TEST_ASSERT_TRUE(m->buttons[NT_BUTTON_LEFT].is_pressed && m->buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_EQUAL_INT(90, (int)m->x);
    TEST_ASSERT_EQUAL_INT(40, (int)m->y);
}

/* A mouse that left after its release and came back in the same frame: the release is still published
   where it happened, the new place one frame later with no delta for the jump. */
void test_mouse_back_after_release_reports_no_delta(void) {
    nt_input_feed(nt_test_mouse(10.0F, 10.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(10.0F, 10.0F, 0));
    nt_input_feed(nt_test_leave());
    nt_input_feed(nt_test_mouse(500.0F, 10.0F, 0));
    nt_input_poll();
    const nt_pointer_t *m = &g_nt_input.pointers[0];
    TEST_ASSERT_TRUE(m->active && m->buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_EQUAL_INT(10, (int)m->x);
    TEST_ASSERT_EQUAL_INT(0, (int)m->dx);
    nt_input_poll();
    TEST_ASSERT_EQUAL_INT(500, (int)m->x);
    TEST_ASSERT_EQUAL_INT(0, (int)m->dx);
    nt_input_feed(nt_test_mouse(510.0F, 10.0F, 0));
    nt_input_poll();
    TEST_ASSERT_EQUAL_INT(10, (int)m->dx); /* tracked again */
}

/* A waiting tap is published before a mouse that left in the same frame is gone. */
void test_waiting_tap_survives_a_leave(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_feed(nt_test_leave());
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT) && !nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT) && nt_input_mouse_is_released(NT_BUTTON_LEFT));
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
}

/* A tap that follows a release in the same frame is published whole one frame later; a press that
   follows a tap from an up button waits the same way. */
void test_release_then_tap_and_tap_then_press(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_poll();
    nt_button_state_t b = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(b.is_released && !b.is_pressed && !b.is_down);
    nt_input_poll();
    b = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(b.is_pressed && b.is_released && !b.is_down);
    nt_input_poll();
    b = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(!b.is_pressed && !b.is_released && !b.is_down);

    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    b = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(b.is_pressed && b.is_released && !b.is_down);
    nt_input_poll();
    b = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(b.is_pressed && b.is_down && !b.is_released);
}

/* A press waiting for the next frame holds the mouse like a held button: a leave does not drop it. */
void test_waiting_press_keeps_the_mouse_through_a_leave(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_feed(nt_test_leave());
    nt_input_poll();
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT) && nt_input_mouse_is_down(NT_BUTTON_LEFT));
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

void test_contacts_fill_slots_1_to_7(void) {
    for (uint32_t id = 1; id <= NT_INPUT_MAX_POINTERS; id++) {
        nt_input_feed(nt_test_contact_down(id, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    }
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
    for (uint32_t i = 1; i < NT_INPUT_MAX_POINTERS; i++) {
        TEST_ASSERT_EQUAL_UINT32(i, g_nt_input.pointers[i].id); /* the 8th down finds no slot */
    }
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

/* ---- Cancel: a release that did not happen ---- */

void test_contact_cancel_releases_cancelled_and_ends_the_contact(void) {
    nt_input_feed(nt_test_contact_down(7, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].cancelled);
    nt_input_feed(nt_test_contact_cancel(7));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].cancelled);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].active);
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].cancelled);
}

/* An up and a cancel of one contact in one poll: the first ends it, the second finds no contact. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_contact_up_and_cancel_in_one_poll_end_it_once(void) {
    nt_input_feed(nt_test_contact_down(7, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_contact_up(7));
    nt_input_feed(nt_test_contact_cancel(7));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].cancelled);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].active);

    nt_input_feed(nt_test_contact_down(8, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_contact_cancel(8));
    nt_input_feed(nt_test_contact_up(8));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].cancelled);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].active);
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_released);

    nt_input_feed(nt_test_contact_down(6, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_contact_up(6));
    nt_input_feed(nt_test_contact_down(6, 9.0F, 9.0F, 0.5F, NT_POINTER_TOUCH, 1)); /* the platform reuses the id */
    nt_input_feed(nt_test_contact_cancel(6));
    nt_input_poll();
    TEST_ASSERT_FALSE_MESSAGE(g_nt_input.pointers[1].cancelled, "the lifted contact's release is real");
    TEST_ASSERT_TRUE_MESSAGE(g_nt_input.pointers[2].cancelled, "the cancel ends the new contact");
    nt_input_poll();

    nt_input_feed(nt_test_contact_down(9, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_contact_cancel(10)); /* not held: nothing to cancel */
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_down);
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].cancelled);
    TEST_ASSERT_FALSE(g_nt_input.pointers[2].active);
}

void test_mouse_cancel_releases_cancelled_and_keeps_the_mouse(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_mouse_cancel());
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].cancelled);
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].cancelled);
    nt_input_feed(nt_test_mouse_cancel()); /* nothing held: nothing to cancel */
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].cancelled);
}

/* Only a pointer with a button held or waiting has a release to cancel. */
void test_focus_lost_cancels_only_held_pointers(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_feed(nt_test_contact_down(7, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].cancelled);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].cancelled);
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1)); /* Alt+Tab during a mouse drag */
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].cancelled);
}

/* The real release keeps its frame; the waiting press the cancel lifted is published as a cancelled tap. */
void test_cancel_of_a_waiting_press_cancels_its_tap(void) {
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_feed(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_feed(nt_test_mouse_cancel());
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].cancelled);
    nt_input_poll();
    nt_button_state_t b = g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT];
    TEST_ASSERT_TRUE(b.is_pressed && b.is_released && !b.is_down);
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].cancelled);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].cancelled);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_event_invisible_until_poll);
    RUN_TEST(test_press_and_release_in_one_frame);
    RUN_TEST(test_key_enum_count);
    RUN_TEST(test_key_repeat_is_no_new_press);
    RUN_TEST(test_key_released_edge);
    RUN_TEST(test_key_pressed_clears_next_frame);
    RUN_TEST(test_any_key_pressed);
    RUN_TEST(test_events_after_focus_lost_apply_in_the_same_frame);
    RUN_TEST(test_focus_lost_then_press_same_key);
    RUN_TEST(test_focus_lost_then_press_same_button);
    RUN_TEST(test_release_then_press_takes_two_frames);
    RUN_TEST(test_press_is_published_where_it_happened);
    RUN_TEST(test_contact_down_is_published_where_it_landed);
    RUN_TEST(test_waiting_press_keeps_its_position_through_a_buttons_event);
    RUN_TEST(test_waiting_tap_survives_a_leave);
    RUN_TEST(test_waiting_tap_keeps_its_point);
    RUN_TEST(test_mouse_back_after_release_reports_no_delta);
    RUN_TEST(test_release_then_tap_and_tap_then_press);
    RUN_TEST(test_waiting_press_keeps_the_mouse_through_a_leave);
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
    RUN_TEST(test_contacts_fill_slots_1_to_7);
    RUN_TEST(test_contact_move_without_down_ignored);
    RUN_TEST(test_mouse_convenience);
    RUN_TEST(test_contact_cancel_releases_cancelled_and_ends_the_contact);
    RUN_TEST(test_contact_up_and_cancel_in_one_poll_end_it_once);
    RUN_TEST(test_mouse_cancel_releases_cancelled_and_keeps_the_mouse);
    RUN_TEST(test_focus_lost_cancels_only_held_pointers);
    RUN_TEST(test_cancel_of_a_waiting_press_cancels_its_tap);
    return UNITY_END();
}
