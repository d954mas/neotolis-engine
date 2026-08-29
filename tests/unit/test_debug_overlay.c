/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "metrics/nt_metrics.h"
#include "renderers/nt_text_renderer.h"
#include "debug_overlay/nt_debug_overlay.h"
#include "unity.h"
/* clang-format on */

/* Overlay reads its display data from nt_metrics: format_lines reads fps/cpu/gpu/draws + user
 * counters, so these tests feed nt_metrics (count + sample) and assert the HUD text reflects it.
 * The draw test still exercises the text_renderer bind path (gfx-backed). */

/* ---- Assert catching (setjmp/longjmp via hookable handler) ---- */

static jmp_buf s_assert_jmp;

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)expr;
    (void)file;
    (void)line;
    longjmp(s_assert_jmp, 1);
}

/* clang-format off */
#define EXPECT_ASSERT(code)                                                                    \
    do {                                                                                       \
        nt_assert_handler = test_assert_handler;                                               \
        if (setjmp(s_assert_jmp) == 0) {                                                       \
            code;                                                                              \
            nt_assert_handler = NULL;                                                          \
            TEST_FAIL_MESSAGE("Expected NT_ASSERT to fire");                                   \
        }                                                                                      \
        nt_assert_handler = NULL;                                                              \
    } while (0)
/* clang-format on */

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 16, .max_textures = 8, .max_meshes = 8, .max_render_targets = 16});
    nt_text_renderer_init();
    nt_text_renderer_test_reset_call_counters();
    nt_metrics_init();
}

void tearDown(void) {
    nt_text_renderer_shutdown();
    nt_gfx_shutdown();
}

#if NT_METRICS_ENABLED
/* Push one frame with the given scalars (gpu < 0 => N/A). */
static void push_frame(float frame_ms, float cpu_ms, float gpu_ms, uint32_t draws) {
    nt_metrics_frame_t f = {.frame_ms = frame_ms, .cpu_ms = cpu_ms, .gpu_ms = gpu_ms, .draw_calls = draws};
    nt_metrics_sample(&f);
}
#endif

/* ---- Test 1: init + shutdown round-trip ---- */

static void test_stats_init_shutdown(void) {
    nt_debug_overlay_desc_t desc = nt_debug_overlay_desc_defaults();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_debug_overlay_init(&desc));
    nt_debug_overlay_shutdown();
    /* Re-init must succeed (asserts not initialized first) */
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_debug_overlay_init(NULL));
    nt_debug_overlay_shutdown();
}

/* ---- Test 2: format_lines schema (reads nt_metrics) ---- */

static void test_stats_format_lines_schema(void) {
    nt_debug_overlay_init(NULL);

    char buf[512];
    uint32_t n = nt_debug_overlay_format_lines(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "FPS:"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "CPU:"));
    /* No frame pushed yet -> nt_metrics_last() gpu sentinel -1 -> "GPU: N/A". */
    TEST_ASSERT_NOT_NULL(strstr(buf, "GPU: N/A"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Draws:"));

    nt_debug_overlay_shutdown();
}

#if NT_METRICS_ENABLED
/* ---- Test 3: format reflects the last-frame cpu/gpu/draws from nt_metrics ----
   The format tests below need real nt_metrics bodies; the OFF mirror (NT_METRICS_ENABLED=0) has no-op
   stubs (fps 0 / gpu sentinel / no counters), so they are gated out there — the init/shutdown, schema
   (labels present), and draw-bind tests stay live in both configs. */

static void test_stats_format_reflects_last_frame(void) {
    nt_debug_overlay_init(NULL);

    push_frame(1000.0F / 60.0F, 7.25F, 3.5F, 42U);
    char buf[512];
    uint32_t n = nt_debug_overlay_format_lines(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "CPU: 7.25 ms"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "GPU: 3.50 ms")); /* real gpu sample -> not N/A */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Draws: 42"));

    nt_debug_overlay_shutdown();
}

