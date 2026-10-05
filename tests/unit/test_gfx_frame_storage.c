#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/nt_gfx_test_desc.h"

#include <string.h>

#include "graphics/nt_gfx.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

#define TEST_DESC NT_GFX_TEST_DESC(.max_shaders = 4, .max_programs = 4, .max_pipelines = 4, .max_buffers = 8, .max_textures = 4, .max_meshes = 4, .max_vertex_inputs = 4, .max_render_targets = 4)

static nt_pipeline_t s_pipeline;
static nt_vertex_input_t s_empty_input;

static void make_draw_state(void) {
    s_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_fake_make_program(NULL, 0)});
    s_empty_input = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
}

void setUp(void) {
    nt_gfx_init(&TEST_DESC);
    make_draw_state();
    nt_gfx_begin_frame();
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

static void next_frame(void) {
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
}

static uint32_t alloc_filled(nt_gfx_frame_stream_t stream, uint32_t size, uint32_t align, uint8_t value) {
    uint32_t offset = UINT32_MAX;
    memset(nt_gfx_frame_alloc(stream, size, align, &offset), value, size);
    return offset;
}

static void begin_draw_pass(void) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(s_pipeline);
    nt_gfx_bind_vertex_input(s_empty_input);
}

/* A recorded draw makes the stream non-empty, so its execution uploads. */
static void draw_pass(void) {
    begin_draw_pass();
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
}

// #region allocation
static void test_offsets_are_multiples_of_any_align_and_same_stride_runs_are_contiguous(void) {
    TEST_ASSERT_EQUAL_UINT32(0, alloc_filled(NT_GFX_FRAME_VERTEX, 5, 4, 0x11));
    TEST_ASSERT_EQUAL_UINT32(104, alloc_filled(NT_GFX_FRAME_VERTEX, 104, 104, 0x22));
    TEST_ASSERT_EQUAL_UINT32(208, alloc_filled(NT_GFX_FRAME_VERTEX, 208, 104, 0x33));
    TEST_ASSERT_EQUAL_UINT32(624, alloc_filled(NT_GFX_FRAME_VERTEX, 312, 312, 0x44));
    TEST_ASSERT_EQUAL_UINT32(936, g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    const uint8_t *staging = g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].staging;
    TEST_ASSERT_EQUAL_HEX8(0x11, staging[4]);
    TEST_ASSERT_EACH_EQUAL_HEX8(0, staging + 5, 99); /* padding is zeroed once at init */
    TEST_ASSERT_EQUAL_HEX8(0x33, staging[415]);
    TEST_ASSERT_EQUAL_HEX8(0x44, staging[624]);
    /* The streams are independent. */
    TEST_ASSERT_EQUAL_UINT32(0, alloc_filled(NT_GFX_FRAME_INDEX, 12, 4, 0x55));
    TEST_ASSERT_EQUAL_UINT32(0, alloc_filled(NT_GFX_FRAME_UNIFORM, 64, 256, 0x66));
    TEST_ASSERT_EQUAL_UINT32(256, alloc_filled(NT_GFX_FRAME_UNIFORM, 64, 256, 0x77));
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
static void test_overflow_and_bad_arguments_assert(void) {
    const uint32_t capacity = g_nt_gfx_frame_storage[NT_GFX_FRAME_INDEX].capacity;
    uint32_t offset = 0;
    (void)alloc_filled(NT_GFX_FRAME_INDEX, capacity - 8U, 4, 0);
    NT_TEST_EXPECT_ASSERT(nt_gfx_frame_alloc(NT_GFX_FRAME_INDEX, 12, 4, &offset));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "frame_capacity"));
    NT_TEST_EXPECT_ASSERT(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 0, 4, &offset));
    NT_TEST_EXPECT_ASSERT(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 4, 0, &offset));
    NT_TEST_EXPECT_ASSERT(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 4, 4, NULL));
    NT_TEST_EXPECT_ASSERT(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, UINT32_MAX, 4, &offset)); /* no wrap */
    /* The size fits the free bytes, the alignment padding does not. */
    (void)alloc_filled(NT_GFX_FRAME_UNIFORM, g_nt_gfx_frame_storage[NT_GFX_FRAME_UNIFORM].capacity - 14U, 1, 0);
    NT_TEST_EXPECT_ASSERT(nt_gfx_frame_alloc(NT_GFX_FRAME_UNIFORM, 8, 16, &offset));
    /* Exactly the capacity fits. */
    TEST_ASSERT_EQUAL_UINT32(capacity - 8U, alloc_filled(NT_GFX_FRAME_INDEX, 8, 4, 0));
}

