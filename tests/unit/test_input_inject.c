/* Player gate and inject tests. Frame scheduling lives in devapi (test_devapi_input). */

#include "input/nt_input_feed.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_input_test_frame.h"
#include "unity.h"

static bool float_near(float a, float b, float eps) { return (a - b) <= eps && (b - a) <= eps; }

void setUp(void) {
    nt_test_assert_install();
    nt_input_init();
}

void tearDown(void) { nt_input_shutdown(); }

/* ---- Player gate: drop real input while a bot drives ---- */

void test_gate_off_drops_real_key(void) {
    nt_input_set_player_enabled(false);
    nt_input_feed(nt_test_key(NT_KEY_A, true)); /* real device event — must be dropped at the wrapper */
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
}

/* The gate filters; it releases nothing. */
void test_gate_off_keeps_held_key(void) {
    nt_input_feed(nt_test_key(NT_KEY_W, true));
    nt_input_poll();
    nt_input_set_player_enabled(false);
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_W));
    TEST_ASSERT_FALSE(nt_input_key_is_released(NT_KEY_W));
}

void test_gate_off_drops_real_release(void) {
    nt_input_feed(nt_test_key(NT_KEY_W, true));
    nt_input_poll();
    nt_input_set_player_enabled(false);
    nt_input_feed(nt_test_key(NT_KEY_W, false));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_W)); /* held input is the bot's while gated */
}

void test_gate_reenable_applies_real(void) {
    nt_input_set_player_enabled(false);
    nt_input_set_player_enabled(true);
    nt_input_feed(nt_test_key(NT_KEY_B, true));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_B));
}

void test_gate_off_keeps_held_pointer(void) {
    nt_input_feed(nt_test_mouse(1.0F, 1.0F, 1));
    nt_input_poll();
    nt_input_set_player_enabled(false);
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
}

/* Gated, held input is the bot's: a click into another window must not cut a bot drag. */
void test_gated_focus_lost_keeps_bot_input(void) {
    nt_input_set_player_enabled(false);
    nt_input_inject(nt_test_key(NT_KEY_A, true));
    nt_input_inject(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    nt_input_feed(nt_test_focus_lost());
    nt_input_set_player_enabled(true); /* the gate judged the loss when it arrived */
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* A real event is judged by the gate when it reaches the core, not at the next poll. */
void test_gate_judges_real_event_at_arrival(void) {
    nt_input_feed(nt_test_key(NT_KEY_A, true));
    nt_input_set_player_enabled(false);
    nt_input_feed(nt_test_key(NT_KEY_B, true));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_B));
}

/* Real and injected input reaching the core in one frame both apply, in arrival order. */
void test_inject_and_real_in_one_frame(void) {
    nt_input_feed(nt_test_key(NT_KEY_A, true));
    nt_input_inject(nt_test_key(NT_KEY_A, false));
    nt_input_feed(nt_test_char('r'));
    nt_input_inject(nt_test_char('i'));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_A));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
    uint32_t cp = 0;
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32('r', cp);
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32('i', cp);
}

/* ---- inject applies at the call; the next poll publishes it ---- */

/* ---- inject contact down/move/up + mouse + wheel ---- */

/* The bot drives the same mouse a real one holds: its click is on pointers[0], and the mouse stays. */
void test_inject_mouse_drives_real_mouse(void) {
    nt_input_feed(nt_test_mouse(1.0F, 1.0F, 0));
    nt_input_poll();
    nt_input_inject(nt_test_mouse(5.0F, 5.0F, 1));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT].is_pressed);
    nt_input_inject(nt_test_mouse_buttons(0));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT].is_released);
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
}

void test_inject_wheel_mouse_slot(void) {
    /* The move brings the mouse over the window and the wheel lands on it. */
    nt_input_inject(nt_test_mouse(5.0F, 5.0F, 0));
    nt_input_inject(nt_test_wheel(0.0F, 3.0F));
    nt_input_poll();
    TEST_ASSERT_TRUE(float_near(3.0F, g_nt_input.pointers[0].wheel_dy, 0.001F));
}

/* A non-left button mask must press the CORRECT button (bit->button mapping), not just leave the
   slot active. bit 2 -> RIGHT, bit 4 -> MIDDLE — queried by-value via the mouse helpers. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_inject_button_mask_maps_right_and_middle(void) {
    /* mask 2 = right only. */
    nt_input_inject(nt_test_mouse(1.0F, 1.0F, 2U));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_RIGHT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_MIDDLE));

    /* mask 4 = middle only: RIGHT releases, MIDDLE presses. */
    nt_input_inject(nt_test_mouse(1.0F, 1.0F, 4U));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_MIDDLE));
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_MIDDLE));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_RIGHT));
}

/* inject_mouse_buttons sets the mask at the mouse's CURRENT position without moving it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_inject_buttons_at_current_position(void) {
    nt_input_inject(nt_test_mouse(70.0F, 80.0F, 0));
    nt_input_inject(nt_test_mouse_buttons(1U)); /* left, at the mouse's current (70,80) */
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    TEST_ASSERT_TRUE(float_near(70.0F, g_nt_input.pointers[0].x, 0.001F));
    TEST_ASSERT_TRUE(float_near(80.0F, g_nt_input.pointers[0].y, 0.001F));
    TEST_ASSERT_TRUE(float_near(0.0F, g_nt_input.pointers[0].dx, 0.001F)); /* no move -> no delta */
}

