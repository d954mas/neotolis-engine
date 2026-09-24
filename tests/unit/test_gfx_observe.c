#include <string.h>

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "log/nt_log.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

static uint32_t s_error_logs;

static void count_error_logs(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)domain;
    (void)msg;
    (void)user;
    s_error_logs += level == NT_LOG_LEVEL_ERROR;
}

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
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
}

static void draw_teardown(void) { nt_gfx_end_pass(); }

static void test_passes_sum_and_begin_frame_resets(void) {
    TEST_ASSERT_EQUAL_UINT64(1, g_nt_gfx.counters.frame_sequence);
    draw_setup();
    nt_gfx_draw(0, 3);
    draw_teardown();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_draw_calls(&g_nt_gfx.counters));
    draw_setup();
    nt_gfx_draw_instanced(0, 6, 4);
    draw_teardown();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(1, g_nt_gfx.last_frame.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_draw_calls(&g_nt_gfx.last_frame));
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.last_frame.accepted[NT_GFX_OP_DRAW_INSTANCED]);
    TEST_ASSERT_EQUAL_UINT64(27, g_nt_gfx.last_frame.vertices);
    TEST_ASSERT_EQUAL_UINT64(4, g_nt_gfx.last_frame.instances);
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.last_frame.accepted[NT_GFX_OP_PIPELINE]);
    /* Closing a frame opens the next one with fresh counters. */
    TEST_ASSERT_EQUAL_UINT64(2, g_nt_gfx.counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_draw_calls(&g_nt_gfx.counters));

    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(2, g_nt_gfx.last_frame.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_draw_calls(&g_nt_gfx.last_frame));
}

static void test_instanced_products_are_widened_before_multiplication(void) {
    draw_setup();
    nt_gfx_draw_instanced(0, 65536, 65537);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(4295032832), g_nt_gfx.counters.vertices);
    draw_teardown();
}

/* begin_frame wipes a new loss; pass calls on the lost context are no-ops, not traps. */
static void test_loss_is_wiped_at_begin_frame_and_pass_calls_are_no_ops(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(nt_gfx_program_ready(program));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored); /* the whole iteration sees it */
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);
}

/* A loss inside an iteration changes no state: its work is issued and does nothing, and the next begin_frame wipes. */
static void test_loss_during_an_iteration_is_wiped_at_the_next_begin_frame(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(nt_gfx_program_ready(program));
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(nt_gfx_program_ready(program));
}

/* Creates on a loss the browser has not reported yet and on a known loss end CONTEXT_LOST without error logs. */
static void test_creates_on_a_loss_fail_quietly(void) {
    const nt_buffer_desc_t buffer_desc = {.type = NT_BUFFER_VERTEX, .size = 8};
    const nt_texture_desc_t texture_desc = {.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8};
    const nt_render_target_desc_t rt_desc = {.width = 4, .height = 4, .color_format = NT_TEXTURE_FORMAT_RGBA8};
    const nt_shader_desc_t shader_desc = {.type = NT_SHADER_VERTEX, .source = "void main(){}"};
    nt_gfx_fake_set_context_lost(true);
    s_error_logs = 0;
    for (uint32_t known = 0; known < 2; known++) {
        TEST_ASSERT_EQUAL(known != 0, g_nt_gfx.context_lost);
        TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_buffer(&buffer_desc).id);
        TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_texture(&texture_desc).id);
        TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_render_target(&rt_desc).id);
        TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_shader(&shader_desc).id);
        TEST_ASSERT_EQUAL_UINT32(0, s_error_logs);
        nt_gfx_begin_frame();
        s_error_logs = 0;
    }
}

/* A loss and restore between two iterations (a background tab) wipe and restore in one begin_frame. */
static void test_loss_and_restore_between_iterations_restore_in_one_begin_frame(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_fake_lose_and_restore_context();
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_FALSE(nt_gfx_program_ready(program));
}

/* Loading after init lands in the first frame, so its creations are counted like any frame's. */
static void test_first_frame_counts_initial_resource_creation(void) {
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8});
    (void)nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    (void)nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(1, g_nt_gfx.last_frame.frame_sequence);
    /* The texture also creates its default sampler: four accepted creations. */
    TEST_ASSERT_EQUAL_UINT32(4, g_nt_gfx.last_frame.accepted[NT_GFX_OP_CREATE]);
}

