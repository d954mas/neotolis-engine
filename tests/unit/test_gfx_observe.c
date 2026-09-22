#include "graphics/nt_gfx.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 64;
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

#if NT_GFX_CAPTURE_ENABLED
static void test_draw_trace_preserves_arguments_and_live_prefix(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_gfx_capture_set_enabled(true);
    nt_gfx_observe_begin_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    uint32_t start = nt_gfx_capture_read().count;
    nt_gfx_draw_instanced(7, 9, 5);
    nt_gfx_capture_view_t live = nt_gfx_capture_read();
    bool found = false;
    for (uint32_t i = start; i < live.count; i++) {
        const nt_gfx_event_t *e = &live.events[i];
        if (e->kind == NT_GFX_EVENT_BEGIN && e->operation == NT_GFX_OP_DRAW_INSTANCED) {
            TEST_ASSERT_EQUAL_UINT32(7, e->data.draw.first);
            TEST_ASSERT_EQUAL_UINT32(9, e->data.draw.vertices);
            TEST_ASSERT_EQUAL_UINT32(5, e->data.draw.instances);
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found);
    nt_gfx_event_t copy[64];
    TEST_ASSERT_TRUE(live.count <= 64);
    memcpy(copy, live.events, live.count * sizeof(copy[0]));
    nt_gfx_draw(1, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    (void)nt_gfx_observe_end_frame();
    TEST_ASSERT_EQUAL_MEMORY(copy, live.events, live.count * sizeof(copy[0]));
}

static void test_capture_prefix_lifetime_and_saved_snapshot(void) {
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_capture_read().count);
    nt_gfx_capture_set_enabled(true);
    nt_gfx_observe_begin_frame();
    nt_gfx_capture_view_t before = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_RECORDING, before.phase);
    TEST_ASSERT_TRUE(before.count > 0);
    nt_gfx_event_t saved = before.events[0];
    nt_gfx_frame_snapshot_t snapshot = *nt_gfx_observe_end_frame();
    nt_gfx_capture_view_t after = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_FINALIZED, after.phase);
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_COMPLETE, after.status);
    TEST_ASSERT_EQUAL_UINT64(snapshot.counters.frame_sequence, after.frame_sequence);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &before.events[0], sizeof(saved));
    TEST_ASSERT_TRUE(after.count > before.count);
    TEST_ASSERT_EQUAL_MEMORY(&snapshot, &after.snapshot, sizeof(snapshot));

    nt_gfx_capture_set_enabled(false);
    nt_gfx_observe_begin_frame();
    (void)nt_gfx_observe_end_frame();
    nt_gfx_capture_view_t retained = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT64(after.frame_sequence, retained.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(after.count, retained.count);
    TEST_ASSERT_EQUAL_MEMORY(&after.snapshot, &retained.snapshot, sizeof(snapshot));
    TEST_ASSERT_EQUAL_MEMORY(&saved, &retained.events[0], sizeof(saved));
}

static void test_capture_overflow_does_not_stop_counters(void) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 1;
    nt_gfx_init(&desc);
    nt_gfx_capture_set_enabled(true);
    nt_gfx_observe_begin_frame();
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
    const nt_gfx_frame_snapshot_t *snapshot = nt_gfx_observe_end_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(1, capture.count);
    TEST_ASSERT_TRUE(capture.overflow);
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_TRUNCATED, capture.status);
    TEST_ASSERT_EQUAL_UINT64(snapshot->counters.frame_sequence, capture.snapshot.counters.frame_sequence);
#if NT_GFX_COUNTERS_ENABLED
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_COUNTERS_FRONTEND, snapshot->counters.availability);
#endif
}

static void test_capture_toggle_waits_until_next_begin(void) {
    nt_gfx_observe_begin_frame();
    nt_gfx_capture_set_enabled(true);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_capture_read().count);
    (void)nt_gfx_observe_end_frame();
    nt_gfx_observe_begin_frame();
    nt_gfx_capture_set_enabled(false);
    TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_RECORDING, nt_gfx_capture_read().phase);
    (void)nt_gfx_observe_end_frame();
    uint64_t sequence = nt_gfx_capture_read().frame_sequence;
    nt_gfx_observe_begin_frame();
    (void)nt_gfx_observe_end_frame();
    TEST_ASSERT_EQUAL_UINT64(sequence, nt_gfx_capture_read().frame_sequence);
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
#if NT_GFX_CAPTURE_ENABLED
    RUN_TEST(test_draw_trace_preserves_arguments_and_live_prefix);
    RUN_TEST(test_capture_prefix_lifetime_and_saved_snapshot);
    RUN_TEST(test_capture_overflow_does_not_stop_counters);
    RUN_TEST(test_capture_toggle_waits_until_next_begin);
#endif
    return UNITY_END();
}
