/* Scroll container + custom engine physics tests (RED Wave-0 scaffold).
 *
 * 59-02 GREENs these: custom momentum/fling + overscroll bounce + smooth wheel +
 * animated scroll-to physics feeding clip.childOffset (Clay_UpdateScrollContainers
 * bypassed); capture-steal-by-threshold drag arbitration; both axes; Clay
 * childOffset sign convention. Until then this binary is intentionally RED. */

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Physics: momentum decays toward rest, overscroll rubber-bands, wheel eases to
 * target (no teleport), nt_ui_scroll_to animates; capture-steal cancels a child
 * press past the threshold; childOffset feed matches Clay's sign. */
static void test_scroll_red_wave0(void) { TEST_FAIL_MESSAGE("Wave 2 (59-02 scroll-physics-and-container) — RED scaffold, not yet GREEN"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_scroll_red_wave0);
    return UNITY_END();
}
