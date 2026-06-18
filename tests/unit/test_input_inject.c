/* L1 synthetic-input + player-gate tests (INPUT-01..04).
 *
 * Covers the L1 player gate at the apply-helper seam: gate-off drops real device events regardless
 * of backend (web-leak guard, Pitfall 2); the ON->OFF edge releases held real keys/pointers so
 * nothing sticks down (Pitfall 3); re-enable starts fresh from real devices. Plus the pure-apply
 * inject API: nt_input is now a pure apply layer — every inject stages into an immediate buffer that
 * nt_input_poll() drains WHOLE that same poll (no frame, no countdown, no schedule). Frame scheduling
 * (hold, gesture stride, pause-freeze) lives in the devapi layer and is covered by test_devapi_input. */

#include "input/nt_input_internal.h" /* nt_input_set_player_enabled + inject API + apply helpers */
#include "unity.h"

static bool float_near(float a, float b, float eps) { return (a - b) <= eps && (b - a) <= eps; }

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
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_W));
    nt_input_set_player_enabled(false); /* ON->OFF edge releases the held key */
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_W));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_W));
}

void test_gate_off_releases_held_pointer(void) {
    nt_input_pointer_down(1, 100.0F, 200.0F, 0.5F, NT_POINTER_MOUSE, 1);
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    nt_input_set_player_enabled(false); /* ON->OFF edge releases the held button (deferred deactivation) */
    /* The release edge is readable via the PUBLIC query this frame: deactivation is deferred one
       poll (like a normal pointer_up) so find_mouse_pointer still sees the active slot. */
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    nt_input_poll(); /* next poll resolves deactivate_pending -> slot deactivates, no leak */
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_released(NT_BUTTON_LEFT)); /* slot gone -> release no longer readable */
}

void test_gate_reenable_applies_real(void) {
    nt_input_set_player_enabled(false);
    nt_input_set_player_enabled(true); /* fresh from real devices again */
    nt_input_set_key(NT_KEY_B, true);
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_B));
}

/* ---- INPUT-01: inject is IMMEDIATE — staged this poll, applied this poll ---- */

void test_inject_key_down_up(void) {
    TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_A, true));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_A, false));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_A));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
}

/* The whole buffer drains in ONE poll, in stage order: down then up -> pressed && released the same
   poll, ending not-down. (Holding across frames is a devapi-schedule concern, not an L1 one.) */
void test_inject_same_poll_down_then_up(void) {
    TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_A, true));
    TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_A, false));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_A));
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
}

void test_inject_flows_when_gated(void) {
    nt_input_set_player_enabled(false); /* real device dropped, inject still flows (D-03) */
    TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_A, true));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
}

/* ---- INPUT-02: inject pointer down/move/up + wheel + reserved id ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_inject_pointer_down_move_up(void) {
    TEST_ASSERT_TRUE(nt_input_inject_pointer(NT_INJECT_POINTER_DOWN, 2, 10.0F, 20.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].active);
    TEST_ASSERT_EQUAL_UINT32(2U, g_nt_input.pointers[0].id);
    TEST_ASSERT_EQUAL_UINT8(NT_POINTER_TOUCH, g_nt_input.pointers[0].type);
    TEST_ASSERT_TRUE(float_near(10.0F, g_nt_input.pointers[0].x, 0.001F));
    TEST_ASSERT_TRUE(float_near(20.0F, g_nt_input.pointers[0].y, 0.001F));

    TEST_ASSERT_TRUE(nt_input_inject_pointer(NT_INJECT_POINTER_MOVE, 2, 30.0F, 40.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    TEST_ASSERT_TRUE(float_near(30.0F, g_nt_input.pointers[0].x, 0.001F));
    TEST_ASSERT_TRUE(float_near(40.0F, g_nt_input.pointers[0].y, 0.001F));

    TEST_ASSERT_TRUE(nt_input_inject_pointer(NT_INJECT_POINTER_UP, 2, 0.0F, 0.0F, 0.0F, 0, 0));
    nt_input_poll(); /* up sets deactivate_pending */
    TEST_ASSERT_TRUE(g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT].is_released);
    nt_input_poll(); /* next poll deactivates */
    TEST_ASSERT_FALSE(g_nt_input.pointers[0].active);
}

void test_inject_wheel_mouse_slot(void) {
    /* Wheel routes to the mouse slot only -- needs a mouse pointer first. Both stage this poll, so
       the MOVE creates the slot and the WHEEL lands on it in the same drain (stage order). */
    TEST_ASSERT_TRUE(nt_input_inject_pointer(NT_INJECT_POINTER_MOVE, 0, 5.0F, 5.0F, 0.0F, NT_POINTER_MOUSE, 0));
    TEST_ASSERT_TRUE(nt_input_inject_wheel(0.0F, 3.0F));
    nt_input_poll();
    TEST_ASSERT_EQUAL_UINT8(NT_POINTER_MOUSE, g_nt_input.pointers[0].type);
    TEST_ASSERT_TRUE(float_near(3.0F, g_nt_input.pointers[0].wheel_dy, 0.001F));
}

