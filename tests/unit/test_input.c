#include "input/nt_input_internal.h"
#include "unity.h"

static bool float_near(float a, float b, float eps) { return (a - b) <= eps && (b - a) <= eps; }

void setUp(void) { nt_input_init(); }

void tearDown(void) { nt_input_shutdown(); }

/* ---- Key tests ---- */

void test_key_enum_count(void) { TEST_ASSERT_EQUAL_INT(69, NT_KEY_COUNT); }

void test_key_down_after_set(void) {
    nt_input_set_key(NT_KEY_A, true);
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
}

void test_key_pressed_edge(void) {
    nt_input_set_key(NT_KEY_W, true);
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_W));
}

void test_key_released_edge(void) {
    nt_input_set_key(NT_KEY_S, true);
    nt_input_poll();
    nt_input_set_key(NT_KEY_S, false);
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_S));
}

void test_key_pressed_clears_next_frame(void) {
    nt_input_set_key(NT_KEY_D, true);
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_D));
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_key_is_pressed(NT_KEY_D));
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_D));
}

void test_any_key_pressed(void) {
    TEST_ASSERT_FALSE(nt_input_any_key_pressed());
    nt_input_set_key(NT_KEY_SPACE, true);
    TEST_ASSERT_TRUE(nt_input_any_key_pressed());
}

void test_clear_all_keys_sets_released(void) {
    nt_input_set_key(NT_KEY_W, true);
    nt_input_set_key(NT_KEY_A, true);
    nt_input_poll();
    nt_input_clear_all_keys();
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_W));
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_W));
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_A));
}

/* ---- Mouse: pointers[0], kept across a release ---- */

void test_mouse_move_activates_slot_0(void) {
    nt_input_mouse_move(50.0F, 60.0F, 0);
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    TEST_ASSERT_EQUAL_UINT8(NT_POINTER_MOUSE, g_nt_input.pointers[0].type);
    TEST_ASSERT_TRUE(float_near(50.0F, g_nt_input.pointers[0].x, 1e-3F));
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].active);
}

void test_mouse_release_keeps_mouse(void) {
    nt_input_mouse_move(1.0F, 1.0F, 1);
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
    nt_input_poll();
    nt_input_mouse_move(1.0F, 1.0F, 0);
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
}

void test_mouse_leave_deactivates_next_poll(void) {
    nt_input_mouse_move(1.0F, 1.0F, 0);
    nt_input_poll();
    nt_input_mouse_leave();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
}

void test_mouse_leave_ignored_while_held(void) {
    nt_input_mouse_move(1.0F, 1.0F, 1);
    nt_input_mouse_leave();
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* Leave and come back in one poll: the release before the leave stays readable. */
void test_mouse_back_before_leave_keeps_release(void) {
    nt_input_mouse_move(1.0F, 1.0F, 1);
    nt_input_poll();
    nt_input_mouse_move(1.0F, 1.0F, 0);
    nt_input_mouse_leave();
    nt_input_mouse_move(2.0F, 2.0F, 0);
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
}

/* Out at one edge and in at another within one poll: no jump in the delta. */
void test_mouse_back_before_leave_no_jump(void) {
    nt_input_mouse_move(10.0F, 10.0F, 0);
    nt_input_poll();
    nt_input_mouse_leave();
    nt_input_mouse_move(500.0F, 10.0F, 0);
    TEST_ASSERT_TRUE(float_near(0.0F, g_nt_input.pointers[0].dx, 1e-3F));
}

void test_mouse_delta(void) {
    nt_input_mouse_move(100.0F, 200.0F, 0);
    nt_input_poll();
    nt_input_mouse_move(120.0F, 230.0F, 0);
    TEST_ASSERT_TRUE(float_near(20.0F, g_nt_input.pointers[0].dx, 1e-3F));
    TEST_ASSERT_TRUE(float_near(30.0F, g_nt_input.pointers[0].dy, 1e-3F));
}

void test_wheel_needs_mouse_over_window(void) {
    nt_input_wheel(0.0F, 1.0F);
    TEST_ASSERT_TRUE(float_near(0.0F, g_nt_input.pointers[0].wheel_dy, 1e-3F));
    nt_input_mouse_move(1.0F, 1.0F, 0);
    nt_input_wheel(0.0F, 1.0F);
    TEST_ASSERT_TRUE(float_near(1.0F, g_nt_input.pointers[0].wheel_dy, 1e-3F));
}

/* ---- Contacts: slots 1.., from down to up ---- */

void test_contact_never_takes_slot_0(void) {
    nt_input_pointer_down(0, 100.0F, 200.0F, 0.5F, NT_POINTER_TOUCH, 1);
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].active);
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_input.pointers[1].id);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_pressed);
}

