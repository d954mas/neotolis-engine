/* tests/unit/test_nt_ui_walker_dispatch.c — Plan 52-00 stub
 *
 * Covers WALK-01 (all 7 cmd types dispatch correctly) and WALK-04 (BORDER
 * emits 4 thin RECT quads with all 4 widths non-zero). Wave 0 ships
 * TEST_IGNORE bodies; Plan 52-04 fills with real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* WALK-01: RECTANGLE → sprite renderer emit_region (white region) */
static void test_dispatch_rectangle(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-04: BORDER with all 4 widths non-zero emits exactly 4 thin rects */
static void test_dispatch_border_emits_4_rects(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-01: TEXT → flushes sprite, sets font+material, calls nt_text_renderer_draw_n */
static void test_dispatch_text(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-01: IMAGE → reads nt_ui_image_payload_t, emit_region on payload atlas */
static void test_dispatch_image(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-01: SCISSOR_START / SCISSOR_END → flush + push/pop walker stack + gfx scissor */
static void test_dispatch_scissor_start_end(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-01: CUSTOM → flush + invoke g_nt_ui_custom_fn (registered handler) */
static void test_dispatch_custom(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-01: NONE → silent skip (no assert, no warning) */
static void test_dispatch_none_silent_skip(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dispatch_rectangle);
    RUN_TEST(test_dispatch_border_emits_4_rects);
    RUN_TEST(test_dispatch_text);
    RUN_TEST(test_dispatch_image);
    RUN_TEST(test_dispatch_scissor_start_end);
    RUN_TEST(test_dispatch_custom);
    RUN_TEST(test_dispatch_none_silent_skip);
    return UNITY_END();
}