static void test_zero_capacity_asserts_at_init(void) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = TEST_DESC;
    desc.frame_capacity[NT_GFX_FRAME_UNIFORM] = 0;
    NT_TEST_EXPECT_ASSERT(nt_gfx_init(&desc));
    nt_gfx_shutdown();
    nt_gfx_init(&TEST_DESC);
    nt_gfx_begin_frame();
}
#endif

static void test_begin_frame_publishes_use_and_empties_the_storage(void) {
    (void)alloc_filled(NT_GFX_FRAME_VERTEX, 40, 4, 0);
    (void)alloc_filled(NT_GFX_FRAME_UNIFORM, 16, 256, 0);
    nt_gfx_end_frame();
    (void)alloc_filled(NT_GFX_FRAME_INDEX, 8, 4, 0); /* after end_frame: still this frame's use */
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.counters.frame_bytes[NT_GFX_FRAME_VERTEX]);
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(40, g_nt_gfx.last_frame.frame_bytes[NT_GFX_FRAME_VERTEX]);
    TEST_ASSERT_EQUAL_UINT32(8, g_nt_gfx.last_frame.frame_bytes[NT_GFX_FRAME_INDEX]);
    TEST_ASSERT_EQUAL_UINT32(16, g_nt_gfx.last_frame.frame_bytes[NT_GFX_FRAME_UNIFORM]);
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx_frame_storage[s].used);
    }
    TEST_ASSERT_EQUAL_UINT32(0, alloc_filled(NT_GFX_FRAME_VERTEX, 4, 4, 0));
}
// #endregion

// #region upload
static void test_execution_uploads_each_allocated_storage_once_before_its_draws(void) {
    (void)alloc_filled(NT_GFX_FRAME_VERTEX, 24, 4, 0xAB);
    (void)alloc_filled(NT_GFX_FRAME_UNIFORM, 16, 256, 0xCD);
    const uint32_t updates = nt_gfx_fake_update_buffer_count();
    draw_pass();
    TEST_ASSERT_EQUAL_UINT32(updates, nt_gfx_fake_update_buffer_count()); /* recorded, not executed */
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(updates + 2U, nt_gfx_fake_update_buffer_count()); /* the empty index storage sends nothing */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_update_buffer_offset());
    TEST_ASSERT_EQUAL_UINT32(16, nt_gfx_fake_last_update_buffer_size());
    TEST_ASSERT_EQUAL_PTR(g_nt_gfx_frame_storage[NT_GFX_FRAME_UNIFORM].staging, nt_gfx_fake_last_update_buffer_data());
    nt_gfx_begin_frame();
}

static void test_an_empty_stream_uploads_nothing(void) {
    (void)alloc_filled(NT_GFX_FRAME_VERTEX, 24, 4, 0);
    const uint32_t updates = nt_gfx_fake_update_buffer_count();
    next_frame();
    TEST_ASSERT_EQUAL_UINT32(updates, nt_gfx_fake_update_buffer_count());
}

static void test_a_mid_frame_execution_uploads_and_the_next_one_sends_only_the_delta(void) {
    const nt_buffer_t game = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16});
    (void)alloc_filled(NT_GFX_FRAME_VERTEX, 24, 4, 0);
    const uint32_t updates = nt_gfx_fake_update_buffer_count();
    draw_pass();
    nt_gfx_update_buffer(game, 0, (const uint8_t[16]){0}, 16); /* executes the stream first */
    TEST_ASSERT_EQUAL_UINT32(updates + 2U, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.counters.accepted[NT_GFX_OP_BUFFER_UPLOAD]); /* the frame storage upload, then the game write */
    (void)alloc_filled(NT_GFX_FRAME_VERTEX, 10, 4, 0);
    draw_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(updates + 3U, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_EQUAL_UINT32(24, nt_gfx_fake_last_update_buffer_offset());
    TEST_ASSERT_EQUAL_UINT32(10, nt_gfx_fake_last_update_buffer_size());
    nt_gfx_begin_frame();
    nt_gfx_destroy_buffer(game);
}

