#include <string.h>

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_frame.h"
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
    nt_gfx_begin_frame();
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
    TEST_ASSERT_EQUAL_UINT64(2, g_nt_gfx.counters.frame_sequence);
    draw_setup();
    nt_gfx_draw(0, 3);
    draw_teardown();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_draw_calls(&g_nt_gfx.counters));
    draw_setup();
    nt_gfx_draw_instanced(0, 6, 4);
    draw_teardown();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(2, g_nt_gfx.last_frame.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_draw_calls(&g_nt_gfx.last_frame));
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.last_frame.accepted[NT_GFX_OP_DRAW_INSTANCED]);
    TEST_ASSERT_EQUAL_UINT64(27, g_nt_gfx.last_frame.vertices);
    TEST_ASSERT_EQUAL_UINT64(4, g_nt_gfx.last_frame.instances);
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.last_frame.accepted[NT_GFX_OP_PIPELINE]);
    /* Closing a frame opens the next one with fresh counters. */
    TEST_ASSERT_EQUAL_UINT64(3, g_nt_gfx.counters.frame_sequence);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_draw_calls(&g_nt_gfx.counters));

    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(3, g_nt_gfx.last_frame.frame_sequence);
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(nt_gfx_program_ready(program));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored); /* the whole iteration sees it */
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(nt_gfx_program_ready(program));
}

/* Creates on a loss the browser has not reported yet and on a known loss end CONTEXT_LOST without error logs. */
static void test_creates_on_a_loss_fail_quietly(void) {
    const nt_buffer_desc_t buffer_desc = {.type = NT_BUFFER_VERTEX, .size = 8};
    const nt_texture_desc_t texture_desc = {.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8};
    const nt_render_target_desc_t rt_desc = {.color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8})};
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
        nt_gfx_end_frame();
        nt_gfx_begin_frame();
        s_error_logs = 0;
    }
}

/* A loss and restore between two iterations (a background tab) wipe and restore in one begin_frame. */
static void test_loss_and_restore_between_iterations_restore_in_one_begin_frame(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_fake_lose_and_restore_context();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_FALSE(nt_gfx_program_ready(program));
}

/* Loading after init lands in the first frame, so its creations are counted like any frame's. */
static void test_first_frame_counts_initial_resource_creation(void) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8});
    (void)nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    (void)nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(1, g_nt_gfx.last_frame.frame_sequence);
    /* The texture also creates its default sampler: four accepted creations. */
    TEST_ASSERT_EQUAL_UINT32(4, g_nt_gfx.last_frame.accepted[NT_GFX_OP_CREATE]);
}

/* The pre-swap capture seam reads after end_frame: work there is legal and counts in the open frame. */
static void test_work_after_end_frame_counts_in_the_open_frame(void) {
    nt_gfx_end_frame();
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8});
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.last_frame.accepted[NT_GFX_OP_CREATE]);
}

static void test_shutdown_discards_an_open_frame(void) {
    nt_gfx_end_frame();
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

static void test_clear_preserves_bound_draw_state(void) {
    draw_setup();
    nt_gfx_apply_texture_bindings(NULL, 0);
    uint32_t pipeline = nt_gfx_test_bound_pipeline();
    uint32_t input = nt_gfx_test_bound_vertex_input();
    uint32_t textures = nt_gfx_test_texture_set_state();
    nt_gfx_clear(&(nt_clear_desc_t){.color = true, .depth = true, .clear_depth = 0.5F});
    TEST_ASSERT_EQUAL_UINT32(pipeline, nt_gfx_test_bound_pipeline());
    TEST_ASSERT_EQUAL_UINT32(input, nt_gfx_test_bound_vertex_input());
    TEST_ASSERT_EQUAL_UINT32(textures, nt_gfx_test_texture_set_state());
    nt_gfx_draw(0, 3);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_draw_calls(&g_nt_gfx.counters));
    draw_teardown();
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
static void test_clear_requires_an_open_pass_and_descriptor(void) {
    NT_TEST_EXPECT_ASSERT(nt_gfx_clear(&(nt_clear_desc_t){.color = true}));
    NT_TEST_EXPECT_ASSERT(nt_gfx_clear(&(nt_clear_desc_t){0}));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_clear(NULL));
    nt_gfx_end_pass();
}

