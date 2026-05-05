/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "stats/nt_stats.h"
#include "unity.h"
/* clang-format on */

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

/* ---- Unity setUp / tearDown ----
 * nt_gfx is needed because nt_stats reads nt_gfx_get_frame_draw_calls.
 * No font/material/resource init required for data-only tests; the Pitfall 9
 * draw test passes nt_material_t{0} + nt_font_t{0}, the text_renderer counts
 * the call entry regardless. */

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_pipelines = 4, .max_buffers = 16, .max_textures = 8, .max_meshes = 8});
    nt_text_renderer_init();
    nt_text_renderer_test_reset_call_counters();
}

void tearDown(void) {
    nt_text_renderer_shutdown();
    nt_gfx_shutdown();
}

/* ---- Test 1: init + shutdown round-trip ---- */

static void test_stats_init_shutdown(void) {
    nt_stats_desc_t desc = nt_stats_desc_defaults();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_stats_init(&desc));
    nt_stats_shutdown();
    /* Re-init must succeed (asserts not initialized first) */
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_stats_init(NULL));
    nt_stats_shutdown();
}

/* ---- Test 2: rolling FPS avg over fps_window=60 frames (DEMO-05) ---- */

/* Helper: assert |actual - expected| <= tolerance (Unity's float macros are
 * compiled out via UNITY_EXCLUDE_FLOAT — use this instead). */
static void assert_float_within(float tolerance, float expected, float actual, const char *msg) {
    float diff = actual - expected;
    if (diff < 0.0F) {
        diff = -diff;
    }
    TEST_ASSERT_TRUE_MESSAGE(diff <= tolerance, msg);
}

static void test_stats_fps_rolling_avg(void) {
    nt_stats_desc_t desc = nt_stats_desc_defaults();
    desc.fps_window = 60;
    desc.enable_throughput_log = false; /* keep test output quiet */
    nt_stats_init(&desc);

    /* Drive 60 frames at 16.67 ms (= 60 fps) via test injection */
    for (int i = 0; i < 60; i++) {
        nt_stats_test_inject_frame(1.0F / 60.0F);
    }
    float fps = nt_stats_get_fps();
    assert_float_within(0.5F, 60.0F, fps, "fps avg should be ~60 after 60 frames at 60 fps");

    /* Drive another 60 frames at 33.33 ms (= 30 fps); window fully replaces */
    for (int i = 0; i < 60; i++) {
        nt_stats_test_inject_frame(1.0F / 30.0F);
    }
    fps = nt_stats_get_fps();
    assert_float_within(0.5F, 30.0F, fps, "fps avg should be ~30 after window fully replaced with 30 fps frames");

    /* Discrete window — NOT instantaneous: half-replace ring with new dt
     * (30 frames at 60 fps after 30 frames at 30 fps still in ring) → avg
     * lands between 30 and 60. */
    for (int i = 0; i < 30; i++) {
        nt_stats_test_inject_frame(1.0F / 60.0F);
    }
    fps = nt_stats_get_fps();
    TEST_ASSERT_TRUE_MESSAGE(fps > 30.0F && fps < 60.0F, "FPS should reflect window of mixed rates (proves NOT instantaneous)");

    nt_stats_shutdown();
}

/* ---- Test 3: user counters set/update + readable in format (DEMO-04) ---- */

static void test_stats_user_counters(void) {
    nt_stats_init(NULL);

    nt_stats_count("bunnies", 1000);
    nt_stats_count("bunnies", 2000);    /* update */
    nt_stats_count("atlas_quality", 1); /* second counter */

    char buf[512];
    uint32_t n = nt_stats_format_lines(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "bunnies: 2000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "atlas_quality: 1"));
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "bunnies: 1000"), "old value must be overwritten");

    nt_stats_shutdown();
}

/* ---- Test 4: user-counter capacity overflow asserts ---- */

static void test_stats_user_counter_overflow_assert(void) {
    nt_stats_desc_t desc = nt_stats_desc_defaults();
    desc.user_counter_capacity = 2;
    nt_stats_init(&desc);

    nt_stats_count("a", 1);
    nt_stats_count("b", 2);
    /* Adding a third distinct name must trip NT_ASSERT */
    EXPECT_ASSERT(nt_stats_count("c", 3));

    /* Need to shutdown manually because EXPECT_ASSERT longjmps past it */
    nt_stats_shutdown();
}

/* ---- Test 5: format_lines schema (DEMO-04) ---- */

