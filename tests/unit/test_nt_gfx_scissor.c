/*
 * Phase 51 / GFX-01..GFX-03 — Scissor and viewport API round-trip
 *
 * Verifies that the public nt_gfx_set_scissor / _set_scissor_enabled /
 * _set_viewport wrappers update the cached state observable via the
 * NT_GFX_TEST_ACCESS probes. Stub backend is used (no GL calls).
 *
 * Plan 03 adds restore_gpu test cases (GFX-04 / SC#5) to this same file.
 */

/* System headers BEFORE unity.h (per tests/CMakeLists.txt note on MSVC noreturn). */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"

/* Stub backend (nt_gfx_stub) is linked — provides backend symbols. The
 * NT_GFX_TEST_ACCESS define is set on this test target AND on nt_gfx_stub
 * via tests/CMakeLists.txt so probes are visible in both. */

void setUp(void) {
    /* nt_gfx_init returns void; success is signalled via g_nt_gfx.initialized.
     * Pool sizes sized for the largest test in this file: the restore_gpu
     * cases (Plan 03) init both sprite + text renderers, each allocating
     * 2 dynamic buffers (vbo + ibo); restore_gpu cycles shutdown + init so
     * the pool needs only 4 live buffers at any moment. */
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

/* ---- GFX-02 (default-disabled): begin_frame leaves scissor disabled (D-51-06) ---- */
static void test_begin_frame_disables_scissor(void) {
    nt_gfx_set_scissor_enabled(true);
    TEST_ASSERT_TRUE(nt_gfx_test_scissor_enabled());
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());
    /* Close the frame so the gfx state machine returns to IDLE before tearDown
     * shuts down — symmetric pairing matches the existing test_gfx precedent. */
    nt_gfx_end_frame();
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

/* ---- GFX-04: nt_sprite_renderer_restore_gpu() resets scissor (D-51-14 direct call) ---- */
static void test_sprite_renderer_restore_gpu_disables_scissor(void) {
    nt_sprite_renderer_desc_t desc = (nt_sprite_renderer_desc_t){.max_pipelines = 1};
    TEST_ASSERT_TRUE(nt_sprite_renderer_init(&desc) == NT_OK);

    /* Simulate stale post-context-loss state: cached flag says enabled. */
    nt_gfx_set_scissor_enabled(true);
    TEST_ASSERT_TRUE(nt_gfx_test_scissor_enabled());

    /* Direct invocation — no real WEBGL_lose_context simulation (D-51-14). */
    nt_sprite_renderer_restore_gpu();

    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());
    nt_sprite_renderer_shutdown();
}

/* ---- GFX-04: nt_text_renderer_restore_gpu() resets scissor (D-51-14 direct call) ----
 * nt_text_renderer_init asserts on nt_font_set_pre_flush_callback that the
 * font module is initialized, so init/shutdown font around the renderer
 * cycle. No font handle is touched by init/restore_gpu/shutdown — the test
 * exercises the scissor-reset code path only, not draw or font lookup. */
static void test_text_renderer_restore_gpu_disables_scissor(void) {
    nt_font_desc_t fdesc = (nt_font_desc_t){.max_fonts = 1};
    TEST_ASSERT_TRUE(nt_font_init(&fdesc) == NT_OK);
    nt_text_renderer_init();

    /* Simulate stale post-context-loss state. */
    nt_gfx_set_scissor_enabled(true);
    TEST_ASSERT_TRUE(nt_gfx_test_scissor_enabled());

    /* Direct invocation — symmetric to the sprite case. */
    nt_text_renderer_restore_gpu();

    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());

    nt_text_renderer_shutdown();
    nt_font_shutdown();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_scissor_round_trips);
    RUN_TEST(test_set_scissor_enabled_round_trips);
    RUN_TEST(test_begin_frame_disables_scissor);
    RUN_TEST(test_set_viewport_round_trips);
    RUN_TEST(test_viewport_survives_scissor_toggle);
    RUN_TEST(test_sprite_renderer_restore_gpu_disables_scissor);
    RUN_TEST(test_text_renderer_restore_gpu_disables_scissor);
    return UNITY_END();
}