static void test_begin_frame_needs_the_open_frame_ended(void) {
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_frame());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "begin_frame: the open frame has no nt_gfx_end_frame"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_frame());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "begin_frame: the open frame has no nt_gfx_end_frame"));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
}

static void test_end_frame_needs_an_open_frame_without_a_pass(void) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_end_frame());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "end_frame: needs an open frame with no open pass"));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    NT_TEST_EXPECT_ASSERT(nt_gfx_end_frame());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "end_frame: needs an open frame with no open pass"));
}

static void test_begin_pass_needs_an_open_frame_without_a_pass_also_on_a_loss(void) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F}));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "begin_pass: needs an open frame with no open pass"));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F}));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "begin_pass: needs an open frame with no open pass"));
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    nt_gfx_end_frame();
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F}));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "begin_pass: needs an open frame with no open pass"));
}

static void test_scissor_and_viewport_require_an_open_pass(void) {
    NT_TEST_EXPECT_ASSERT(nt_gfx_set_scissor(0, 0, 1, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "set_scissor: must be called inside a pass"));
    NT_TEST_EXPECT_ASSERT(nt_gfx_set_scissor_enabled(true));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "set_scissor_enabled: must be called inside a pass"));
    NT_TEST_EXPECT_ASSERT(nt_gfx_set_viewport(0, 0, 1, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "set_viewport: must be called inside a pass"));
}

static void test_bindings_require_an_open_pass(void) {
    nt_buffer_t ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_DYNAMIC, .size = 256});
    NT_TEST_EXPECT_ASSERT(nt_gfx_set_vertex_attrib_default(0, 0.0F, 0.0F, 0.0F, 1.0F));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "set_vertex_attrib_default: must be called inside a pass"));
    NT_TEST_EXPECT_ASSERT(nt_gfx_bind_uniform_buffer(ubo, 0));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "bind_uniform_buffer: must be called inside a pass"));
    NT_TEST_EXPECT_ASSERT(nt_gfx_bind_uniform_buffer_range(ubo, 0, 0, 256));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "bind_uniform_buffer: must be called inside a pass"));
}

static void test_draw_state_in_a_pass_on_a_lost_context_does_not_assert(void) {
    nt_buffer_t ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_DYNAMIC, .size = 256});
    nt_gfx_end_frame();
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    /* The game's pass does not open on a lost context; its draw state calls return quietly. */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_set_scissor(0, 0, 1, 1);
    nt_gfx_set_scissor_enabled(true);
    nt_gfx_set_viewport(0, 0, 1, 1);
    nt_gfx_set_vertex_attrib_default(0, 0.0F, 0.0F, 0.0F, 1.0F);
    nt_gfx_bind_uniform_buffer(ubo, 0);
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx_stream.used);
    nt_gfx_end_pass();
}

static void test_segments_require_an_open_frame(void) {
    nt_gfx_end_frame();
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_segment("frame"));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "begin_segment: must be called inside a frame"));
    NT_TEST_EXPECT_ASSERT(nt_gfx_end_segment());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "end_segment: must be called inside a frame"));
}
#endif

#if NT_GFX_CAPTURE_ENABLED
/* The begin_frame that consumes a request starts recording the frame it opens. */
static void record_next_frame(void) {
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
}

static void test_resource_operations_keep_published_handles_after_destroy(void) {
    record_next_frame();
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 24});
    nt_gfx_destroy_buffer(buffer);
    nt_gfx_end_frame();
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