static void test_allocating_between_draws_keeps_the_merge(void) {
    nt_gfx_fake_draw_trace_reset(true);
    begin_draw_pass();
    nt_gfx_draw(0, 3);
    (void)alloc_filled(NT_GFX_FRAME_VERTEX, 24, 4, 0);
    nt_gfx_draw(3, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    nt_gfx_begin_frame();
}

static void test_indexed_draws_read_the_index_storage_as_uint32(void) {
    const nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}},
        .vertex_buffer = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX),
        .index_buffer = nt_gfx_frame_buffer(NT_GFX_FRAME_INDEX),
    });
    uint32_t vertex_offset = 0;
    uint32_t index_offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 36, 12, &vertex_offset), 0, 36);
    uint32_t *indices = (uint32_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_INDEX, 3 * sizeof(uint32_t), 4, &index_offset);
    for (uint32_t i = 0; i < 3; i++) {
        indices[i] = (vertex_offset / 12U) + i;
    }
    nt_gfx_fake_draw_trace_reset(true);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(s_pipeline);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw_indexed(index_offset / 4U, 3, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT8(NT_INDEX_UINT32, nt_gfx_fake_draw_trace_at(0).index_type);
    nt_gfx_begin_frame();
}
// #endregion

// #region restore
static void test_restore_makes_new_buffers_and_a_lost_frame_uploads_nothing(void) {
    nt_buffer_t before[NT_GFX_FRAME_STREAM_COUNT];
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        before[s] = nt_gfx_frame_buffer((nt_gfx_frame_stream_t)s);
    }
    nt_gfx_fake_set_context_lost(true);
    next_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    (void)alloc_filled(NT_GFX_FRAME_VERTEX, 24, 4, 0);
    const uint32_t updates = nt_gfx_fake_update_buffer_count();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_fake_set_context_lost(false);
    next_frame();
    TEST_ASSERT_EQUAL_UINT32(updates, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        const nt_buffer_t after = nt_gfx_frame_buffer((nt_gfx_frame_stream_t)s);
        TEST_ASSERT_NOT_EQUAL_UINT32(0, after.id);
        TEST_ASSERT_NOT_EQUAL_UINT32(before[s].id, after.id);
    }
    /* A loss frees pipelines and vertex inputs: the draw state is made again, like a game would. */
    make_draw_state();
    (void)alloc_filled(NT_GFX_FRAME_VERTEX, 24, 4, 0);
    draw_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(updates + 1U, nt_gfx_fake_update_buffer_count());
    nt_gfx_begin_frame();
}
/* No spare buffer slot: the restore frees the wiped buffers before it makes new ones,
 * also when the first restore meets a new loss and a later one succeeds. */
static void test_a_retried_restore_reuses_the_frame_buffer_slots(void) {
    nt_gfx_end_frame();
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = TEST_DESC;
    desc.max_buffers = NT_GFX_FRAME_STREAM_COUNT;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(true);
    next_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_fake_lose_context_during_next_restore();
    next_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    nt_gfx_fake_set_context_lost(false);
    next_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_frame_buffer((nt_gfx_frame_stream_t)s).id);
    }
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* On a live context a frame buffer that cannot be made is a bug, not a loss. */
static void test_a_failed_frame_buffer_creation_on_a_live_context_asserts(void) {
    nt_gfx_fake_set_context_lost(true);
    next_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_fake_fail_buffer_creates(1);
    nt_gfx_end_frame();
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_frame());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "frame storage buffer creation failed"));
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
    nt_gfx_init(&TEST_DESC);
    nt_gfx_begin_frame();
}
#endif
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_offsets_are_multiples_of_any_align_and_same_stride_runs_are_contiguous);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_overflow_and_bad_arguments_assert);
    RUN_TEST(test_zero_capacity_asserts_at_init);
#endif
    RUN_TEST(test_begin_frame_publishes_use_and_empties_the_storage);
    RUN_TEST(test_execution_uploads_each_allocated_storage_once_before_its_draws);
    RUN_TEST(test_an_empty_stream_uploads_nothing);
    RUN_TEST(test_a_mid_frame_execution_uploads_and_the_next_one_sends_only_the_delta);
    RUN_TEST(test_allocating_between_draws_keeps_the_merge);
    RUN_TEST(test_indexed_draws_read_the_index_storage_as_uint32);
    RUN_TEST(test_restore_makes_new_buffers_and_a_lost_frame_uploads_nothing);
    RUN_TEST(test_a_retried_restore_reuses_the_frame_buffer_slots);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_a_failed_frame_buffer_creation_on_a_live_context_asserts);
#endif
    return UNITY_END();
}
