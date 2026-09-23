#include "graphics/nt_gfx.h"
#include "test_helpers/nt_assert_trap.h"
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

static void draw_setup(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
}

static void draw_teardown(void) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_render_frames_sum_and_only_begin_tick_resets(void) {
    nt_gfx_begin_tick();
    TEST_ASSERT_EQUAL_UINT64(1, g_nt_gfx.counters.frame_sequence);
    draw_setup();
    nt_gfx_draw(0, 3);
    draw_teardown();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.draw_calls);
    nt_gfx_end_frame();
    draw_setup();
    nt_gfx_draw_instanced(0, 6, 4);
    draw_teardown();
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_COMPLETE, g_nt_gfx.last_frame.status);
    TEST_ASSERT_EQUAL_UINT64(1, g_nt_gfx.last_frame.counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.last_frame.counters.draw_calls);
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.last_frame.counters.draw_calls_instanced);
    TEST_ASSERT_EQUAL_UINT64(27, g_nt_gfx.last_frame.counters.vertices);
    TEST_ASSERT_EQUAL_UINT64(4, g_nt_gfx.last_frame.counters.instances);
#if NT_GFX_COUNTERS_ENABLED
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.last_frame.counters.pipeline_requests);
#else
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.last_frame.counters.pipeline_requests);
#endif
    /* Closing a tick copies; it does not reset. */
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.counters.draw_calls);

    nt_gfx_begin_tick();
    TEST_ASSERT_EQUAL_UINT64(2, g_nt_gfx.counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.counters.draw_calls);
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.last_frame.counters.draw_calls);
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL_UINT64(2, g_nt_gfx.last_frame.counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.last_frame.counters.draw_calls);
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_COMPLETE, g_nt_gfx.last_frame.status);
}

static void test_instanced_products_are_widened_before_multiplication(void) {
    nt_gfx_begin_tick();
    draw_setup();
    nt_gfx_draw_instanced(0, 65536, 65537);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(4295032832), g_nt_gfx.counters.vertices);
    draw_teardown();
    nt_gfx_end_tick();
}

static void test_availability_is_fixed_per_build_and_backend(void) {
    const uint32_t expected = NT_GFX_COUNTERS_DRAWS | (NT_GFX_COUNTERS_ENABLED ? NT_GFX_COUNTERS_FRONTEND : 0U);
    TEST_ASSERT_EQUAL_UINT32(expected, g_nt_gfx.counters.availability);
    nt_gfx_begin_tick();
    TEST_ASSERT_EQUAL_UINT32(expected, g_nt_gfx.counters.availability);
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL_UINT32(expected, g_nt_gfx.last_frame.counters.availability);
    /* The fake issues no GL calls, so backend payload totals stay unavailable. */
    TEST_ASSERT_FALSE(nt_gfx_upload_totals_read().available);
}

static void test_loss_aborts_the_tick_and_restore_completes_it(void) {
    nt_gfx_begin_tick();
    nt_gfx_fake_set_context_lost(true);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8}).id);
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_ABORTED, g_nt_gfx.last_frame.status);

    nt_gfx_begin_tick();
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_ABORTED, g_nt_gfx.last_frame.status);

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_tick();
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    nt_gfx_end_frame();
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_COMPLETE, g_nt_gfx.last_frame.status);
}

static void test_shutdown_discards_an_open_tick(void) {
    nt_gfx_begin_tick();
    nt_gfx_end_tick();
    nt_gfx_begin_tick();
    draw_setup();
    nt_gfx_draw(0, 3);
    draw_teardown();
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_UNAVAILABLE, g_nt_gfx.last_frame.status);
    TEST_ASSERT_EQUAL_UINT64(0, g_nt_gfx.last_frame.counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.counters.draw_calls);
    nt_gfx_begin_tick();
    TEST_ASSERT_EQUAL_UINT64(1, g_nt_gfx.counters.frame_sequence);
    nt_gfx_end_tick();
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
static void test_tick_contract_asserts(void) {
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_frame());
    NT_TEST_EXPECT_ASSERT(nt_gfx_end_frame());
    NT_TEST_EXPECT_ASSERT(nt_gfx_end_tick());
    nt_gfx_begin_tick();
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_tick());
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
    nt_gfx_end_tick();
}
#endif

#if NT_GFX_CAPTURE_ENABLED
static void test_resource_operations_keep_published_handles_after_destroy(void) {
    nt_gfx_capture_set_enabled(true);
    nt_gfx_begin_tick();
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 24});
    nt_gfx_destroy_buffer(buffer);
    nt_gfx_end_tick();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    bool created = false;
    bool destroyed = false;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->kind == NT_GFX_EVENT_RESULT && e->object_kind == NT_GFX_OBJECT_BUFFER && e->object == buffer.id && e->reason == NT_GFX_REASON_ACCEPTED) {
            created |= e->operation == NT_GFX_OP_CREATE;
            destroyed |= e->operation == NT_GFX_OP_DESTROY;
        }
    }
    TEST_ASSERT_TRUE(created);
    TEST_ASSERT_TRUE(destroyed);
}