static void test_clear_copies_requests_and_skips_known_loss(void) {
    const nt_clear_desc_t requests[2] = {{.color = true, .clear_color = {0.25F, 0.5F, 0.75F, 1}, .clear_depth = 0.25F}, {.depth = true, .clear_color = {0.75F, 0.25F, 0.5F, 1}, .clear_depth = 0.75F}};
    record_next_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    for (uint32_t i = 0; i < 2; i++) {
        nt_clear_desc_t desc = requests[i];
        nt_gfx_clear(&desc);
        memset(&desc, 0, sizeof(desc));
    }
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t found = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_BEGIN && event->operation == NT_GFX_OP_CLEAR) {
            if (found >= 2) {
                TEST_FAIL_MESSAGE("Unexpected extra clear request");
                return;
            }
            TEST_ASSERT_EQUAL_MEMORY(requests[found].clear_color, event->data.clear.clear_color, sizeof(requests[found].clear_color));
            TEST_ASSERT_EQUAL_MEMORY(&requests[found].clear_depth, &event->data.clear.clear_depth, sizeof(float));
            TEST_ASSERT_EQUAL(requests[found].color, event->data.clear.color);
            TEST_ASSERT_EQUAL(requests[found].depth, event->data.clear.depth);
            found++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(2, found);
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.last_frame.accepted[NT_GFX_OP_CLEAR]);
    nt_gfx_fake_set_context_lost(true);
    record_next_frame();
    nt_gfx_clear(NULL);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(nt_gfx_capture_read(), NT_GFX_OP_CLEAR, NT_GFX_OBJECT_RENDER_TARGET));
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.last_frame.accepted[NT_GFX_OP_CLEAR]);
}

static void test_render_target_work_on_a_known_loss_ends_context_lost(void) {
    const nt_render_target_desc_t rt_desc = {.color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8})};
    nt_render_target_t target = nt_gfx_make_render_target(&rt_desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    nt_gfx_fake_set_context_lost(true);
    record_next_frame();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_render_target(&rt_desc).id);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(capture, NT_GFX_OP_CREATE, NT_GFX_OBJECT_RENDER_TARGET));
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* FULL traps before the rejection returns, so the recorded operation has a BEGIN and no RESULT. */
static void test_rejected_destroys_assert_inside_a_recorded_frame(void) {
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8});
    record_next_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_destroy_texture(texture));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "inside a pass"));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_fake_fail_next_backend_restore_lost();
    s_error_logs = 0;
    record_next_frame();
    for (uint32_t i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
        nt_gfx_end_pass();
        nt_gfx_end_frame();
        nt_gfx_begin_frame();
    }
    TEST_ASSERT_EQUAL_UINT32(NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR ? 1 : 0, s_error_logs);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_backend_restore_count());
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(nt_gfx_capture_read(), NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE));
}

static void test_restore_meeting_a_new_loss_stays_lost_and_the_next_restore_works(void) {
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_fake_lose_context_during_next_restore();
    s_error_logs = 0;
    record_next_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);
    TEST_ASSERT_EQUAL_UINT32(0, s_error_logs);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_CONTEXT_LOST, result_of(capture, NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE));
    TEST_ASSERT_EQUAL_UINT32(0, capture.counters.accepted[NT_GFX_OP_CONTEXT]);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
}

/* The explicit sampler outlives the loss; its backend is recreated lazily at the bind. */
static void test_lazy_sampler_recreate_on_a_latched_loss_ends_context_lost(void) {
    nt_sampler_t sampler = nt_gfx_make_sampler(&(nt_sampler_desc_t){.min_filter = NT_FILTER_LINEAR, .mag_filter = NT_FILTER_LINEAR});
    nt_gfx_fake_lose_and_restore_context();
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_RESULT_UNREADY, result_of(nt_gfx_capture_read(), NT_GFX_OP_CREATE, NT_GFX_OBJECT_PROGRAM));
}

/* Loss frees render targets like vertex inputs, so the restore defines none. */
static void test_restore_defines_no_render_targets(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8})});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(target));

    nt_gfx_fake_set_context_lost(false);
    record_next_frame();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    TEST_ASSERT_EQUAL_UINT32(1, capture.counters.accepted[NT_GFX_OP_CONTEXT]);
    for (uint32_t i = 0; i < capture.count; i++) {
        TEST_ASSERT_FALSE(capture.events[i].kind == NT_GFX_EVENT_DEFINITION && capture.events[i].object_kind == NT_GFX_OBJECT_RENDER_TARGET);
    }
}

