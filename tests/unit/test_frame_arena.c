#include "test_helpers/nt_gfx_fake.h"

#include <string.h>

#include "core/nt_assert.h"
#include "frame_arena/nt_frame_arena.h"
#include "graphics/nt_gfx.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

void setUp(void) {
    /* One buffer slot: only the arena creates buffers, so a restore that leaks its old buffer cannot create. */
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 4, .max_programs = 4, .max_pipelines = 4, .max_buffers = 1, .max_textures = 4, .max_meshes = 4, .max_vertex_inputs = 4, .max_render_targets = 4});
}

void tearDown(void) {
    nt_assert_handler = NULL;
    nt_frame_arena_shutdown();
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

static void arena_init(uint32_t capacity) { TEST_ASSERT_EQUAL(NT_OK, nt_frame_arena_init(&(nt_frame_arena_desc_t){.capacity = capacity})); }

static void next_frame(void) {
    nt_gfx_begin_frame();
    nt_frame_arena_begin_frame();
}

static uint32_t reserve_filled(uint32_t size, uint8_t value) {
    uint32_t offset = UINT32_MAX;
    memset(nt_frame_arena_reserve(size, &offset), value, size);
    return offset;
}

static void assert_last_buffer_is_the_arena(uint32_t capacity) {
    nt_buffer_desc_t d = nt_gfx_fake_last_buffer_desc();
    TEST_ASSERT_EQUAL(NT_BUFFER_VERTEX, d.type);
    TEST_ASSERT_EQUAL(NT_USAGE_STREAM, d.usage);
    TEST_ASSERT_EQUAL_UINT32(capacity, d.size);
    TEST_ASSERT_NULL(d.data);
}

// #region frame
static void test_init_creates_one_stream_vertex_buffer_of_the_capacity(void) {
    arena_init(64);
    assert_last_buffer_is_the_arena(64);
}

static void test_reserves_are_disjoint_and_16_aligned(void) {
    arena_init(64);
    next_frame();
    TEST_ASSERT_EQUAL_UINT32(0, reserve_filled(6, 0xA1));
    TEST_ASSERT_EQUAL_UINT32(16, reserve_filled(16, 0xB2));
    TEST_ASSERT_EQUAL_UINT32(32, reserve_filled(1, 0xC3));
}

static void test_upload_sends_every_reserved_byte_in_one_call(void) {
    arena_init(64);
    next_frame();
    uint32_t a = reserve_filled(8, 0xA1);
    uint32_t b = reserve_filled(12, 0xB2);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_update_buffer_count());

    nt_frame_arena_upload();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_update_buffer_offset());
    TEST_ASSERT_EQUAL_UINT32(32, nt_gfx_fake_last_update_buffer_size());
    const uint8_t *sent = (const uint8_t *)nt_gfx_fake_last_update_buffer_data();
    uint8_t expect_a[8];
    uint8_t expect_b[12];
    uint8_t zero_pad[8] = {0};
    memset(expect_a, 0xA1, sizeof expect_a);
    memset(expect_b, 0xB2, sizeof expect_b);
    TEST_ASSERT_EQUAL_MEMORY(expect_a, sent + a, sizeof expect_a);
    TEST_ASSERT_EQUAL_MEMORY(zero_pad, sent + a + sizeof expect_a, sizeof zero_pad);
    TEST_ASSERT_EQUAL_MEMORY(expect_b, sent + b, sizeof expect_b);
}

static void test_upload_of_an_empty_frame_sends_nothing(void) {
    arena_init(64);
    next_frame();
    nt_frame_arena_upload();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_frame_arena_buffer().id);
}

static void test_every_frame_reuses_one_buffer_from_offset_0(void) {
    arena_init(64);
    next_frame();
    (void)reserve_filled(32, 0xA1);
    nt_frame_arena_upload();
    nt_buffer_t first = nt_frame_arena_buffer();

    next_frame();
    TEST_ASSERT_EQUAL_UINT32(0, reserve_filled(4, 0xB2));
    nt_frame_arena_upload();
    TEST_ASSERT_EQUAL_UINT32(first.id, nt_frame_arena_buffer().id);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_EQUAL_UINT32(16, nt_gfx_fake_last_update_buffer_size());
}