void test_inject_buttons_without_mouse_is_noop(void) {
    nt_input_inject(nt_test_mouse_buttons(1U));
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active); /* never a press at (0,0) */
}

static void touch_down(uint32_t id) { nt_input_inject(nt_test_contact_down(id, (float)id, (float)id, 0.5F, NT_POINTER_TOUCH, 1)); }

static uint32_t primary_id(void) { return g_nt_input.pointers[g_nt_input.primary].id; }

void test_primary_touch_press_over_hovering_mouse(void) {
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT8(0, g_nt_input.primary);
    nt_input_inject(nt_test_mouse(1.0F, 1.0F, 0));
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT8(0, g_nt_input.primary);
    touch_down(7);
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT8(1, g_nt_input.primary);
}

void test_primary_held_mouse_keeps_over_touch_press(void) {
    nt_input_inject(nt_test_mouse(1.0F, 1.0F, 1));
    nt_input_poll();
    touch_down(7);
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT8(0, g_nt_input.primary);
}

/* A lifted touch stays primary through its release frame, so a single-pointer consumer sees the click. */
void test_primary_releasing_touch_stays(void) {
    touch_down(7);
    nt_input_poll();
    nt_input_inject(nt_test_contact_up(7));
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT32(7, primary_id());
    TEST_ASSERT_TRUE(g_nt_input.pointers[g_nt_input.primary].buttons[NT_BUTTON_LEFT].is_released);
}

/* A finger held before the primary lifts, or landing in its release frame, never takes over; the next press does. */
void test_primary_held_contact_never_takes_over(void) {
    touch_down(1);
    nt_input_poll();
    touch_down(2);
    nt_input_poll();
    nt_input_inject(nt_test_contact_up(1));
    touch_down(3);
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT32(1, primary_id());
    nt_input_poll();
    TEST_ASSERT_FALSE(g_nt_input.pointers[g_nt_input.primary].active);
    nt_input_inject(nt_test_mouse(1.0F, 1.0F, 1)); /* contacts 2 and 3 are still held */
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT8(0, g_nt_input.primary);
}

void test_primary_next_contact_press_takes_over(void) {
    touch_down(1);
    nt_input_poll();
    touch_down(2);
    nt_input_inject(nt_test_contact_up(1));
    nt_input_poll();
    nt_input_poll();
    touch_down(4); /* contact 2 is still held */
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT32(4, primary_id());
}

/* The mouse pressed during a touch never takes over: not on its release, not while it hovers with a contact held. */
void test_primary_mouse_waits_until_nothing_in_use(void) {
    touch_down(1);
    nt_input_poll();
    nt_input_inject(nt_test_mouse(1.0F, 1.0F, 1));
    nt_input_poll();
    nt_input_inject(nt_test_contact_up(1));
    nt_input_poll();
    nt_input_poll();
    nt_input_inject(nt_test_mouse(1.0F, 1.0F, 0));
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT8(1, g_nt_input.primary);
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT8(0, g_nt_input.primary);
}

/* ---- The devapi layer validates untrusted commands; an invalid inject argument asserts ---- */

void test_inject_invalid_args_assert(void) {
    NT_TEST_EXPECT_ASSERT(nt_input_inject(nt_test_key(NT_KEY_COUNT, true)));
    NT_TEST_EXPECT_ASSERT(nt_input_inject(nt_test_contact_down(0, 0, 0, 0, NT_POINTER_MOUSE, 1))); /* the mouse is not a contact */
    NT_TEST_EXPECT_ASSERT(nt_input_inject(nt_test_contact_down(0, 0, 0, 0, 99, 1)));
    NT_TEST_EXPECT_ASSERT(nt_input_inject(nt_test_contact_down(0, 0, 0, 0, NT_POINTER_TOUCH, 8)));
    NT_TEST_EXPECT_ASSERT(nt_input_inject(nt_test_mouse(0, 0, 8)));
    NT_TEST_EXPECT_ASSERT(nt_input_inject(nt_test_mouse_buttons(8))); /* even with no mouse over the window */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gate_off_drops_real_key);
    RUN_TEST(test_gate_off_keeps_held_key);
    RUN_TEST(test_gate_off_drops_real_release);
    RUN_TEST(test_gate_reenable_applies_real);
    RUN_TEST(test_gate_off_keeps_held_pointer);
    RUN_TEST(test_gated_focus_lost_keeps_bot_input);
    RUN_TEST(test_gate_judges_real_event_at_arrival);
    RUN_TEST(test_inject_and_real_in_one_frame);
    RUN_TEST(test_inject_mouse_drives_real_mouse);
    RUN_TEST(test_inject_wheel_mouse_slot);
    RUN_TEST(test_inject_button_mask_maps_right_and_middle);
    RUN_TEST(test_inject_buttons_at_current_position);
    RUN_TEST(test_inject_buttons_without_mouse_is_noop);
    RUN_TEST(test_primary_touch_press_over_hovering_mouse);
    RUN_TEST(test_primary_held_mouse_keeps_over_touch_press);
    RUN_TEST(test_primary_releasing_touch_stays);
    RUN_TEST(test_primary_held_contact_never_takes_over);
    RUN_TEST(test_primary_next_contact_press_takes_over);
    RUN_TEST(test_primary_mouse_waits_until_nothing_in_use);
    RUN_TEST(test_inject_invalid_args_assert);
    return UNITY_END();
}