static void test_loss_detected_during_creation_aborts_observation(void) {
    nt_gfx_capture_set_enabled(true);
    nt_gfx_begin_tick();
    nt_gfx_fake_set_context_lost(true);
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8});
    TEST_ASSERT_EQUAL_UINT32(0, buffer.id);
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_ABORTED, g_nt_gfx.last_frame.status);
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_ABORTED, nt_gfx_capture_read().status);
}

static void test_restore_frame_completes_under_new_context_sequence(void) {
    nt_gfx_capture_set_enabled(true);
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_tick();
    uint64_t lost_sequence = nt_gfx_capture_read().events[0].context_sequence;
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_ABORTED, g_nt_gfx.last_frame.status);

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_tick();
    TEST_ASSERT_EQUAL_UINT64(lost_sequence, nt_gfx_capture_read().events[0].context_sequence);
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    nt_gfx_end_frame();
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_COMPLETE, g_nt_gfx.last_frame.status);
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_COMPLETE, capture.status);
    TEST_ASSERT_FALSE(capture.overflow);
    const nt_gfx_event_t *last = &capture.events[capture.count - 1];
    TEST_ASSERT_EQUAL(NT_GFX_EVENT_RESULT, last->kind);
    TEST_ASSERT_EQUAL(NT_GFX_OP_FRAME, last->operation);
    TEST_ASSERT_EQUAL_UINT64(lost_sequence + 1, last->context_sequence);
}

static void test_overflow_and_loss_finalize_aborted(void) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 1;
    nt_gfx_init(&desc);
    nt_gfx_capture_set_enabled(true);
    nt_gfx_begin_tick();
    nt_gfx_fake_set_context_lost(true);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8}).id);
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_ABORTED, g_nt_gfx.last_frame.status);
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_TRUE(capture.overflow);
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_ABORTED, capture.status);
}

static void test_sampler_cache_hit_defines_nothing(void) {
    const nt_sampler_desc_t sampler_desc = {.min_filter = NT_FILTER_LINEAR, .mag_filter = NT_FILTER_LINEAR};
    nt_sampler_t sampler = nt_gfx_make_sampler(&sampler_desc);
    nt_gfx_capture_set_enabled(true);
    nt_gfx_begin_tick();
    uint32_t start = nt_gfx_capture_read().count;
    TEST_ASSERT_EQUAL_UINT32(sampler.id, nt_gfx_make_sampler(&sampler_desc).id);
    nt_gfx_end_tick();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    bool cache = false;
    for (uint32_t i = start; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        TEST_ASSERT_FALSE(e->kind == NT_GFX_EVENT_DEFINITION && e->object_kind == NT_GFX_OBJECT_SAMPLER);
        cache |= e->kind == NT_GFX_EVENT_RESULT && e->object == sampler.id && e->reason == NT_GFX_REASON_CACHE;
    }
    TEST_ASSERT_TRUE(cache);
}

static void test_capture_defines_inherited_resources_and_unknown_scissor(void) {
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 24});
    nt_gfx_capture_set_enabled(true);
    nt_gfx_begin_tick();
    nt_gfx_capture_view_t initial = nt_gfx_capture_read();
    bool buffer_found = false;
    bool scissor_unknown = false;
    for (uint32_t i = 0; i < initial.count; i++) {
        const nt_gfx_event_t *e = &initial.events[i];
        if (e->kind == NT_GFX_EVENT_DEFINITION && e->object_kind == NT_GFX_OBJECT_BUFFER && e->object == buffer.id) {
            TEST_ASSERT_EQUAL_UINT64(24, e->data.resource.size);
            buffer_found = true;
        }
        if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_SCISSOR && e->reason == NT_GFX_REASON_UNKNOWN) {
            scissor_unknown = true;
        }
    }
    TEST_ASSERT_TRUE(buffer_found);
    TEST_ASSERT_TRUE(scissor_unknown);
    nt_gfx_destroy_buffer(buffer);
    nt_gfx_end_tick();
}

static void test_draw_trace_preserves_arguments_and_live_prefix(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_gfx_capture_set_enabled(true);
    nt_gfx_begin_tick();
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
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL_MEMORY(copy, live.events, live.count * sizeof(copy[0]));
}

