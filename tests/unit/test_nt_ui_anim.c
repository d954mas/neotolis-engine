/* RED scaffold for the direct-mapped per-id anim cache (D-56-15/16).
 *
 * Wave 0 (Plan 01): registration + RED status only. The real assertions land
 * in Plan 02 once the anim cache + accessor exist. The cache follows the
 * Phase-51 font-measure-cache precedent: fixed power-of-2 array, key = Clay
 * uint32 id directly (no hashing), replace-on-collision, no LRU/eviction.
 * Each test body is a TEST_FAIL_MESSAGE placeholder.
 *
 * Test design (RESEARCH 56 New Work Item 3 + CONTEXT D-56-15/16):
 *   - Lerp convergence: cur += (target - cur) * clampf(transition_speed*dt,0,1).
 *       transition_speed == 0 → instant (cur == target).
 *       transition_speed  > 0 → cur approaches target over N dt steps
 *       (monotone, never overshoots).
 *   - First touch of a slot initializes cur = target (no animate-from-zero flash).
 *   - Replace-on-collision: two ids mapping to the same slot via
 *       id & (NT_UI_ANIM_SLOTS - 1) → the second overwrites the first,
 *       no crash, no eviction scan. */

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

/* ---- Test 1: transition_speed == 0 → instant snap to target ---- */
static void test_anim_instant_when_speed_zero(void) {
    /* Plan 02: set target with transition_speed 0 → cur == target same frame.
     * References the anim accessor (not yet implemented). */
    (void)float_near;
    TEST_FAIL_MESSAGE("Plan 02 (Wave 1): nt_ui anim cache not implemented");
}

/* ---- Test 2: transition_speed > 0 → eased convergence over dt steps ---- */
static void test_anim_eases_toward_target(void) {
    /* Plan 02: step several frames with dt>0; assert cur monotonically
     * approaches target and converges within eps. References the anim accessor. */
    TEST_FAIL_MESSAGE("Plan 02 (Wave 1): nt_ui anim cache not implemented");
}

/* ---- Test 3: first touch initializes cur = target (no flash) ---- */
static void test_anim_first_touch_no_flash(void) {
    /* Plan 02: a freshly-seen id starts cur == target (not from zero).
     * References the anim accessor (not yet implemented). */
    TEST_FAIL_MESSAGE("Plan 02 (Wave 1): nt_ui anim cache not implemented");
}

/* ---- Test 4: replace-on-collision (two ids same slot, no crash) ---- */
static void test_anim_replace_on_collision(void) {
    /* Plan 02: two ids that map to the same slot via id & (SLOTS-1) → second
     * overwrites first, no crash, no eviction. References the anim accessor and
     * NT_UI_ANIM_SLOTS (not yet defined). */
    TEST_FAIL_MESSAGE("Plan 02 (Wave 1): nt_ui anim cache not implemented");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_anim_instant_when_speed_zero);
    RUN_TEST(test_anim_eases_toward_target);
    RUN_TEST(test_anim_first_touch_no_flash);
    RUN_TEST(test_anim_replace_on_collision);
    return UNITY_END();
}
