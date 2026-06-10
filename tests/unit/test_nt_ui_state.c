/* Generic per-id state pool tests (#190). RED Wave-0 scaffold — Task 2 of this
 * plan (59-01) GREENs it once engine/ui/nt_ui_state.{h,c} ship.
 *
 * Test Map (filled by Task 2):
 *   - get-or-create returns a non-NULL ZEROED payload on first touch
 *   - re-acquire same id+size in-frame returns the SAME pointer (mutation intact)
 *   - find returns NULL for an id never created
 *   - clear frees the cell; a later nt_ui_state re-creates it zeroed
 *   - clear_all empties the pool (used_slots == 0)
 *   - used_slots / used_bytes track live cells
 *   - FULL-gated death: re-acquire id with a different size asserts (D-59-08)
 *   - FULL-gated death: fill past the probe chain / all slots asserts (D-59-07) */

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_state_red_wave0(void) { TEST_FAIL_MESSAGE("Wave 1 (59-01 Task 2) — nt_ui_state pool not yet shipped; RED scaffold"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_state_red_wave0);
    return UNITY_END();
}
