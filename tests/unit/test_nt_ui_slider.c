/* Slider widget tests (RED Wave-0 scaffold).
 *
 * 59-03 GREENs these: value<->position map (float + int variants), thumb-grab
 * (press on thumb = relative drag, value unchanged) vs track-jump (press on
 * track = value jumps to click point), step quantize (0 = continuous), exposed
 * thumb screen position. Until then this binary is intentionally RED. */

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* value map round-trips min/max; thumb-grab keeps value on press, track-jump
 * moves it; step quantizes; thumb_pos query matches the emitted thumb bbox. */
static void test_slider_red_wave0(void) { TEST_FAIL_MESSAGE("Wave 3 (59-03 slider-and-fill-helper) — RED scaffold, not yet GREEN"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_slider_red_wave0);
    return UNITY_END();
}