static void test_sampler_cache_hit_defines_nothing(void) {
    const nt_sampler_desc_t sampler_desc = {.min_filter = NT_FILTER_LINEAR, .mag_filter = NT_FILTER_LINEAR};
    nt_sampler_t sampler = nt_gfx_make_sampler(&sampler_desc);
    record_next_frame();
    TEST_ASSERT_EQUAL_UINT32(sampler.id, nt_gfx_make_sampler(&sampler_desc).id);
    nt_gfx_end_frame();
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
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8})});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_buffer(NULL).id);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
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

/* Scissor rectangle and uniform-buffer slots carry over frames: the snapshot
 * shows the state a CACHE bind inside the capture matched. */
static uint32_t initial_records(nt_gfx_capture_view_t capture, nt_gfx_operation_t operation, nt_gfx_result_t result) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        count += (capture.events[i].kind == NT_GFX_EVENT_INITIAL && capture.events[i].operation == operation && capture.events[i].result == result) ? 1U : 0U;
    }
    return count;
}

/* A destroyed buffer leaves its slots, and a context loss forgets slots and the scissor rectangle. */
static void test_capture_initial_state_forgets_destroyed_and_lost_bindings(void) {
    nt_buffer_t ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_DYNAMIC, .size = 256});
    nt_buffer_t kept = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_DYNAMIC, .size = 256});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_set_scissor(1, 2, 3, 4);
    nt_gfx_bind_uniform_buffer(ubo, 2);
    nt_gfx_bind_uniform_buffer(kept, 3);
    nt_gfx_end_pass();
    nt_gfx_destroy_buffer(ubo);
    record_next_frame();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(1, initial_records(capture, NT_GFX_OP_UBO, NT_GFX_RESULT_NONE)); /* only `kept` */
    TEST_ASSERT_EQUAL_UINT32(0, initial_records(capture, NT_GFX_OP_SCISSOR, NT_GFX_RESULT_UNKNOWN));

    nt_gfx_fake_lose_and_restore_context();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    record_next_frame();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    capture = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(0, initial_records(capture, NT_GFX_OP_UBO, NT_GFX_RESULT_NONE));
    TEST_ASSERT_EQUAL_UINT32(1, initial_records(capture, NT_GFX_OP_SCISSOR, NT_GFX_RESULT_UNKNOWN));
}

/* GL keeps a uniform-buffer binding across an orphan, so the snapshot keeps it too. */
static void test_capture_initial_state_keeps_bindings_across_an_orphan(void) {
    nt_buffer_t ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_DYNAMIC, .size = 256});
    const uint8_t data[256] = {0};
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_uniform_buffer(ubo, 1);
    nt_gfx_end_pass();
    nt_gfx_orphan_buffer(ubo, data, sizeof(data));
    record_next_frame();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(1, initial_records(nt_gfx_capture_read(), NT_GFX_OP_UBO, NT_GFX_RESULT_NONE));
}

/* An equal bind and a merged indexed draw each end CACHE in the capture. */
static void test_capture_shows_cache_for_equal_binds_and_merged_draws(void) {
    static const float verts[9] = {0};
    static const uint16_t indices[6] = {0, 1, 2, 0, 1, 2};
    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = verts, .size = sizeof(verts)});
    nt_buffer_t ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = indices, .size = sizeof(indices), .index_type = NT_INDEX_UINT16});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(
        &(nt_vertex_input_desc_t){.layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}}, .vertex_buffer = vbo, .index_buffer = ibo});
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_fake_make_program(NULL, 0)});
    record_next_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    for (uint32_t repeat = 0; repeat < 2; repeat++) {
        nt_gfx_bind_pipeline(pipeline);
        nt_gfx_bind_vertex_input(vi);
        nt_gfx_set_viewport(0, 0, 4, 4);
        nt_gfx_draw_indexed(repeat * 3U, 3, 3);
    }
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    const nt_gfx_operation_t ops[] = {NT_GFX_OP_PIPELINE, NT_GFX_OP_VERTEX_INPUT, NT_GFX_OP_VIEWPORT, NT_GFX_OP_DRAW_INDEXED};
    for (uint32_t o = 0; o < sizeof(ops) / sizeof(ops[0]); o++) {
        uint32_t cache = 0;
        for (uint32_t i = 0; i < capture.count; i++) {
            const nt_gfx_event_t *e = &capture.events[i];
            cache += (e->kind == NT_GFX_EVENT_RESULT && e->operation == ops[o] && e->result == NT_GFX_RESULT_CACHE) ? 1U : 0U;
        }
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, cache, "one CACHE result per operation");
    }
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_draw_calls(&capture.counters));
}