static void test_shutdown_discards_an_open_frame(void) {
    nt_gfx_begin_frame();
    draw_setup();
    nt_gfx_draw(0, 3);
    draw_teardown();
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_EQUAL_UINT64(0, g_nt_gfx.last_frame.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_draw_calls(&g_nt_gfx.counters));
    TEST_ASSERT_EQUAL_UINT64(1, g_nt_gfx.counters.frame_sequence);
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
static void test_begin_frame_with_an_open_pass_asserts(void) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_frame());
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
}
#endif

#if NT_GFX_CAPTURE_ENABLED
/* The begin_frame that consumes a request starts recording the frame it opens. */
static void record_next_frame(void) {
    nt_gfx_capture_request();
    nt_gfx_begin_frame();
}

static void test_resource_operations_keep_published_handles_after_destroy(void) {
    record_next_frame();
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 24});
    nt_gfx_destroy_buffer(buffer);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    bool created = false;
    bool destroyed = false;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->kind == NT_GFX_EVENT_RESULT && e->object_kind == NT_GFX_OBJECT_BUFFER && e->object == buffer.id && e->result == NT_GFX_RESULT_ACCEPTED) {
            created |= e->operation == NT_GFX_OP_CREATE;
            destroyed |= e->operation == NT_GFX_OP_DESTROY;
        }
    }
    TEST_ASSERT_TRUE(created);
    TEST_ASSERT_TRUE(destroyed);
}

static uint32_t result_of(nt_gfx_capture_view_t capture, nt_gfx_operation_t operation, nt_gfx_object_kind_t kind) {
    uint32_t result = UINT32_MAX;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->kind == NT_GFX_EVENT_RESULT && e->operation == operation && e->object_kind == kind) {
            result = (uint32_t)e->result;
        }
    }
    return result;
}

static void test_render_target_work_on_a_known_loss_ends_context_lost(void) {
    const nt_render_target_desc_t rt_desc = {.width = 4, .height = 4, .color_format = NT_TEXTURE_FORMAT_RGBA8};
    nt_render_target_t target = nt_gfx_make_render_target(&rt_desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    nt_gfx_fake_set_context_lost(true);
    record_next_frame();
    TEST_ASSERT_FALSE(nt_gfx_resize_render_target(target, 8, 8));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_render_target(&rt_desc).id);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(capture, NT_GFX_OP_RESIZE, NT_GFX_OBJECT_RENDER_TARGET));
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(capture, NT_GFX_OP_CREATE, NT_GFX_OBJECT_RENDER_TARGET));
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* FULL traps before the rejection returns, so the recorded operation has a BEGIN and no RESULT. */
static void test_rejected_destroys_and_resize_assert_inside_a_recorded_frame(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.width = 4, .height = 4, .color_format = NT_TEXTURE_FORMAT_RGBA8});
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8});
    record_next_frame();
    NT_TEST_EXPECT_ASSERT(nt_gfx_destroy_texture(nt_gfx_render_target_color(target)));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "owned by a render target"));
    NT_TEST_EXPECT_ASSERT(nt_gfx_resize_render_target((nt_render_target_t){target.id + 1}, 8, 8));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "invalid handle"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_destroy_texture(texture));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "inside a pass"));
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, result_of(nt_gfx_capture_read(), NT_GFX_OP_DESTROY, NT_GFX_OBJECT_TEXTURE));
}
#endif

/* The wipe runs before the frame opens and the restore inside it, so the snapshot
 * shows the lost tables and one CONTEXT operation follows. */
static void test_restore_is_one_context_operation_after_the_lost_snapshot(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_gfx_fake_lose_and_restore_context();
    record_next_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_EVENT_INITIAL, capture.events[0].kind);
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_INITIAL_FRONTEND, capture.events[0].detail);
    TEST_ASSERT_EQUAL_UINT32(1, capture.events[0].data.state.integers[5]); /* context_lost */
    uint32_t restores = 0;
    bool pipeline_defined = false;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        restores += e->kind == NT_GFX_EVENT_RESULT && e->operation == NT_GFX_OP_CONTEXT && e->result == NT_GFX_RESULT_ACCEPTED;
        pipeline_defined |= e->kind == NT_GFX_EVENT_DEFINITION && e->object_kind == NT_GFX_OBJECT_PIPELINE && e->object == pipeline.id;
    }
    TEST_ASSERT_EQUAL_UINT32(1, restores);
    TEST_ASSERT_FALSE(pipeline_defined);
    TEST_ASSERT_EQUAL_UINT32(1, capture.counters.accepted[NT_GFX_OP_CONTEXT]);
}

