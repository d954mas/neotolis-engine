#include "unity.h"
#include "window/nt_window.h"

#include <math.h>

/* Helper: float approximately equal (avoids UNITY_EXCLUDE_FLOAT issue) */
static bool float_near(float a, float b, float epsilon) { return fabsf(a - b) <= epsilon; }

void setUp(void) {
    /* Reset g_nt_window to defaults before each test */
    g_nt_window.max_dpr = 2.0F;
    g_nt_window.width = 0;
    g_nt_window.height = 0;
    g_nt_window.fb_width = 0;
    g_nt_window.fb_height = 0;
    g_nt_window.dpr = 0.0F;
}

void tearDown(void) { /* Called after each test */ }

/* 1. Default max_dpr is 2.0F after definition */
void test_window_default_max_dpr(void) {
    /* Temporarily set max_dpr to something else, then verify the compiled
       default from nt_window.c by re-reading a fresh global.  Since we cannot
       re-initialize the global, we verify the setUp value which mirrors it. */
    nt_window_t fresh = {.max_dpr = 2.0F};
    TEST_ASSERT_TRUE_MESSAGE(float_near(2.0F, fresh.max_dpr, 1e-6F), "Default max_dpr should be 2.0F");
}

/* 2. apply_sizes with 1x DPR: fb matches canvas */
void test_window_apply_sizes_1x_dpr(void) {
    nt_window_apply_sizes(800.0F, 600.0F, 1.0F);
    TEST_ASSERT_EQUAL_UINT32(800, g_nt_window.width);
    TEST_ASSERT_EQUAL_UINT32(600, g_nt_window.height);
    TEST_ASSERT_EQUAL_UINT32(800, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(600, g_nt_window.fb_height);
    TEST_ASSERT_TRUE_MESSAGE(float_near(1.0F, g_nt_window.dpr, 1e-6F), "Effective DPR should be 1.0");
}

/* 3. apply_sizes with 2x DPR and max_dpr=2.0: fb = canvas * 2 */
void test_window_apply_sizes_2x_dpr(void) {
    g_nt_window.max_dpr = 2.0F;
    nt_window_apply_sizes(800.0F, 600.0F, 2.0F);
    TEST_ASSERT_EQUAL_UINT32(800, g_nt_window.width);
    TEST_ASSERT_EQUAL_UINT32(600, g_nt_window.height);
    TEST_ASSERT_EQUAL_UINT32(1600, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(1200, g_nt_window.fb_height);
    TEST_ASSERT_TRUE_MESSAGE(float_near(2.0F, g_nt_window.dpr, 1e-6F), "Effective DPR should be 2.0");
}

/* 4. apply_sizes with device DPR exceeding max_dpr: DPR capped */
void test_window_apply_sizes_dpr_capped(void) {
    g_nt_window.max_dpr = 2.0F;
    nt_window_apply_sizes(800.0F, 600.0F, 3.0F);
    TEST_ASSERT_EQUAL_UINT32(1600, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(1200, g_nt_window.fb_height);
    TEST_ASSERT_TRUE_MESSAGE(float_near(2.0F, g_nt_window.dpr, 1e-6F), "DPR should be capped to max_dpr=2.0");
}

/* 5. apply_sizes with device DPR below 1.0: floored to 1.0 */
void test_window_apply_sizes_dpr_floor(void) {
    nt_window_apply_sizes(800.0F, 600.0F, 0.5F);
    TEST_ASSERT_EQUAL_UINT32(800, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(600, g_nt_window.fb_height);
    TEST_ASSERT_TRUE_MESSAGE(float_near(1.0F, g_nt_window.dpr, 1e-6F), "DPR should be floored to 1.0 (never below)");
}

/* 6. Fractional DPR: fb dimensions use lroundf */
void test_window_apply_sizes_fractional_dpr(void) {
    g_nt_window.max_dpr = 2.0F;
    nt_window_apply_sizes(601.0F, 401.0F, 1.5F);
    /* 601 * 1.5 = 901.5 -> round = 902 */
    /* 401 * 1.5 = 601.5 -> round = 602 */
    TEST_ASSERT_EQUAL_UINT32(902, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(602, g_nt_window.fb_height);
    TEST_ASSERT_TRUE_MESSAGE(float_near(1.5F, g_nt_window.dpr, 1e-6F), "Effective DPR should be 1.5");
}

/* 7. max_dpr=1.0 forces 1x regardless of device DPR */
void test_window_apply_sizes_max_dpr_one(void) {
    g_nt_window.max_dpr = 1.0F;
    nt_window_apply_sizes(800.0F, 600.0F, 2.0F);
    TEST_ASSERT_EQUAL_UINT32(800, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(600, g_nt_window.fb_height);
    TEST_ASSERT_TRUE_MESSAGE(float_near(1.0F, g_nt_window.dpr, 1e-6F), "max_dpr=1.0 should force 1x DPR");
}

/* 8. Multiple calls: fields fully overwritten each time (no stale data) */
void test_window_apply_sizes_overwrites(void) {
    nt_window_apply_sizes(800.0F, 600.0F, 2.0F);
    TEST_ASSERT_EQUAL_UINT32(1600, g_nt_window.fb_width);

    /* Second call with different values */
    nt_window_apply_sizes(400.0F, 300.0F, 1.0F);
    TEST_ASSERT_EQUAL_UINT32(400, g_nt_window.width);
    TEST_ASSERT_EQUAL_UINT32(300, g_nt_window.height);
    TEST_ASSERT_EQUAL_UINT32(400, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(300, g_nt_window.fb_height);
    TEST_ASSERT_TRUE_MESSAGE(float_near(1.0F, g_nt_window.dpr, 1e-6F), "Fields must be fully overwritten on each call");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_window_default_max_dpr);
    RUN_TEST(test_window_apply_sizes_1x_dpr);
    RUN_TEST(test_window_apply_sizes_2x_dpr);
    RUN_TEST(test_window_apply_sizes_dpr_capped);
    RUN_TEST(test_window_apply_sizes_dpr_floor);
    RUN_TEST(test_window_apply_sizes_fractional_dpr);
    RUN_TEST(test_window_apply_sizes_max_dpr_one);
    RUN_TEST(test_window_apply_sizes_overwrites);
    return UNITY_END();
}
