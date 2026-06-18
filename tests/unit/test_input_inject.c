/* Synthetic-input + player-gate tests (INPUT-04 Wave-0 scaffold).
 *
 * Covers the L1 player gate at the apply-helper seam: gate-off drops real device
 * events regardless of backend (web-leak guard, Pitfall 2); the ON->OFF edge
 * releases held real keys/pointers so nothing sticks down (Pitfall 3); re-enable
 * starts fresh from real devices. Also a placeholder for the nt_input_poll(frame)
 * countdown that the Plan-03 inject cases extend.
 *
 * Shared scaffold: Plan 03 adds the nt_input_inject_* cases to this same file. */

#include "input/nt_input_internal.h" /* nt_input_set_player_enabled + apply helpers */
#include "unity.h"

void setUp(void) { nt_input_init(); }

void tearDown(void) { nt_input_shutdown(); }

/* ---- Player gate: drop real input while a bot drives ---- */

void test_gate_off_drops_real_key(void) {
    nt_input_set_player_enabled(false);
    nt_input_set_key(NT_KEY_A, true); /* real device event — must be dropped at the wrapper */
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
}

void test_gate_off_releases_held_key(void) {
    nt_input_set_key(NT_KEY_W, true);
    nt_input_poll(1U);
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_W));
    nt_input_set_player_enabled(false); /* ON->OFF edge releases the held key */
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_W));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_W));
}

void test_gate_off_releases_held_pointer(void) {
    nt_input_pointer_down(1, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    nt_input_set_player_enabled(false); /* ON->OFF edge releases the held button + clears the slot */
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT].is_released);
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
}

void test_gate_reenable_applies_real(void) {
    nt_input_set_player_enabled(false);
    nt_input_set_player_enabled(true); /* fresh from real devices again */
    nt_input_set_key(NT_KEY_B, true);
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_B));
}

/* ---- Poll countdown placeholder (Plan-03 inject countdown cases extend this) ---- */

void test_poll_countdown_freezes_on_same_frame(void) {
    /* Re-polling the SAME sim-advance frame must not crash and must still clear edge
       state as normal -- the relative inject countdown (Plan 03) freezes on a repeat. */
    nt_input_set_key(NT_KEY_C, true);
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_C));
    nt_input_poll(5U);
    nt_input_poll(5U); /* same frame: no advance */
    TEST_ASSERT_FALSE(nt_input_key_is_pressed(NT_KEY_C));
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_C));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gate_off_drops_real_key);
    RUN_TEST(test_gate_off_releases_held_key);
    RUN_TEST(test_gate_off_releases_held_pointer);
    RUN_TEST(test_gate_reenable_applies_real);
    RUN_TEST(test_poll_countdown_freezes_on_same_frame);
    return UNITY_END();
}