static void test_capture_initial_state_holds_carried_over_bindings(void) {
    nt_buffer_t ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_DYNAMIC, .size = 512});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_set_scissor(1, 2, 3, 4);
    nt_gfx_bind_uniform_buffer_range(ubo, 5, 256, 128);
    nt_gfx_end_pass();
    record_next_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_set_scissor(1, 2, 3, 4);
    nt_gfx_bind_uniform_buffer_range(ubo, 5, 256, 128);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    bool scissor_found = false;
    bool ubo_found = false;
    uint32_t cache_results = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_SCISSOR) {
            TEST_ASSERT_EQUAL_UINT32(1, e->data.state.integers[0]);
            TEST_ASSERT_EQUAL_UINT32(2, e->data.state.integers[1]);
            TEST_ASSERT_EQUAL_UINT32(3, e->data.state.integers[2]);
            TEST_ASSERT_EQUAL_UINT32(4, e->data.state.integers[3]);
            scissor_found = true;
        }
        if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_UBO) {
            TEST_ASSERT_EQUAL_UINT32(ubo.id, e->object);
            TEST_ASSERT_EQUAL_UINT32(5, e->data.binding.slot);
            TEST_ASSERT_EQUAL_UINT32(256, e->data.binding.offset);
            TEST_ASSERT_EQUAL_UINT32(128, e->data.binding.size);
            ubo_found = true;
        }
        if (e->kind == NT_GFX_EVENT_RESULT && (e->operation == NT_GFX_OP_SCISSOR || e->operation == NT_GFX_OP_UBO) && e->result == NT_GFX_RESULT_CACHE) {
            cache_results++;
        }
    }
    TEST_ASSERT_TRUE(scissor_found);
    TEST_ASSERT_TRUE(ubo_found);
    TEST_ASSERT_EQUAL_UINT32(2, cache_results);
}

/* Size and formats come from the borrowed textures. */
static void test_depth_only_render_target_definition_has_no_color_fields(void) {
    nt_texture_t depth = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 64, .height = 32, .format = NT_TEXTURE_FORMAT_DEPTH16});
    record_next_frame();
    nt_render_target_t rt = nt_gfx_make_render_target(&(nt_render_target_desc_t){.depth = depth});
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    const uint32_t depth_id = depth.id;
    TEST_ASSERT_NOT_EQUAL_UINT32(0, depth_id);
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t definitions = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->kind != NT_GFX_EVENT_DEFINITION || e->object_kind != NT_GFX_OBJECT_RENDER_TARGET || e->object != rt.id) {
            continue;
        }
        TEST_ASSERT_EQUAL_UINT32(0, e->data.resource.related[0]);
        TEST_ASSERT_EQUAL_UINT32(depth_id, e->data.resource.related[1]);
        TEST_ASSERT_EQUAL_UINT32(0, e->data.resource.format);
        TEST_ASSERT_EQUAL_UINT32(NT_TEXTURE_FORMAT_DEPTH16, e->data.resource.usage);
        TEST_ASSERT_EQUAL_UINT32(64, e->data.resource.width);
        TEST_ASSERT_EQUAL_UINT32(32, e->data.resource.height);
        definitions++;
    }
    TEST_ASSERT_EQUAL_UINT32(1, definitions);
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
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_counters_t snapshot = g_nt_gfx.last_frame;
    nt_gfx_capture_view_t after = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_MEMORY(&saved, &before.events[0], sizeof(saved));
    TEST_ASSERT_TRUE(after.count > before.count);
    TEST_ASSERT_EQUAL_MEMORY(&snapshot, &after.counters, sizeof(snapshot));

    /* Unrecorded frames keep the finalized capture. */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
}