/* A failed recreate leaves no context: the engine stays lost for good with one error log. */
static void test_failed_restore_stays_lost_with_one_error_log(void) {
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_fake_fail_next_backend_restore_lost();
    s_error_logs = 0;
    record_next_frame();
    for (uint32_t i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
        nt_gfx_end_pass();
        nt_gfx_begin_frame();
    }
    TEST_ASSERT_EQUAL_UINT32(NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR ? 1 : 0, s_error_logs);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_backend_restore_count());
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(nt_gfx_capture_read(), NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE));
}

/* A restore that meets a new loss wipes what it refilled and stays lost; the next restore starts clean. */
static void test_restore_meeting_a_new_loss_stays_lost_and_the_next_restore_works(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.width = 4, .height = 4, .color_format = NT_TEXTURE_FORMAT_RGBA8});
    nt_sampler_t sampler = nt_gfx_get_texture_default_sampler(nt_gfx_render_target_color(target));
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_fake_lose_context_during_next_restore();
    s_error_logs = 0;
    record_next_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);
    TEST_ASSERT_FALSE(nt_gfx_render_target_ready(target));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_sampler_backend_id(sampler));
    TEST_ASSERT_EQUAL_UINT32(0, s_error_logs);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_depth = 1.0F});
    nt_gfx_end_pass();

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(capture, NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE));
    TEST_ASSERT_EQUAL_UINT32(0, capture.counters.accepted[NT_GFX_OP_CONTEXT]);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(target));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_test_sampler_backend_id(sampler));
}

/* A render target whose restore meets a new loss is that loss, not a live failure. */
static void test_render_target_restore_meeting_a_loss_ends_context_lost(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.width = 4, .height = 4, .color_format = NT_TEXTURE_FORMAT_RGBA8});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_fake_lose_context_on_texture_create();
    s_error_logs = 0;
    record_next_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);
    TEST_ASSERT_FALSE(nt_gfx_render_target_ready(target));
    TEST_ASSERT_EQUAL_UINT32(0, s_error_logs);

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(nt_gfx_capture_read(), NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE));
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(target));
}

static void test_resize_failing_on_a_latched_loss_ends_context_lost(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.width = 4, .height = 4, .color_format = NT_TEXTURE_FORMAT_RGBA8});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    record_next_frame();
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_fake_fail_next_render_target_resize();
    TEST_ASSERT_FALSE(nt_gfx_resize_render_target(target, 8, 8));
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(capture, NT_GFX_OP_RESIZE, NT_GFX_OBJECT_RENDER_TARGET));
}

/* The explicit sampler outlives the loss; its backend is recreated lazily at the bind. */
static void test_lazy_sampler_recreate_on_a_latched_loss_ends_context_lost(void) {
    nt_sampler_t sampler = nt_gfx_make_sampler(&(nt_sampler_desc_t){.min_filter = NT_FILTER_LINEAR, .mag_filter = NT_FILTER_LINEAR});
    nt_gfx_fake_lose_and_restore_context();
    nt_gfx_begin_frame();
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_tex"}, 1);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    record_next_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_fake_fail_next_sampler_create();
    const nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("u_tex"), .texture = texture, .sampler = sampler};
    nt_gfx_apply_texture_bindings(&binding, 1);
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(capture, NT_GFX_OP_TEXTURE_SET, NT_GFX_OBJECT_PIPELINE));
}

/* A stage whose GPU object died with the context is unready, not a backend failure. */
static void test_link_with_a_stage_left_unready_by_a_loss_ends_unready(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    nt_gfx_fake_lose_and_restore_context();
    record_next_frame();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_program(vs, fs).id);
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_UNREADY, result_of(nt_gfx_capture_read(), NT_GFX_OP_CREATE, NT_GFX_OBJECT_PROGRAM));
}

