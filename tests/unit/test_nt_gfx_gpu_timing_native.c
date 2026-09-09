#include <stdlib.h>

#include "unity.h"

/* Include the real backend so the fixture can supply capabilities without a window. */
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "graphics/gl/nt_gfx_gl.c"

static bool s_fixture_lost;
static bool s_fixture_supported;
static unsigned int s_gen_count;
static unsigned int s_delete_count;
static unsigned int s_begin_count;
static unsigned int s_end_count;
static unsigned int s_push_count;
static unsigned int s_pop_count;
static unsigned int s_available_count;
static unsigned int s_result_count;
static GLuint s_next_query;
static bool s_fixture_available;
static GLuint64 s_fixture_result;

bool nt_gfx_gl_ctx_create(const nt_gfx_desc_t *desc) {
    (void)desc;
    return true;
}
void nt_gfx_gl_ctx_destroy(void) {}
bool nt_gfx_gl_ctx_is_lost(void) { return s_fixture_lost; }
bool nt_gfx_gl_ctx_enable_timer_query(void) { return s_fixture_supported; }
bool nt_gfx_gl_ctx_enable_debug_groups(void) { return true; }
bool nt_gfx_gl_ctx_enable_debug_callback(void) { return false; }
nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void) { return (nt_gfx_gpu_caps_t){0}; }

static void GLAD_API_PTR capture_gen(GLsizei count, GLuint *queries) {
    s_gen_count += (unsigned int)count;
    for (GLsizei i = 0; i < count; i++) {
        queries[i] = ++s_next_query;
    }
}
static void GLAD_API_PTR capture_delete(GLsizei count, const GLuint *queries) {
    (void)queries;
    s_delete_count += (unsigned int)count;
}
static void GLAD_API_PTR capture_begin(GLenum target, GLuint query) {
    TEST_ASSERT_EQUAL_HEX32(GL_TIME_ELAPSED, target);
    TEST_ASSERT_NOT_EQUAL(0, query);
    s_begin_count++;
}
static void GLAD_API_PTR capture_end(GLenum target) {
    TEST_ASSERT_EQUAL_HEX32(GL_TIME_ELAPSED, target);
    s_end_count++;
}
static void GLAD_API_PTR capture_push(GLenum source, GLuint id, GLsizei length, const GLchar *name) {
    (void)source;
    (void)id;
    (void)length;
    TEST_ASSERT_NOT_NULL(name);
    s_push_count++;
}
static void GLAD_API_PTR capture_pop(void) { s_pop_count++; }
static void GLAD_API_PTR capture_available(GLuint query, GLenum pname, GLuint *out) {
    (void)query;
    TEST_ASSERT_EQUAL_HEX32(GL_QUERY_RESULT_AVAILABLE, pname);
    s_available_count++;
    *out = s_fixture_available ? 1U : 0U;
}
static void GLAD_API_PTR capture_result(GLuint query, GLenum pname, GLuint64 *out) {
    (void)query;
    TEST_ASSERT_EQUAL_HEX32(GL_QUERY_RESULT, pname);
    s_result_count++;
    *out = s_fixture_result;
}
static void GLAD_API_PTR capture_gen_vao(GLsizei count, GLuint *names) {
    for (GLsizei i = 0; i < count; i++) {
        names[i] = 1;
    }
}
static void GLAD_API_PTR capture_delete_vao(GLsizei count, const GLuint *names) {
    (void)count;
    (void)names;
}