static void test_stats_format_lines_schema(void) {
    nt_stats_init(NULL);

    char buf[512];
    uint32_t n = nt_stats_format_lines(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "FPS:"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "CPU:"));
    /* GPU is -1.0 (Pitfall 5: no timer query yet) → emitted as "GPU: N/A" */
    TEST_ASSERT_NOT_NULL(strstr(buf, "GPU: N/A"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Draws:"));

    nt_stats_shutdown();
}

/* ---- Test 6: throughput-log periodicity (DEMO-06)
 *
 * Manual-only verification: the project has no log-capture harness.
 * A future plan can add NT_LOG_TEST_SINK or similar; until then this
 * test merely exercises the code path to make sure it doesn't crash.
 */

static void test_stats_throughput_log_period(void) {
    nt_stats_desc_t desc = nt_stats_desc_defaults();
    desc.throughput_log_period = 10;
    desc.enable_throughput_log = true;
    nt_stats_init(&desc);

    nt_stats_count("bunnies", 42);
    nt_stats_count("atlas_quality", 0);

    /* Drive 25 frames; logs at frame 10 and 20 emit via NT_LOG_INFO.
     * No assertion — just smoke-test the path without crashing. */
    for (int i = 0; i < 25; i++) {
        nt_stats_test_inject_frame(1.0F / 60.0F);
        /* nt_stats_test_inject_frame doesn't emit the log (only frame_end does);
         * we exercise frame_begin/frame_end pair instead for log emission. */
        nt_stats_frame_begin();
        nt_stats_frame_end();
    }

    nt_stats_shutdown();
}

/* ---- Test 7: draw call count from nt_gfx (DEMO-04) ---- */

static void test_stats_draw_calls_from_gfx(void) {
    nt_stats_desc_t desc = nt_stats_desc_defaults();
    desc.enable_throughput_log = false;
    nt_stats_init(&desc);

    /* Mirror test_gfx_frame_draw_calls setup: minimal pipeline so the four
     * draw entry points pass their NT_ASSERT(bound_pipeline != 0) gate. */
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .format = NT_FORMAT_FLOAT3}}},
    });

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);

    /* Make 5 draw calls via the public API; each increments the gfx counter */
    nt_gfx_draw(0, 0);
    nt_gfx_draw_indexed(0, 0, 0);
    nt_gfx_draw_instanced(0, 0, 0);
    nt_gfx_draw_indexed_instanced(0, 0, 0, 0);
    nt_gfx_draw(0, 0);
    /* nt_gfx_get_frame_draw_calls reports 5 */
    TEST_ASSERT_EQUAL_UINT32(5U, nt_gfx_get_frame_draw_calls());

    nt_stats_frame_begin();
    nt_stats_frame_end();
    TEST_ASSERT_EQUAL_UINT32(5U, nt_stats_get_draw_calls());

    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_destroy_pipeline(pip);
    nt_gfx_destroy_shader(vs);
    nt_gfx_destroy_shader(fs);
    nt_stats_shutdown();
}

/* ---- Test 8: Pitfall 9 / Issue 2 — explicit set_material AND set_font calls
 *
 * nt_stats_draw must call BOTH setters every time even when the material/font
 * id matches the previous frame, defeating the change-detection early-out in
 * nt_text_renderer. We verify by counting entries (not state changes) into
 * each setter via test-only counters.
 */

static void test_stats_draw_pitfall9_explicit_set_calls(void) {
    nt_stats_init(NULL);

    nt_text_renderer_test_reset_call_counters();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_set_material_calls());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_set_font_calls());

    /* Same handle values for both calls — if early-out were honoured the
     * second call would NOT increment the counters. */
    nt_material_t mat = {0};
    nt_font_t font = {0};
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float white[4] = {1, 1, 1, 1};

    nt_stats_draw(mat, font, identity, 16.0F, white);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_set_material_calls());
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_set_font_calls());

    /* Second call with SAME material/font: still increments because the
     * caller (nt_stats_draw) ALWAYS calls both setters explicitly. */
    nt_stats_draw(mat, font, identity, 16.0F, white);
    TEST_ASSERT_EQUAL_UINT32(2U, nt_text_renderer_test_set_material_calls());
    TEST_ASSERT_EQUAL_UINT32(2U, nt_text_renderer_test_set_font_calls());

    nt_stats_shutdown();
}

/* ---- main ---- */

int main(void) {
    /* Force unbuffered stdout so diagnostics survive a SIGILL from a stray
     * NT_ASSERT in any test; matches the pattern used in test_text_renderer. */
    setvbuf(stdout, NULL, _IONBF, 0);
    UNITY_BEGIN();
    RUN_TEST(test_stats_init_shutdown);
    RUN_TEST(test_stats_fps_rolling_avg);
    RUN_TEST(test_stats_user_counters);
    RUN_TEST(test_stats_user_counter_overflow_assert);
    RUN_TEST(test_stats_format_lines_schema);
    RUN_TEST(test_stats_throughput_log_period);
    RUN_TEST(test_stats_draw_calls_from_gfx);
    RUN_TEST(test_stats_draw_pitfall9_explicit_set_calls);
    return UNITY_END();
}
