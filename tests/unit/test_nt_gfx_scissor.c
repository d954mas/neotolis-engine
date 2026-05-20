/* Scissor and viewport API round-trip via NT_TEST_ACCESS probes. */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "graphics/nt_gfx.h"

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){
        .max_shaders = 4,
        .max_pipelines = 4,
        .max_buffers = 8,
        .max_textures = 4,
        .max_meshes = 4,
    });
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) { nt_gfx_shutdown(); }

/* ---- GFX-01: nt_gfx_set_scissor round-trips via probe ---- */
static void test_set_scissor_round_trips(void) {
    nt_gfx_set_scissor(10, 20, 100, 200);
    int rect[4] = {0};
    nt_gfx_test_scissor_rect(rect);
    TEST_ASSERT_EQUAL_INT(10, rect[0]);
    TEST_ASSERT_EQUAL_INT(20, rect[1]);
    TEST_ASSERT_EQUAL_INT(100, rect[2]);
    TEST_ASSERT_EQUAL_INT(200, rect[3]);
}

/* ---- GFX-02: nt_gfx_set_scissor_enabled round-trips via probe ---- */
static void test_set_scissor_enabled_round_trips(void) {
    nt_gfx_set_scissor_enabled(true);
    TEST_ASSERT_TRUE(nt_gfx_test_scissor_enabled());
    nt_gfx_set_scissor_enabled(false);
    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());
}

/* ---- GFX-03: nt_gfx_set_viewport round-trips ---- */
static void test_set_viewport_round_trips(void) {
    nt_gfx_set_viewport(0, 0, 1280, 720);
    int rect[4] = {0};
    nt_gfx_test_viewport_rect(rect);
    TEST_ASSERT_EQUAL_INT(0, rect[0]);
    TEST_ASSERT_EQUAL_INT(0, rect[1]);
    TEST_ASSERT_EQUAL_INT(1280, rect[2]);
    TEST_ASSERT_EQUAL_INT(720, rect[3]);
}

/* ---- GFX-03 (independence): viewport survives scissor toggle ---- */
static void test_viewport_survives_scissor_toggle(void) {
    nt_gfx_set_viewport(0, 0, 800, 600);
    nt_gfx_set_scissor_enabled(true);
    nt_gfx_set_scissor_enabled(false);
    int rect[4] = {0};
    nt_gfx_test_viewport_rect(rect);
    TEST_ASSERT_EQUAL_INT(800, rect[2]);
    TEST_ASSERT_EQUAL_INT(600, rect[3]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_scissor_round_trips);
    RUN_TEST(test_set_scissor_enabled_round_trips);
    RUN_TEST(test_set_viewport_round_trips);
    RUN_TEST(test_viewport_survives_scissor_toggle);
    return UNITY_END();
}