void setUp(void) {
    glad_glGenQueries = capture_gen;
    glad_glDeleteQueries = capture_delete;
    glad_glBeginQuery = capture_begin;
    glad_glEndQuery = capture_end;
    glad_glPushDebugGroup = capture_push;
    glad_glPopDebugGroup = capture_pop;
    glad_glGetQueryObjectuiv = capture_available;
    glad_glGetQueryObjectui64v = capture_result;
    glad_glGenVertexArrays = capture_gen_vao;
    glad_glDeleteVertexArrays = capture_delete_vao;
    s_fixture_lost = false;
    s_fixture_supported = true;
    s_fixture_available = false;
    s_fixture_result = UINT64_C(0x123456789);
    s_next_query = 0;
    s_gen_count = 0;
    s_delete_count = 0;
    s_begin_count = 0;
    s_end_count = 0;
    s_push_count = 0;
    s_pop_count = 0;
    s_available_count = 0;
    s_result_count = 0;
    nt_gfx_backend_drop_timer_segments();
    nt_gfx_gl_init_context_features();
    nt_gfx_backend_set_gpu_timing_enabled(true);
}

void tearDown(void) {
    /* A failed assertion must not carry an unfinished query into the next case. */
    nt_gfx_backend_drop_timer_segments();
    nt_gfx_backend_shutdown();
}

#if NT_GFX_GPU_TIMING_ENABLED
static void test_disable_active_balances_query_and_debug_group(void) {
    nt_gfx_backend_begin_segment("active");
    TEST_ASSERT_EQUAL_UINT(1, s_begin_count);
    TEST_ASSERT_EQUAL_UINT(1, s_push_count);
    nt_gfx_backend_set_gpu_timing_enabled(false);
    TEST_ASSERT_EQUAL_UINT(1, s_end_count);
    TEST_ASSERT_EQUAL_UINT(1, s_pop_count);
    unsigned int allocated = s_gen_count;
    uint64_t out = 19;
    for (unsigned int i = 0; i < 3; i++) {
        nt_gfx_backend_begin_frame();
        nt_gfx_backend_begin_segment("disabled-new-name");
        nt_gfx_backend_end_segment();
        nt_gfx_backend_set_gpu_timing_enabled(false);
        TEST_ASSERT_FALSE(nt_gfx_backend_poll_segment_time_ns("active", &out));
    }
    TEST_ASSERT_EQUAL_UINT(allocated, s_gen_count);
    TEST_ASSERT_EQUAL_UINT(0, s_delete_count);
    TEST_ASSERT_EQUAL_UINT(1, s_begin_count);
    TEST_ASSERT_EQUAL_UINT(1, s_end_count);
    TEST_ASSERT_EQUAL_UINT(1, s_pop_count);
    TEST_ASSERT_EQUAL_UINT(0, s_available_count);
    TEST_ASSERT_EQUAL_UINT(0, s_result_count);
    TEST_ASSERT_TRUE(nt_gfx_backend_is_gpu_timing_supported());
}

static void test_pending_cancelled_and_reenable_returns_only_new_result(void) {
    nt_gfx_backend_begin_segment("pending");
    nt_gfx_backend_end_segment();
    nt_gfx_backend_set_gpu_timing_enabled(false);
    nt_gfx_backend_set_gpu_timing_enabled(true);
    uint64_t out = 19;
    TEST_ASSERT_FALSE(nt_gfx_backend_poll_segment_time_ns("pending", &out));
    TEST_ASSERT_EQUAL_UINT(0, s_available_count);
    nt_gfx_backend_begin_segment("pending");
    nt_gfx_backend_end_segment();
    TEST_ASSERT_FALSE(nt_gfx_backend_poll_segment_time_ns("pending", &out));
    TEST_ASSERT_EQUAL_UINT64(19, out);
    TEST_ASSERT_EQUAL_UINT(0, s_result_count);
    s_fixture_available = true;
    TEST_ASSERT_TRUE(nt_gfx_backend_poll_segment_time_ns("pending", &out));
    TEST_ASSERT_EQUAL_UINT64(s_fixture_result, out);
    TEST_ASSERT_FALSE(nt_gfx_backend_poll_segment_time_ns("pending", &out));
    TEST_ASSERT_EQUAL_UINT(1, s_result_count);
    TEST_ASSERT_EQUAL_UINT(2, s_push_count);
    TEST_ASSERT_EQUAL_UINT(2, s_pop_count);
}