static void test_capture_overflow_does_not_stop_counters(void) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 1;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    record_next_frame();
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_GREATER_THAN_UINT32(0, nt_gfx_capture_read().count);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    uint64_t sequence = nt_gfx_capture_read().counters.frame_sequence;
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_GREATER_THAN_UINT32(0, nt_gfx_capture_read().count);
    nt_gfx_shutdown();
    nt_gfx_capture_view_t view = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(0, view.count);
    TEST_ASSERT_NULL(view.events);
    TEST_ASSERT_EQUAL_UINT64(0, view.counters.frame_sequence);
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
}

static void test_exact_capacity_and_one_record_short(void) {
    record_next_frame();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    uint32_t needed = nt_gfx_capture_read().count;
    TEST_ASSERT_GREATER_THAN_UINT32(1, needed);
    for (uint32_t missing = 0; missing < 2; missing++) {
        nt_gfx_shutdown();
        nt_gfx_desc_t desc = nt_gfx_desc_defaults();
        desc.capture_capacity = needed - missing;
        nt_gfx_init(&desc);
        nt_gfx_begin_frame();
        record_next_frame();
        nt_gfx_end_frame();
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
    RUN_TEST(test_clear_preserves_bound_draw_state);
    RUN_TEST(test_passes_sum_and_begin_frame_resets);
    RUN_TEST(test_instanced_products_are_widened_before_multiplication);
    RUN_TEST(test_loss_is_wiped_at_begin_frame_and_pass_calls_are_no_ops);
    RUN_TEST(test_loss_during_an_iteration_is_wiped_at_the_next_begin_frame);
    RUN_TEST(test_creates_on_a_loss_fail_quietly);
    RUN_TEST(test_loss_and_restore_between_iterations_restore_in_one_begin_frame);
    RUN_TEST(test_first_frame_counts_initial_resource_creation);
    RUN_TEST(test_work_after_end_frame_counts_in_the_open_frame);
    RUN_TEST(test_shutdown_discards_an_open_frame);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_clear_requires_an_open_pass_and_descriptor);
    RUN_TEST(test_begin_frame_needs_the_open_frame_ended);
    RUN_TEST(test_end_frame_needs_an_open_frame_without_a_pass);
    RUN_TEST(test_begin_pass_needs_an_open_frame_without_a_pass_also_on_a_loss);
    RUN_TEST(test_scissor_and_viewport_require_an_open_pass);
    RUN_TEST(test_bindings_require_an_open_pass);
    RUN_TEST(test_draw_state_in_a_pass_on_a_lost_context_does_not_assert);
    RUN_TEST(test_segments_require_an_open_frame);
#endif
#if NT_GFX_CAPTURE_ENABLED
    RUN_TEST(test_clear_copies_requests_and_skips_known_loss);
    RUN_TEST(test_resource_operations_keep_published_handles_after_destroy);
    RUN_TEST(test_render_target_work_on_a_known_loss_ends_context_lost);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_rejected_destroys_assert_inside_a_recorded_frame);
#endif
    RUN_TEST(test_restore_is_one_context_operation_after_the_lost_snapshot);
    RUN_TEST(test_failed_restore_stays_lost_with_one_error_log);
    RUN_TEST(test_restore_meeting_a_new_loss_stays_lost_and_the_next_restore_works);
    RUN_TEST(test_lazy_sampler_recreate_on_a_latched_loss_ends_context_lost);
    RUN_TEST(test_link_with_a_stage_left_unready_by_a_loss_ends_unready);
    RUN_TEST(test_restore_defines_no_render_targets);
    RUN_TEST(test_sampler_cache_hit_defines_nothing);
    RUN_TEST(test_every_operation_records_one_begin_and_one_result);
    RUN_TEST(test_accepted_counters_match_recorded_results);
    RUN_TEST(test_capture_defines_inherited_resources_and_unknown_scissor);
    RUN_TEST(test_capture_initial_state_holds_carried_over_bindings);
    RUN_TEST(test_capture_initial_state_forgets_destroyed_and_lost_bindings);
    RUN_TEST(test_capture_initial_state_keeps_bindings_across_an_orphan);
    RUN_TEST(test_capture_shows_cache_for_equal_binds_and_merged_draws);
    RUN_TEST(test_depth_only_render_target_definition_has_no_color_fields);
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