/* Restored render targets are defined inside the CONTEXT operation, a failed one with complete=0. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one walk locates the operation and both definitions
static void test_restore_defines_render_targets_inside_the_context_operation(void) {
    const nt_render_target_desc_t rt_desc = {.width = 4, .height = 4, .color_format = NT_TEXTURE_FORMAT_RGBA8};
    nt_render_target_t failed = nt_gfx_make_render_target(&rt_desc);
    nt_render_target_t restored = nt_gfx_make_render_target(&rt_desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, failed.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, restored.id);
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_fake_fail_next_render_target_create(); /* recreation walks slots in order: the first target fails */
    record_next_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t begin = UINT32_MAX;
    uint32_t result = UINT32_MAX;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->operation == NT_GFX_OP_CONTEXT && e->kind == NT_GFX_EVENT_BEGIN) {
            begin = i;
        } else if (e->operation == NT_GFX_OP_CONTEXT && e->kind == NT_GFX_EVENT_RESULT) {
            result = i;
        }
    }
    TEST_ASSERT_LESS_THAN_UINT32(result, begin);
    int32_t failed_complete = -1;
    int32_t restored_complete = -1;
    for (uint32_t i = begin + 1; i < result; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->kind == NT_GFX_EVENT_DEFINITION && e->object_kind == NT_GFX_OBJECT_RENDER_TARGET) {
            if (e->object == failed.id) {
                failed_complete = (int32_t)e->data.resource.flags;
            } else if (e->object == restored.id) {
                restored_complete = (int32_t)e->data.resource.flags;
            }
        }
    }
    TEST_ASSERT_EQUAL_INT32(0, failed_complete);
    TEST_ASSERT_EQUAL_INT32(1, restored_complete);
}

static void test_sampler_cache_hit_defines_nothing(void) {
    const nt_sampler_desc_t sampler_desc = {.min_filter = NT_FILTER_LINEAR, .mag_filter = NT_FILTER_LINEAR};
    nt_sampler_t sampler = nt_gfx_make_sampler(&sampler_desc);
    record_next_frame();
    TEST_ASSERT_EQUAL_UINT32(sampler.id, nt_gfx_make_sampler(&sampler_desc).id);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    /* The request starts after the inherited definitions. */
    uint32_t start = 0;
    while (start < capture.count && capture.events[start].kind != NT_GFX_EVENT_BEGIN) {
        start++;
    }
    bool cache = false;
    for (uint32_t i = start; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        TEST_ASSERT_FALSE(e->kind == NT_GFX_EVENT_DEFINITION && e->object_kind == NT_GFX_OBJECT_SAMPLER);
        cache |= e->kind == NT_GFX_EVENT_RESULT && e->object == sampler.id && e->result == NT_GFX_RESULT_CACHE;
    }
    TEST_ASSERT_TRUE(cache);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one walk checks nesting, pairing and creator handles
static void test_every_operation_records_one_begin_and_one_result(void) {
    record_next_frame();
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.width = 4, .height = 4, .color_format = NT_TEXTURE_FORMAT_RGBA8});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_buffer(NULL).id);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    nt_gfx_operation_t stack[8] = {0};
    uint32_t depth = 0;
    bool target_result = false;
    bool invalid_buffer = false;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->kind == NT_GFX_EVENT_BEGIN) {
            TEST_ASSERT_LESS_THAN_UINT32(8, depth);
            stack[depth++] = e->operation;
        } else if (e->kind == NT_GFX_EVENT_RESULT) {
            TEST_ASSERT_GREATER_THAN_UINT32(0, depth);
            TEST_ASSERT_EQUAL(stack[--depth], e->operation);
            target_result |= e->operation == NT_GFX_OP_CREATE && e->object_kind == NT_GFX_OBJECT_RENDER_TARGET && e->object == target.id && e->result == NT_GFX_RESULT_ACCEPTED;
            invalid_buffer |= e->operation == NT_GFX_OP_CREATE && e->object_kind == NT_GFX_OBJECT_BUFFER && e->object == 0 && e->result == NT_GFX_RESULT_INVALID_ARGUMENT;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0, depth);
    TEST_ASSERT_TRUE(target_result);
    TEST_ASSERT_TRUE(invalid_buffer);
}