static void test_lost_setter_and_drop_do_not_touch_dead_names(void) {
    nt_gfx_backend_begin_segment("lost");
    s_fixture_lost = true;
    nt_gfx_backend_set_gpu_timing_enabled(false);
    nt_gfx_backend_end_segment();
    nt_gfx_backend_drop_timer_segments();
    TEST_ASSERT_EQUAL_UINT(0, s_end_count);
    TEST_ASSERT_EQUAL_UINT(0, s_pop_count);
    TEST_ASSERT_EQUAL_UINT(0, s_delete_count);
    s_fixture_lost = false;
    nt_gfx_gl_init_context_features();
    nt_gfx_backend_begin_segment("still-disabled");
    TEST_ASSERT_EQUAL_UINT(1, s_begin_count);
    nt_gfx_backend_set_gpu_timing_enabled(true);
    nt_gfx_backend_begin_segment("restored");
    TEST_ASSERT_EQUAL_UINT(2, s_begin_count);
    nt_gfx_backend_end_segment();
}

static void test_shutdown_closes_active_then_deletes_retained_queries(void) {
    nt_gfx_backend_begin_segment("shutdown");
    unsigned int allocated = s_gen_count;
    nt_gfx_backend_shutdown();
    TEST_ASSERT_EQUAL_UINT(1, s_end_count);
    TEST_ASSERT_EQUAL_UINT(1, s_pop_count);
    TEST_ASSERT_EQUAL_UINT(allocated, s_delete_count);
    TEST_ASSERT_EQUAL_UINT(0, s_result_count);
}

static void test_lost_shutdown_does_not_delete_queries(void) {
    nt_gfx_backend_begin_segment("lost-shutdown");
    s_fixture_lost = true;
    nt_gfx_backend_shutdown();
    TEST_ASSERT_EQUAL_UINT(0, s_end_count);
    TEST_ASSERT_EQUAL_UINT(0, s_pop_count);
    TEST_ASSERT_EQUAL_UINT(0, s_delete_count);
}
#endif

static void test_unavailable_timer_is_inert(void) {
#if !NT_GFX_GPU_TIMING_ENABLED
    TEST_ASSERT_FALSE(nt_gfx_backend_is_gpu_timing_supported());
    nt_gfx_backend_begin_segment("compiled-out");
    nt_gfx_backend_end_segment();
    TEST_ASSERT_EQUAL_UINT(0, s_gen_count);
#endif
    s_fixture_supported = false;
    nt_gfx_gl_init_context_features();
    TEST_ASSERT_FALSE(nt_gfx_backend_is_gpu_timing_supported());
    nt_gfx_backend_begin_segment("unsupported");
    nt_gfx_backend_end_segment();
    uint64_t out = 19;
    TEST_ASSERT_FALSE(nt_gfx_backend_poll_segment_time_ns("unsupported", &out));
    TEST_ASSERT_EQUAL_UINT(0, s_gen_count);
    TEST_ASSERT_EQUAL_UINT(0, s_begin_count);
    TEST_ASSERT_EQUAL_UINT(0, s_end_count);
    TEST_ASSERT_EQUAL_UINT(0, s_available_count);
#if !NT_GFX_GPU_TIMING_ENABLED
    TEST_ASSERT_EQUAL_UINT64(0, out);
#endif
}

int main(void) {
    UNITY_BEGIN();
#if NT_GFX_GPU_TIMING_ENABLED
    RUN_TEST(test_disable_active_balances_query_and_debug_group);
    RUN_TEST(test_pending_cancelled_and_reenable_returns_only_new_result);
    RUN_TEST(test_lost_setter_and_drop_do_not_touch_dead_names);
    RUN_TEST(test_shutdown_closes_active_then_deletes_retained_queries);
    RUN_TEST(test_lost_shutdown_does_not_delete_queries);
#endif
    RUN_TEST(test_unavailable_timer_is_inert);
    return UNITY_END();
}
