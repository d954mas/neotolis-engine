#include "graphics/nt_gfx.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

#if NT_GFX_COUNTERS_ENABLED
static void test_live_and_finalized_have_distinct_lifetimes(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});

    nt_gfx_observe_begin_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_counters_t before = nt_gfx_stats_read();
    nt_gfx_draw(0, 3);
    nt_gfx_draw_instanced(0, 6, 4);
    nt_gfx_counters_t after = nt_gfx_stats_read();
    TEST_ASSERT_EQUAL_UINT32(2, after.draw_calls - before.draw_calls);
    TEST_ASSERT_EQUAL_UINT64(27, after.vertices - before.vertices);
    TEST_ASSERT_EQUAL_UINT64(4, after.instances - before.instances);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_get_frame_draw_calls());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    const nt_gfx_frame_snapshot_t *last = nt_gfx_observe_end_frame();
    nt_gfx_frame_snapshot_t saved = *last;
    TEST_ASSERT_EQUAL_UINT64(1, last->counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(2, last->counters.draw_calls);

    nt_gfx_observe_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(1, last->counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_stats_read().draw_calls);
    last = nt_gfx_observe_end_frame();
    TEST_ASSERT_EQUAL_UINT64(2, last->counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(0, last->counters.draw_calls);
    TEST_ASSERT_EQUAL_UINT32(2, saved.counters.draw_calls);
}

static void test_instanced_products_are_widened_before_multiplication(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_gfx_observe_begin_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw_instanced(0, 65536, 65537);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(4295032832), nt_gfx_stats_read().vertices);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    (void)nt_gfx_observe_end_frame();
}
#else
static void test_disabled_counters_are_unavailable(void) {
    nt_gfx_observe_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_stats_read().availability);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_observe_end_frame()->counters.availability);
    TEST_ASSERT_FALSE(nt_gfx_upload_totals_read().available);
}
#endif

int main(void) {
    UNITY_BEGIN();
#if NT_GFX_COUNTERS_ENABLED
    RUN_TEST(test_live_and_finalized_have_distinct_lifetimes);
    RUN_TEST(test_instanced_products_are_widened_before_multiplication);
#else
    RUN_TEST(test_disabled_counters_are_unavailable);
#endif
    return UNITY_END();
}