/* accepted[] and the recorded ACCEPTED results come from the same END, per operation. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one frame mixes accepted, cached and rejected operations
static void test_accepted_counters_match_recorded_results(void) {
    record_next_frame();
    draw_setup();
    nt_gfx_draw(0, 3);
    nt_gfx_set_scissor_enabled(false); /* unchanged: a cache result, not counted */
    nt_gfx_bind_pipeline((nt_pipeline_t){0});
    draw_teardown();
    (void)nt_gfx_make_buffer(NULL);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t recorded[NT_GFX_OP_COUNT] = {0};
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->kind == NT_GFX_EVENT_RESULT && e->result == NT_GFX_RESULT_ACCEPTED) {
            recorded[e->operation]++;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, recorded[NT_GFX_OP_DRAW]);
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        TEST_ASSERT_EQUAL_UINT32(recorded[op], capture.counters.accepted[op]);
    }
}

static void test_capture_defines_inherited_resources_and_unknown_scissor(void) {
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 24});
    record_next_frame();
    nt_gfx_destroy_buffer(buffer);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t initial = nt_gfx_capture_read();
    bool buffer_found = false;
    bool scissor_unknown = false;
    for (uint32_t i = 0; i < initial.count; i++) {
        const nt_gfx_event_t *e = &initial.events[i];
        if (e->kind == NT_GFX_EVENT_DEFINITION && e->object_kind == NT_GFX_OBJECT_BUFFER && e->object == buffer.id) {
            TEST_ASSERT_EQUAL_UINT64(24, e->data.resource.size);
            buffer_found = true;
        }
        if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_SCISSOR && e->result == NT_GFX_RESULT_UNKNOWN) {
            scissor_unknown = true;
        }
    }
    TEST_ASSERT_TRUE(buffer_found);
    TEST_ASSERT_TRUE(scissor_unknown);
}

static void test_draw_trace_preserves_arguments_and_live_prefix(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    record_next_frame();
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
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_MEMORY(copy, live.events, live.count * sizeof(copy[0]));
}

static void test_capture_prefix_lifetime_and_saved_snapshot(void) {
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_capture_read().count);
    record_next_frame();
    nt_gfx_capture_view_t before = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT64(0, before.counters.frame_sequence);
    TEST_ASSERT_TRUE(before.count > 0);
    nt_gfx_event_t saved = before.events[0];
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
    nt_gfx_counters_t snapshot = g_nt_gfx.last_frame;
    nt_gfx_capture_view_t after = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_MEMORY(&saved, &before.events[0], sizeof(saved));
    TEST_ASSERT_TRUE(after.count > before.count);
    TEST_ASSERT_EQUAL_MEMORY(&snapshot, &after.counters, sizeof(snapshot));

    /* Unrecorded frames keep the finalized capture. */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t retained = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(after.count, retained.count);
    TEST_ASSERT_EQUAL_MEMORY(&after.counters, &retained.counters, sizeof(snapshot));
    TEST_ASSERT_EQUAL_MEMORY(&saved, &retained.events[0], sizeof(saved));

    /* A request leaves the finished capture intact until its begin_frame starts recording. */
    nt_gfx_capture_request();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_capture_view_t pending = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(after.count, pending.count);
    TEST_ASSERT_EQUAL_MEMORY(&after.counters, &pending.counters, sizeof(snapshot));
    nt_gfx_begin_frame();
}

static void test_capture_overflow_does_not_stop_counters(void) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 1;
    nt_gfx_init(&desc);
    record_next_frame();
    nt_gfx_begin_frame();
    const nt_gfx_counters_t *snapshot = &g_nt_gfx.last_frame;
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(1, capture.count);
    TEST_ASSERT_TRUE(capture.overflow);
    TEST_ASSERT_EQUAL_UINT64(snapshot->frame_sequence, capture.counters.frame_sequence);
}

static void test_capture_request_records_only_the_next_frame(void) {
    nt_gfx_capture_request();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_capture_read().count);
    nt_gfx_begin_frame();
    TEST_ASSERT_GREATER_THAN_UINT32(0, nt_gfx_capture_read().count);
    nt_gfx_begin_frame();
    uint64_t sequence = nt_gfx_capture_read().counters.frame_sequence;
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(sequence, nt_gfx_capture_read().counters.frame_sequence);
}

