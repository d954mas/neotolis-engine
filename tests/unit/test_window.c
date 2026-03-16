#include "unity.h"
#include "window/nt_window.h"

#include <math.h>

static bool float_near(float a, float b, float epsilon) { return fabsf(a - b) <= epsilon; }

void setUp(void) { g_nt_window = (nt_window_t){.max_dpr = 2.0F}; }

void tearDown(void) {}

/* 1x DPR: fb matches canvas */
void test_apply_sizes_1x(void) {
    nt_window_apply_sizes(800.0F, 600.0F, 1.0F);
    TEST_ASSERT_EQUAL_UINT32(800, g_nt_window.width);
    TEST_ASSERT_EQUAL_UINT32(600, g_nt_window.height);
    TEST_ASSERT_EQUAL_UINT32(800, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(600, g_nt_window.fb_height);
    TEST_ASSERT_TRUE(float_near(1.0F, g_nt_window.dpr, 1e-6F));
}

/* 2x DPR: fb = canvas * 2 */
void test_apply_sizes_2x(void) {
    nt_window_apply_sizes(800.0F, 600.0F, 2.0F);
    TEST_ASSERT_EQUAL_UINT32(1600, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(1200, g_nt_window.fb_height);
    TEST_ASSERT_TRUE(float_near(2.0F, g_nt_window.dpr, 1e-6F));
}

/* device DPR > max_dpr: capped */
void test_apply_sizes_dpr_capped(void) {
    nt_window_apply_sizes(800.0F, 600.0F, 3.0F);
    TEST_ASSERT_EQUAL_UINT32(1600, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(1200, g_nt_window.fb_height);
    TEST_ASSERT_TRUE(float_near(2.0F, g_nt_window.dpr, 1e-6F));
}

/* device DPR < 1.0: floored to 1.0 */
void test_apply_sizes_dpr_floor(void) {
    nt_window_apply_sizes(800.0F, 600.0F, 0.5F);
    TEST_ASSERT_EQUAL_UINT32(800, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(600, g_nt_window.fb_height);
    TEST_ASSERT_TRUE(float_near(1.0F, g_nt_window.dpr, 1e-6F));
}

/* Fractional DPR: fb uses roundf */
void test_apply_sizes_fractional(void) {
    nt_window_apply_sizes(601.0F, 401.0F, 1.5F);
    TEST_ASSERT_EQUAL_UINT32(902, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(602, g_nt_window.fb_height);
    TEST_ASSERT_TRUE(float_near(1.5F, g_nt_window.dpr, 1e-6F));
}

/* max_dpr=1.0 forces 1x */
void test_apply_sizes_max_dpr_one(void) {
    g_nt_window.max_dpr = 1.0F;
    nt_window_apply_sizes(800.0F, 600.0F, 2.0F);
    TEST_ASSERT_EQUAL_UINT32(800, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(600, g_nt_window.fb_height);
    TEST_ASSERT_TRUE(float_near(1.0F, g_nt_window.dpr, 1e-6F));
}

/* Multiple calls: fields fully overwritten */
void test_apply_sizes_overwrites(void) {
    nt_window_apply_sizes(800.0F, 600.0F, 2.0F);
    TEST_ASSERT_EQUAL_UINT32(1600, g_nt_window.fb_width);

    nt_window_apply_sizes(400.0F, 300.0F, 1.0F);
    TEST_ASSERT_EQUAL_UINT32(400, g_nt_window.width);
    TEST_ASSERT_EQUAL_UINT32(300, g_nt_window.height);
    TEST_ASSERT_EQUAL_UINT32(400, g_nt_window.fb_width);
    TEST_ASSERT_EQUAL_UINT32(300, g_nt_window.fb_height);
    TEST_ASSERT_TRUE(float_near(1.0F, g_nt_window.dpr, 1e-6F));
}

/* Stub: swap_buffers is no-op (does not crash) */
void test_stub_swap_buffers(void) {
    nt_window_swap_buffers();
    TEST_PASS(); /* No crash = pass */
}

/* Stub: should_close returns false */
void test_stub_should_close(void) { TEST_ASSERT_FALSE(nt_window_should_close()); }

/* Stub: request_close is no-op (does not crash) */
void test_stub_request_close(void) {
    nt_window_request_close();
    TEST_PASS();
}

/* Stub: set_vsync is no-op (does not crash) */
void test_stub_set_vsync(void) {
    nt_window_set_vsync(NT_VSYNC_ON);
    nt_window_set_vsync(NT_VSYNC_OFF);
    nt_window_set_vsync(NT_VSYNC_ADAPTIVE);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_apply_sizes_1x);
    RUN_TEST(test_apply_sizes_2x);
    RUN_TEST(test_apply_sizes_dpr_capped);
    RUN_TEST(test_apply_sizes_dpr_floor);
    RUN_TEST(test_apply_sizes_fractional);
    RUN_TEST(test_apply_sizes_max_dpr_one);
    RUN_TEST(test_apply_sizes_overwrites);
    RUN_TEST(test_stub_swap_buffers);
    RUN_TEST(test_stub_should_close);
    RUN_TEST(test_stub_request_close);
    RUN_TEST(test_stub_set_vsync);
    return UNITY_END();
}