static void test_reserve_fills_the_capacity_exactly(void) {
    arena_init(32);
    next_frame();
    (void)reserve_filled(10, 0xA1);
    TEST_ASSERT_EQUAL_UINT32(16, reserve_filled(16, 0xB2));
    nt_frame_arena_upload();
    TEST_ASSERT_EQUAL_UINT32(32, nt_gfx_fake_last_update_buffer_size());
}

static void test_peak_keeps_the_largest_uploaded_frame(void) {
    arena_init(64);
    TEST_ASSERT_EQUAL_UINT32(0, nt_frame_arena_peak());
    next_frame();
    (void)reserve_filled(6, 0xA1);
    (void)reserve_filled(16, 0xB2);
    nt_frame_arena_upload();
    TEST_ASSERT_EQUAL_UINT32(32, nt_frame_arena_peak());

    next_frame();
    (void)reserve_filled(4, 0xC3);
    nt_frame_arena_upload();
    TEST_ASSERT_EQUAL_UINT32(32, nt_frame_arena_peak());
}
// #endregion

// #region lifecycle
static void test_restore_replaces_the_buffer_without_headroom(void) {
    arena_init(64);
    next_frame();
    nt_frame_arena_upload();
    nt_buffer_t before = nt_frame_arena_buffer();

    TEST_ASSERT_EQUAL(NT_OK, nt_frame_arena_restore_gpu());
    assert_last_buffer_is_the_arena(64);
    next_frame();
    (void)reserve_filled(8, 0xA1);
    nt_frame_arena_upload();
    nt_buffer_t after = nt_frame_arena_buffer();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, after.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(before.id, after.id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_update_buffer_count());
}

/* The restored buffer is empty: draws of this frame need a fresh upload of the surviving staging. */
static void test_restore_after_upload_requires_a_new_upload(void) {
    arena_init(64);
    next_frame();
    (void)reserve_filled(8, 0xA1);
    nt_frame_arena_upload();
    TEST_ASSERT_EQUAL(NT_OK, nt_frame_arena_restore_gpu());
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT((void)nt_frame_arena_buffer());
    nt_assert_handler = NULL;
#endif
    nt_frame_arena_upload();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_EQUAL_UINT32(16, nt_gfx_fake_last_update_buffer_size());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_frame_arena_buffer().id);
}

static void test_restore_when_inactive_is_ok(void) { TEST_ASSERT_EQUAL(NT_OK, nt_frame_arena_restore_gpu()); }

static void test_failed_restore_is_retried_before_the_next_upload(void) {
    arena_init(64);
    nt_gfx_fake_fail_buffer_creates(1);
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_frame_arena_restore_gpu());
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    next_frame();
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT(nt_frame_arena_upload());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "retry failed GPU restore"));
    nt_assert_handler = NULL;
#endif
    TEST_ASSERT_EQUAL(NT_OK, nt_frame_arena_restore_gpu());
    next_frame();
    (void)reserve_filled(24, 0xA1);
    nt_frame_arena_upload();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_EQUAL_UINT32(32, nt_gfx_fake_last_update_buffer_size());
}

static void test_failed_init_leaves_the_module_down(void) {
    nt_gfx_fake_fail_buffer_creates(1);
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_frame_arena_init(&(nt_frame_arena_desc_t){.capacity = 64}));
    /* Down, and its failed create left no live slot: the single slot is free for a new init. */
    arena_init(64);
    assert_last_buffer_is_the_arena(64);
}

static void test_shutdown_then_init_starts_clean(void) {
    arena_init(64);
    next_frame();
    (void)reserve_filled(48, 0xA1);
    nt_frame_arena_upload();
    nt_frame_arena_shutdown();

    arena_init(32);
    assert_last_buffer_is_the_arena(32);
    TEST_ASSERT_EQUAL_UINT32(0, nt_frame_arena_peak());
    next_frame();
    TEST_ASSERT_EQUAL_UINT32(0, reserve_filled(32, 0xB2));
}
/* A frame that skips prepare keeps drawing the last upload: nothing rewrote the buffer. */
static void test_buffer_stays_drawable_until_the_next_begin_frame(void) {
    arena_init(64);
    next_frame();
    (void)reserve_filled(4, 0xA1);
    nt_frame_arena_upload();
    nt_buffer_t uploaded = nt_frame_arena_buffer();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(uploaded.id, nt_frame_arena_buffer().id);
}
// #endregion