/* INPUT-02: a non-left button mask must press the CORRECT button (bit->button mapping), not just
   leave the slot active. bit 2 -> RIGHT, bit 4 -> MIDDLE — queried by-value via the mouse helpers. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_inject_button_mask_maps_right_and_middle(void) {
    /* mask 2 = right only: a mouse-slot DOWN carrying the mask. */
    TEST_ASSERT_TRUE(nt_input_inject_pointer(NT_INJECT_POINTER_DOWN, 0, 1.0F, 1.0F, 1.0F, NT_POINTER_MOUSE, 2U));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_RIGHT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_MIDDLE));

    /* mask 4 = middle only (via MOVE on the same slot): RIGHT releases, MIDDLE presses. */
    TEST_ASSERT_TRUE(nt_input_inject_pointer(NT_INJECT_POINTER_MOVE, 0, 1.0F, 1.0F, 1.0F, NT_POINTER_MOUSE, 4U));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_MIDDLE));
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_MIDDLE));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_RIGHT));
}

void test_inject_reserved_id(void) {
    /* A real pointer at id=0 and the convenience synthetic id never collide (different slots). */
    nt_input_pointer_down(0, 1.0F, 1.0F, 0.0F, NT_POINTER_MOUSE, 1);
    TEST_ASSERT_TRUE(nt_input_inject_pointer(NT_INJECT_POINTER_DOWN, NT_INPUT_INJECT_POINTER_ID_BASE, 50.0F, 60.0F, 0.5F, NT_POINTER_TOUCH, 1));
    nt_input_poll();
    nt_pointer_t *real = NULL;
    nt_pointer_t *synth = NULL;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (!g_nt_input.pointers[i].active) {
            continue;
        }
        if (g_nt_input.pointers[i].id == 0U) {
            real = &g_nt_input.pointers[i];
        }
        if (g_nt_input.pointers[i].id == NT_INPUT_INJECT_POINTER_ID_BASE) {
            synth = &g_nt_input.pointers[i];
        }
    }
    TEST_ASSERT_NOT_NULL(real);
    TEST_ASSERT_NOT_NULL(synth);
    TEST_ASSERT_TRUE(real != synth); /* distinct slots, no collision */
}

/* ---- INPUT-03: whole-or-nothing overflow rejection (no poll between fills: the buffer drains
   whole each poll, so the cap is probed purely by staging without draining) ---- */

void test_overflow_rejects_whole(void) {
    /* Fill the immediate buffer to CAP-1 (no poll, so nothing drains). */
    for (uint32_t i = 0; i < NT_INPUT_INJECT_QUEUE_MAX - 1U; i++) {
        TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_A, true));
    }
    /* A 2-codepoint text needs 2 slots but only 1 remains -> rejected WHOLE, writes nothing. */
    uint32_t two[2] = {'x', 'y'};
    TEST_ASSERT_FALSE(nt_input_inject_text(two, 2U));
    /* Proof the text left no partial entry: exactly 1 free slot must remain, so a single 1-entry
       inject succeeds and a second one then overflows. */
    TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_C, true));
    TEST_ASSERT_FALSE(nt_input_inject_key(NT_KEY_D, true));
}

/* ---- string -> nt_key_t ---- */

void test_key_from_name(void) {
    nt_key_t out = NT_KEY_COUNT;
    TEST_ASSERT_TRUE(nt_input_key_from_name("A", &out));
    TEST_ASSERT_EQUAL_INT(NT_KEY_A, out);
    TEST_ASSERT_TRUE(nt_input_key_from_name("SPACE", &out));
    TEST_ASSERT_EQUAL_INT(NT_KEY_SPACE, out);
    TEST_ASSERT_TRUE(nt_input_key_from_name("ARROW_UP", &out));
    TEST_ASSERT_EQUAL_INT(NT_KEY_ARROW_UP, out);
    TEST_ASSERT_TRUE(nt_input_key_from_name("F1", &out));
    TEST_ASSERT_EQUAL_INT(NT_KEY_F1, out);

    nt_key_t untouched = NT_KEY_Z;
    TEST_ASSERT_FALSE(nt_input_key_from_name("NOPE", &untouched));
    TEST_ASSERT_EQUAL_INT(NT_KEY_Z, untouched); /* out left untouched on unknown */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gate_off_drops_real_key);
    RUN_TEST(test_gate_off_releases_held_key);
    RUN_TEST(test_gate_off_releases_held_pointer);
    RUN_TEST(test_gate_reenable_applies_real);
    RUN_TEST(test_inject_key_down_up);
    RUN_TEST(test_inject_same_poll_down_then_up);
    RUN_TEST(test_inject_flows_when_gated);
    RUN_TEST(test_inject_pointer_down_move_up);
    RUN_TEST(test_inject_wheel_mouse_slot);
    RUN_TEST(test_inject_button_mask_maps_right_and_middle);
    RUN_TEST(test_inject_reserved_id);
    RUN_TEST(test_overflow_rejects_whole);
    RUN_TEST(test_key_from_name);
    return UNITY_END();
}