/* A request during a recorded frame replaces that capture at the begin_frame that finishes it. */
static void test_request_during_recorded_frame_replaces_the_capture(void) {
    record_next_frame();
    const uint32_t snapshot_records = nt_gfx_capture_read().count;
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    TEST_ASSERT_GREATER_THAN_UINT32(snapshot_records, nt_gfx_capture_read().count);
    nt_gfx_capture_request();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t replaced = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT64(0, replaced.counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(snapshot_records, replaced.count);
    for (uint32_t i = 0; i < replaced.count; i++) {
        /* only the inherited-state prefix and begin_frame's own timer check */
        TEST_ASSERT_TRUE(replaced.events[i].kind != NT_GFX_EVENT_BEGIN || replaced.events[i].operation == NT_GFX_OP_TIMER_DISJOINT);
    }
}

static void test_capture_read_after_shutdown_is_empty(void) {
    record_next_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_GREATER_THAN_UINT32(0, nt_gfx_capture_read().count);
    nt_gfx_shutdown();
    nt_gfx_capture_view_t view = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(0, view.count);
    TEST_ASSERT_NULL(view.events);
    TEST_ASSERT_EQUAL_UINT64(0, view.counters.frame_sequence);
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
}

static void test_exact_capacity_and_one_record_short(void) {
    record_next_frame();
    nt_gfx_begin_frame();
    uint32_t needed = nt_gfx_capture_read().count;
    TEST_ASSERT_GREATER_THAN_UINT32(1, needed);
    for (uint32_t missing = 0; missing < 2; missing++) {
        nt_gfx_shutdown();
        nt_gfx_desc_t desc = nt_gfx_desc_defaults();
        desc.capture_capacity = needed - missing;
        nt_gfx_init(&desc);
        record_next_frame();
        nt_gfx_begin_frame();
        nt_gfx_capture_view_t view = nt_gfx_capture_read();
        TEST_ASSERT_EQUAL_UINT32(needed - missing, view.count);
        TEST_ASSERT_EQUAL(missing != 0, view.overflow);
    }
}
#endif

int main(void) {
    nt_log_add_sink(count_error_logs, NULL);
    UNITY_BEGIN();
    RUN_TEST(test_passes_sum_and_begin_frame_resets);
    RUN_TEST(test_instanced_products_are_widened_before_multiplication);
    RUN_TEST(test_loss_is_wiped_at_begin_frame_and_pass_calls_are_no_ops);
    RUN_TEST(test_loss_during_an_iteration_is_wiped_at_the_next_begin_frame);
    RUN_TEST(test_creates_on_a_loss_fail_quietly);
    RUN_TEST(test_loss_and_restore_between_iterations_restore_in_one_begin_frame);
    RUN_TEST(test_first_frame_counts_initial_resource_creation);
    RUN_TEST(test_shutdown_discards_an_open_frame);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_begin_frame_with_an_open_pass_asserts);
#endif
#if NT_GFX_CAPTURE_ENABLED
    RUN_TEST(test_resource_operations_keep_published_handles_after_destroy);
    RUN_TEST(test_render_target_work_on_a_known_loss_ends_context_lost);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_rejected_destroys_and_resize_assert_inside_a_recorded_frame);
#endif
    RUN_TEST(test_restore_is_one_context_operation_after_the_lost_snapshot);
    RUN_TEST(test_failed_restore_stays_lost_with_one_error_log);
    RUN_TEST(test_restore_meeting_a_new_loss_stays_lost_and_the_next_restore_works);
    RUN_TEST(test_render_target_restore_meeting_a_loss_ends_context_lost);
    RUN_TEST(test_resize_failing_on_a_latched_loss_ends_context_lost);
    RUN_TEST(test_lazy_sampler_recreate_on_a_latched_loss_ends_context_lost);
    RUN_TEST(test_link_with_a_stage_left_unready_by_a_loss_ends_unready);
    RUN_TEST(test_restore_defines_render_targets_inside_the_context_operation);
    RUN_TEST(test_sampler_cache_hit_defines_nothing);
    RUN_TEST(test_every_operation_records_one_begin_and_one_result);
    RUN_TEST(test_accepted_counters_match_recorded_results);
    RUN_TEST(test_capture_defines_inherited_resources_and_unknown_scissor);
    RUN_TEST(test_draw_trace_preserves_arguments_and_live_prefix);
    RUN_TEST(test_capture_prefix_lifetime_and_saved_snapshot);
    RUN_TEST(test_capture_overflow_does_not_stop_counters);
    RUN_TEST(test_capture_request_records_only_the_next_frame);
    RUN_TEST(test_request_during_recorded_frame_replaces_the_capture);
    RUN_TEST(test_capture_read_after_shutdown_is_empty);
    RUN_TEST(test_exact_capacity_and_one_record_short);
#endif
    return UNITY_END();
}