// #region asserts
#if NT_ASSERT_MODE == NT_ASSERT_FULL
static void expect_assert_message(const char *needle) { TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, needle)); }

static void test_init_rejects_a_capacity_not_multiple_of_the_alignment(void) {
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT((void)nt_frame_arena_init(&(nt_frame_arena_desc_t){.capacity = 40}));
}

static void test_reserve_rejects_zero_size(void) {
    arena_init(64);
    next_frame();
    nt_test_assert_install();
    uint32_t offset = 0;
    NT_TEST_EXPECT_ASSERT((void)nt_frame_arena_reserve(0, &offset));
}

static void test_reserve_rejects_a_null_offset(void) {
    arena_init(64);
    next_frame();
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT((void)nt_frame_arena_reserve(4, NULL));
}

static void test_reserve_asserts_past_the_capacity(void) {
    arena_init(32);
    next_frame();
    (void)reserve_filled(16, 0xA1);
    nt_test_assert_install();
    uint32_t offset = 0;
    NT_TEST_EXPECT_ASSERT((void)nt_frame_arena_reserve(17, &offset));
    expect_assert_message("capacity exceeded");
}

/* Rounding UINT32_MAX up wraps to 0; the guard must not read that as a fit. */
static void test_reserve_asserts_when_rounding_wraps(void) {
    arena_init(32);
    next_frame();
    nt_test_assert_install();
    uint32_t offset = 0;
    NT_TEST_EXPECT_ASSERT((void)nt_frame_arena_reserve(UINT32_MAX, &offset));
    expect_assert_message("capacity exceeded");
}

static void test_reserve_after_upload_asserts(void) {
    arena_init(64);
    next_frame();
    nt_frame_arena_upload();
    nt_test_assert_install();
    uint32_t offset = 0;
    NT_TEST_EXPECT_ASSERT((void)nt_frame_arena_reserve(4, &offset));
    expect_assert_message("reserve after upload");
}

static void test_second_upload_in_a_frame_asserts(void) {
    arena_init(64);
    next_frame();
    nt_frame_arena_upload();
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT(nt_frame_arena_upload());
    expect_assert_message("second upload");
}

static void test_buffer_before_upload_asserts(void) {
    arena_init(64);
    next_frame();
    (void)reserve_filled(4, 0xA1);
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT((void)nt_frame_arena_buffer());
    expect_assert_message("draw before upload");
}

static void test_second_begin_frame_in_one_gfx_frame_asserts(void) {
    arena_init(64);
    next_frame();
    nt_frame_arena_upload();
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT(nt_frame_arena_begin_frame());
    expect_assert_message("twice in one gfx frame");
}
#endif
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_creates_one_stream_vertex_buffer_of_the_capacity);
    RUN_TEST(test_reserves_are_disjoint_and_16_aligned);
    RUN_TEST(test_upload_sends_every_reserved_byte_in_one_call);
    RUN_TEST(test_upload_of_an_empty_frame_sends_nothing);
    RUN_TEST(test_every_frame_reuses_one_buffer_from_offset_0);
    RUN_TEST(test_reserve_fills_the_capacity_exactly);
    RUN_TEST(test_peak_keeps_the_largest_uploaded_frame);
    RUN_TEST(test_restore_replaces_the_buffer_without_headroom);
    RUN_TEST(test_restore_after_upload_requires_a_new_upload);
    RUN_TEST(test_restore_when_inactive_is_ok);
    RUN_TEST(test_failed_restore_is_retried_before_the_next_upload);
    RUN_TEST(test_failed_init_leaves_the_module_down);
    RUN_TEST(test_shutdown_then_init_starts_clean);
    RUN_TEST(test_buffer_stays_drawable_until_the_next_begin_frame);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_init_rejects_a_capacity_not_multiple_of_the_alignment);
    RUN_TEST(test_reserve_rejects_zero_size);
    RUN_TEST(test_reserve_rejects_a_null_offset);
    RUN_TEST(test_reserve_asserts_past_the_capacity);
    RUN_TEST(test_reserve_asserts_when_rounding_wraps);
    RUN_TEST(test_reserve_after_upload_asserts);
    RUN_TEST(test_second_upload_in_a_frame_asserts);
    RUN_TEST(test_buffer_before_upload_asserts);
    RUN_TEST(test_second_begin_frame_in_one_gfx_frame_asserts);
#endif
    return UNITY_END();
}