/* ---- Test 4: fps line reflects the nt_metrics rolling avg ---- */

static void test_stats_format_reflects_fps(void) {
    nt_debug_overlay_init(NULL);

    for (int i = 0; i < 60; i++) {
        push_frame(1000.0F / 60.0F, 5.0F, -1.0F, 0U); /* 60 fps frames */
    }
    char buf[512];
    (void)nt_debug_overlay_format_lines(buf, sizeof(buf));
    /* fps prints with %.1f; ~60 fps. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "FPS: 60.0"));

    nt_debug_overlay_shutdown();
}

/* ---- Test 5: user counters in the HUD, exact int + decimals for floats ---- */

static void test_stats_user_counters(void) {
    nt_debug_overlay_init(NULL);

    nt_metrics_count("bunnies", 1000U);
    nt_metrics_count("bunnies", 2000U);
    nt_metrics_count("atlas_quality", 1U);
    nt_metrics_count_f("frame_ms", 16.667);

    char buf[512];
    uint32_t n = nt_debug_overlay_format_lines(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "bunnies: 2000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "atlas_quality: 1"));
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "bunnies: 1000"), "old value must be overwritten");
    /* float counter prints with decimals; int counter has no decimal point. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "frame_ms: 16.667"));
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "atlas_quality: 1.0"), "int counter must not render decimals");

    nt_debug_overlay_shutdown();
}

/* ---- Test 6: int user counter exact past 2^53 in the HUD ---- */

static void test_stats_user_counter_uint64_exact(void) {
    nt_debug_overlay_init(NULL);

    const uint64_t big = (1ULL << 53) + 1ULL; /* a double would lose the low bit */
    nt_metrics_count("ticks", big);
    char buf[512];
    (void)nt_debug_overlay_format_lines(buf, sizeof(buf));
    char expect[64];
    (void)snprintf(expect, sizeof(expect), "ticks: %llu", (unsigned long long)big);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, expect), "int counter must print the exact uint64, not a double-squashed value");

    nt_debug_overlay_shutdown();
}
#endif /* NT_METRICS_ENABLED */

/* ---- explicit set_material AND set_font on draw ----
 * nt_debug_overlay_draw must call BOTH setters every time even when the material/font id matches the
 * previous frame, defeating nt_text_renderer's change-detection early-out. */
static void test_stats_draw_pitfall9_explicit_set_calls(void) {
    nt_debug_overlay_init(NULL);

    nt_text_renderer_test_reset_call_counters();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_set_material_calls());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_set_font_calls());

    nt_material_t mat = {0};
    nt_font_t font = {0};
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float white[4] = {1, 1, 1, 1};

    /* set_material fail-fast asserts on the {0} handle BEFORE the same-handle early-return, so the
     * counter still increments (proving draw doesn't cache) and set_font is never reached. */
    EXPECT_ASSERT(nt_debug_overlay_draw(mat, font, identity, 16.0F, white));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_set_material_calls());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_set_font_calls());

    EXPECT_ASSERT(nt_debug_overlay_draw(mat, font, identity, 16.0F, white));
    TEST_ASSERT_EQUAL_UINT32(2U, nt_text_renderer_test_set_material_calls());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_set_font_calls());

    nt_debug_overlay_shutdown();
}

/* ---- main ---- */

int main(void) {
    /* Force unbuffered stdout so diagnostics survive a SIGILL from a stray NT_ASSERT. */
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    UNITY_BEGIN();
    RUN_TEST(test_stats_init_shutdown);
    RUN_TEST(test_stats_format_lines_schema);
#if NT_METRICS_ENABLED
    RUN_TEST(test_stats_format_reflects_last_frame);
    RUN_TEST(test_stats_format_reflects_fps);
    RUN_TEST(test_stats_user_counters);
    RUN_TEST(test_stats_user_counter_uint64_exact);
#endif
    RUN_TEST(test_stats_draw_pitfall9_explicit_set_calls);
    return UNITY_END();
}