void test_contact_move_and_delta(void) {
    nt_input_pointer_down(42, 100.0F, 200.0F, 0.5F, NT_POINTER_TOUCH, 1);
    nt_input_poll();
    nt_input_pointer_move(42, 120.0F, 230.0F, 0.5F, NT_POINTER_TOUCH, 1);
    TEST_ASSERT_TRUE(float_near(120.0F, g_nt_input.pointers[1].x, 1e-3F));
    TEST_ASSERT_TRUE(float_near(20.0F, g_nt_input.pointers[1].dx, 1e-3F));
    TEST_ASSERT_TRUE(float_near(30.0F, g_nt_input.pointers[1].dy, 1e-3F));
}

void test_contact_up_deactivates_next_poll(void) {
    nt_input_pointer_down(42, 100.0F, 200.0F, 0.5F, NT_POINTER_TOUCH, 1);
    nt_input_poll();
    nt_input_pointer_up(42);
    /* Slot stays active for one frame so game can read is_released */
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].active);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_released);
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[1].active);
}

/* Lift and a new down with the same id in one poll: the lifted contact keeps its release edge. */
void test_contact_same_id_down_after_up_new_slot(void) {
    nt_input_pointer_down(4, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1);
    nt_input_poll();
    nt_input_pointer_up(4);
    nt_input_pointer_down(4, 9.0F, 9.0F, 0.5F, NT_POINTER_TOUCH, 1);
    TEST_ASSERT_TRUE(g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_TRUE(g_nt_input.pointers[2].buttons[NT_BUTTON_LEFT].is_pressed);
}

void test_contact_move_without_down_ignored(void) {
    nt_input_pointer_move(5, 50.0F, 60.0F, 0.0F, NT_POINTER_PEN, 0); /* a hovering pen */
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        TEST_ASSERT_FALSE(g_nt_input.pointers[i].active);
    }
}

/* ---- Primary pointer ---- */

void test_primary_none_is_slot_0(void) { TEST_ASSERT_EQUAL_PTR(&g_nt_input.pointers[0], nt_pointer_primary(g_nt_input.pointers, NT_INPUT_MAX_POINTERS)); }

void test_primary_touch_down_over_hovering_mouse(void) {
    nt_input_mouse_move(1.0F, 1.0F, 0);
    TEST_ASSERT_EQUAL_PTR(&g_nt_input.pointers[0], nt_pointer_primary(g_nt_input.pointers, NT_INPUT_MAX_POINTERS));
    nt_input_pointer_down(7, 5.0F, 5.0F, 0.5F, NT_POINTER_TOUCH, 1);
    TEST_ASSERT_EQUAL_PTR(&g_nt_input.pointers[1], nt_pointer_primary(g_nt_input.pointers, NT_INPUT_MAX_POINTERS));
}

/* A lifted touch stays primary through its release frame, so a single-pointer consumer sees the click. */
void test_primary_releasing_touch_stays(void) {
    nt_input_mouse_move(1.0F, 1.0F, 0);
    nt_input_pointer_down(7, 5.0F, 5.0F, 0.5F, NT_POINTER_TOUCH, 1);
    nt_input_poll();
    nt_input_pointer_up(7);
    const nt_pointer_t *p = nt_pointer_primary(g_nt_input.pointers, NT_INPUT_MAX_POINTERS);
    TEST_ASSERT_EQUAL_PTR(&g_nt_input.pointers[1], p);
    TEST_ASSERT_TRUE(p->buttons[NT_BUTTON_LEFT].is_released);
}

/* One finger lifts and another lands in the same poll: the press is the primary. */
void test_primary_press_beats_other_release(void) {
    nt_input_pointer_down(1, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 1);
    nt_input_poll();
    nt_input_pointer_up(1);
    nt_input_pointer_down(2, 5.0F, 5.0F, 0.5F, NT_POINTER_TOUCH, 1);
    TEST_ASSERT_EQUAL_PTR(&g_nt_input.pointers[2], nt_pointer_primary(g_nt_input.pointers, NT_INPUT_MAX_POINTERS));
}

void test_mouse_convenience(void) {
    nt_input_mouse_move(100.0F, 200.0F, 1);
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    nt_input_pointer_down(3, 1.0F, 1.0F, 0.5F, NT_POINTER_TOUCH, 0);
    nt_input_mouse_move(100.0F, 200.0F, 0);
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT)); /* reads the mouse, never a contact */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_key_enum_count);
    RUN_TEST(test_key_down_after_set);
    RUN_TEST(test_key_pressed_edge);
    RUN_TEST(test_key_released_edge);
    RUN_TEST(test_key_pressed_clears_next_frame);
    RUN_TEST(test_any_key_pressed);
    RUN_TEST(test_clear_all_keys_sets_released);
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
    RUN_TEST(test_primary_none_is_slot_0);
    RUN_TEST(test_primary_touch_down_over_hovering_mouse);
    RUN_TEST(test_primary_releasing_touch_stays);
    RUN_TEST(test_primary_press_beats_other_release);
    RUN_TEST(test_mouse_convenience);
    return UNITY_END();
}