static void test_capture_prefix_lifetime_and_saved_snapshot(void) {
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_capture_read().count);
    nt_gfx_capture_set_enabled(true);
    nt_gfx_begin_tick();
    nt_gfx_capture_view_t before = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_RECORDING, before.phase);
    TEST_ASSERT_TRUE(before.count > 0);
    nt_gfx_event_t saved = before.events[0];
    nt_gfx_end_tick();
    nt_gfx_frame_snapshot_t snapshot = g_nt_gfx.last_frame;
    nt_gfx_capture_view_t after = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_FINALIZED, after.phase);
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_COMPLETE, after.status);
    TEST_ASSERT_EQUAL_UINT64(snapshot.counters.frame_sequence, after.frame_sequence);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &before.events[0], sizeof(saved));
    TEST_ASSERT_TRUE(after.count > before.count);
    TEST_ASSERT_EQUAL_MEMORY(&snapshot, &after.snapshot, sizeof(snapshot));

    nt_gfx_capture_set_enabled(false);
    nt_gfx_begin_tick();
    nt_gfx_end_tick();
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
    nt_gfx_begin_tick();
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
    nt_gfx_end_tick();
    const nt_gfx_frame_snapshot_t *snapshot = &g_nt_gfx.last_frame;
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(1, capture.count);
    TEST_ASSERT_TRUE(capture.overflow);
    TEST_ASSERT_EQUAL(NT_GFX_FRAME_TRUNCATED, capture.status);
    TEST_ASSERT_EQUAL_UINT64(snapshot->counters.frame_sequence, capture.snapshot.counters.frame_sequence);
#if NT_GFX_COUNTERS_ENABLED
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_COUNTERS_DRAWS | NT_GFX_COUNTERS_FRONTEND, snapshot->counters.availability);
#endif
}

static void test_capture_toggle_waits_until_next_begin(void) {
    nt_gfx_begin_tick();
    nt_gfx_capture_set_enabled(true);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_capture_read().count);
    nt_gfx_end_tick();
    nt_gfx_begin_tick();
    nt_gfx_capture_set_enabled(false);
    TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_RECORDING, nt_gfx_capture_read().phase);
    nt_gfx_end_tick();
    uint64_t sequence = nt_gfx_capture_read().frame_sequence;
    nt_gfx_begin_tick();
    nt_gfx_end_tick();
    TEST_ASSERT_EQUAL_UINT64(sequence, nt_gfx_capture_read().frame_sequence);
}

static void test_exact_capacity_and_one_record_short(void) {
    nt_gfx_capture_set_enabled(true);
    nt_gfx_begin_tick();
    nt_gfx_end_tick();
    uint32_t needed = nt_gfx_capture_read().count;
    TEST_ASSERT_GREATER_THAN_UINT32(1, needed);
    for (uint32_t missing = 0; missing < 2; missing++) {
        nt_gfx_shutdown();
        nt_gfx_desc_t desc = nt_gfx_desc_defaults();
        desc.capture_capacity = needed - missing;
        nt_gfx_init(&desc);
        nt_gfx_capture_set_enabled(true);
        nt_gfx_begin_tick();
        nt_gfx_end_tick();
        nt_gfx_capture_view_t view = nt_gfx_capture_read();
        TEST_ASSERT_EQUAL_UINT32(needed - missing, view.count);
        TEST_ASSERT_EQUAL(missing != 0, view.overflow);
        TEST_ASSERT_EQUAL(missing != 0 ? NT_GFX_FRAME_TRUNCATED : NT_GFX_FRAME_COMPLETE, view.status);
    }
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_render_frames_sum_and_only_begin_tick_resets);
    RUN_TEST(test_instanced_products_are_widened_before_multiplication);
    RUN_TEST(test_availability_is_fixed_per_build_and_backend);
    RUN_TEST(test_loss_aborts_the_tick_and_restore_completes_it);
    RUN_TEST(test_shutdown_discards_an_open_tick);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_tick_contract_asserts);
#endif
#if NT_GFX_CAPTURE_ENABLED
    RUN_TEST(test_resource_operations_keep_published_handles_after_destroy);
    RUN_TEST(test_loss_detected_during_creation_aborts_observation);
    RUN_TEST(test_restore_frame_completes_under_new_context_sequence);
    RUN_TEST(test_overflow_and_loss_finalize_aborted);
    RUN_TEST(test_sampler_cache_hit_defines_nothing);
    RUN_TEST(test_capture_defines_inherited_resources_and_unknown_scissor);
    RUN_TEST(test_draw_trace_preserves_arguments_and_live_prefix);
    RUN_TEST(test_capture_prefix_lifetime_and_saved_snapshot);
    RUN_TEST(test_capture_overflow_does_not_stop_counters);
    RUN_TEST(test_capture_toggle_waits_until_next_begin);
    RUN_TEST(test_exact_capacity_and_one_record_short);
#endif
    return UNITY_END();
}
