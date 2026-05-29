/* RED scaffold for the engine-owned interaction state machine (WIDGET-02).
 *
 * Wave 0 (Plan 01): registration + RED status only. The real assertions
 * (frame-by-frame pointer sequence vs prev-frame bbox) land in Plan 03 once
 * nt_ui_get_interaction / nt_ui_id exist. Each test body is a deliberate
 * TEST_FAIL_MESSAGE placeholder referencing the not-yet-implemented symbol.
 *
 * Test design (RESEARCH 56 "Concrete unit-test design for the state machine"):
 *   Frame 1: nt_ui_begin; declare CLAY({.id=CLAY_ID("btn"), .layout fixed
 *            200x60}) at a known bbox; nt_ui_end (bbox stored in Clay hashmap).
 *   Frame 2: pointers[0] inside bbox + buttons[LEFT].is_pressed=is_down=true;
 *            in = nt_ui_get_interaction(ctx, nt_ui_id("btn"));
 *            assert in.hovered && in.pressed && in.pressed_now && !in.clicked.
 *   Frame 3: is_down=false, is_released=true, still inside →
 *            in.released_now && in.clicked (release-over-widget = one-shot).
 *   Variant: release with pointer OUTSIDE bbox → released_now && !clicked.
 *   Disabled: enabled=false short-circuits → no hover, no click. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Unity float macros are excluded in this build (UNITY_EXCLUDE_FLOAT). */
static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* ---- Test 1: press-then-release over widget → clicked once (SC #1) ---- */
static void test_interaction_click_on_release_within_bounds(void) {
    /* Plan 03 fills this: 3-frame pointer sequence asserting that clicked is
     * true exactly once on release-over-widget and false on press-down.
     * References nt_ui_get_interaction + nt_ui_id (not yet implemented). */
    (void)float_near;
    TEST_FAIL_MESSAGE("Plan 03 (Wave 1): nt_ui_get_interaction not implemented");
}

/* ---- Test 2: idle→hover→pressed state progression (SC #2) ---- */
static void test_interaction_hover_then_pressed(void) {
    /* Plan 03: pointer enters bbox (hovered, !pressed), then presses
     * (hovered && pressed && pressed_now). References nt_ui_get_interaction. */
    TEST_FAIL_MESSAGE("Plan 03 (Wave 1): nt_ui_get_interaction not implemented");
}

/* ---- Test 3: press, release OUTSIDE widget → cancel (released_now, !clicked) ---- */
static void test_interaction_release_outside_cancels(void) {
    /* Plan 03: press inside, move pointer outside bbox, release → released_now
     * true but clicked false. References nt_ui_get_interaction. */
    TEST_FAIL_MESSAGE("Plan 03 (Wave 1): nt_ui_get_interaction not implemented");
}

/* ---- Test 4: disabled widget skips hover + click (D-56-12) ---- */
static void test_interaction_disabled_skips(void) {
    /* Plan 04: nt_ui_button_begin(ctx, id, &style, enabled=false) with pointer
     * inside + pressed → interaction reports no hover/click. References
     * nt_ui_button_begin / nt_ui_get_interaction. */
    TEST_FAIL_MESSAGE("Plan 03/04: disabled-skip path not implemented");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_interaction_click_on_release_within_bounds);
    RUN_TEST(test_interaction_hover_then_pressed);
    RUN_TEST(test_interaction_release_outside_cancels);
    RUN_TEST(test_interaction_disabled_skips);
    return UNITY_END();
}
