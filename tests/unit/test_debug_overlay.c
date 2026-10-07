/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "color/nt_color.h"
#include "test_helpers/nt_gfx_test_desc.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "metrics/nt_metrics.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "debug_overlay/nt_debug_overlay.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/nt_test_font_blob.h"
#include "unity.h"
/* clang-format on */

/* Overlay reads its display data from nt_metrics: format_lines reads fps/cpu/gpu/draws + user
 * counters, so these tests feed nt_metrics (count + sample) and assert the HUD text reflects it. */

void setUp(void) {
    nt_gfx_init(&NT_GFX_TEST_DESC(.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 16, .max_textures = 8, .max_meshes = 8, .max_vertex_inputs = 16, .max_render_targets = 16));
    nt_gfx_begin_frame();
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

/* ---- init + shutdown round-trip ---- */

static void test_stats_init_shutdown(void) {
    nt_debug_overlay_init();
    nt_debug_overlay_shutdown();
    /* Re-init must succeed (asserts not initialized first) */
    nt_debug_overlay_init();
    nt_debug_overlay_shutdown();
}

/* ---- format_lines schema (reads nt_metrics) ---- */

static void test_stats_format_lines_schema(void) {
    nt_debug_overlay_init();

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
/* ---- format reflects the last-frame cpu/gpu/draws from nt_metrics ----
   The format tests below need real nt_metrics bodies; the OFF mirror (NT_METRICS_ENABLED=0) has no-op
   stubs (fps 0 / gpu sentinel / no counters), so they are gated out there — the init/shutdown, schema
   (labels present), and draw-bind tests stay live in both configs. */

static void test_stats_format_reflects_last_frame(void) {
    nt_debug_overlay_init();

    push_frame(1000.0F / 60.0F, 7.25F, 3.5F, 42U);
    char buf[512];
    uint32_t n = nt_debug_overlay_format_lines(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "CPU: 7.25 ms"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "GPU: 3.50 ms")); /* real gpu sample -> not N/A */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Draws: 42"));

    nt_debug_overlay_shutdown();
}

/* ---- fps line reflects the nt_metrics rolling avg ---- */

static void test_stats_format_reflects_fps(void) {
    nt_debug_overlay_init();

    for (int i = 0; i < 60; i++) {
        push_frame(1000.0F / 60.0F, 5.0F, -1.0F, 0U); /* 60 fps frames */
    }
    char buf[512];
    (void)nt_debug_overlay_format_lines(buf, sizeof(buf));
    /* fps prints with %.1f; ~60 fps. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "FPS: 60.0"));

    nt_debug_overlay_shutdown();
}

/* ---- user counters in the HUD, exact int + decimals for floats ---- */

static void test_stats_user_counters(void) {
    nt_debug_overlay_init();

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

/* ---- int user counter exact past 2^53 in the HUD ---- */

static void test_stats_user_counter_uint64_exact(void) {
    nt_debug_overlay_init();

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

/* ---- draw records the HUD through the text renderer ---- */

static void test_draw_records_a_text_draw(void) {
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 2});
    nt_debug_overlay_init();

    const nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_curve_texture"}, 1);
    const nt_material_t material = nt_material_create(&(nt_material_create_desc_t){.program = program, .blend = nt_blend_alpha(), .cull_mode = NT_CULL_NONE});
    uint32_t blob_size = 0;
    uint8_t *blob = nt_test_font_blob('F', 'F', &blob_size); /* the HUD starts with "FPS:" */
    const nt_font_t font = nt_font_create(&(nt_font_create_desc_t){.max_glyphs = 16});
    nt_font_add(font, nt_font_test_resource(nt_font_test_register_data(blob, blob_size)));
    nt_resource_step();
    nt_font_step();

    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_fake_draw_trace_reset(true);
    nt_debug_overlay_draw(material, font, identity, 16.0F, NT_RGBA8(255, 255, 255, 255));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(program.id, nt_gfx_fake_draw_trace_at(0).program.id);

    nt_debug_overlay_shutdown();
    nt_font_destroy(font);
    nt_font_shutdown();
    free(blob);
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_hash_shutdown();
}

/* ---- main ---- */

int main(void) {
    /* Force unbuffered stdout so diagnostics survive a SIGILL from a stray NT_ASSERT. */
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    UNITY_BEGIN();
    RUN_TEST(test_stats_init_shutdown);
    RUN_TEST(test_stats_format_lines_schema);
    RUN_TEST(test_draw_records_a_text_draw);
#if NT_METRICS_ENABLED
    RUN_TEST(test_stats_format_reflects_last_frame);
    RUN_TEST(test_stats_format_reflects_fps);
    RUN_TEST(test_stats_user_counters);
    RUN_TEST(test_stats_user_counter_uint64_exact);
#endif
    return UNITY_END();
}
