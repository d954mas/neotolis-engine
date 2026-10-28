/* L2 gfx group via submit() over nt_gfx_fake: the recording mode, the ring of recent frames, reads by
   sequence and by sim frame, and the frame_not_ready / unknown_frame split. The TU includes the group
   source with a small ring so eviction is reachable. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NT_DEVAPI_GFX_RING_RECORDS 64U
#define NT_DEVAPI_GFX_RING_FRAMES 4U

/* clang-format off */
#include "app/nt_app.h"
// NOLINTNEXTLINE(bugprone-suspicious-include): drive the static ring with crafted frames.
#include "devapi/nt_devapi_gfx.c"
#include "devapi/nt_devapi_internal.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"
/* clang-format on */

static void init_gfx(uint32_t capacity) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = capacity;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
}

void setUp(void) {
    g_nt_app = (nt_app_t){.max_dt = 0.1F, .scale = 1.0F, .step_dt = 1.0F / 60.0F, .render_enabled = true};
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_devapi_register_gfx();
    init_gfx(256);
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
    nt_devapi_shutdown();
}

/* ---- helpers ---- */

/* One host frame in devapi_host order; advance = the simulation stepped this frame. */
static void frame(bool advance) {
    if (advance) {
        g_nt_app.frame++;
    }
    nt_gfx_begin_frame();
    nt_devapi_update();
    nt_gfx_end_frame();
}

static cJSON *submit(const char *line) {
    const char *resp = nt_devapi_submit(line);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    return root;
}

static cJSON *result_of(cJSON *root) {
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    return cJSON_GetObjectItemCaseSensitive(root, "result");
}

static void expect_error(const char *line, const char *code) {
    cJSON *root = submit(line);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING(code, cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    cJSON_Delete(root);
}

static double number(const cJSON *obj, const char *key) {
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, key);
    TEST_ASSERT_TRUE(cJSON_IsNumber(n));
    return n->valuedouble;
}

static void record(bool enabled) { cJSON_Delete(submit(enabled ? "{\"method\":\"gfx.record\",\"params\":{\"enabled\":true}}" : "{\"method\":\"gfx.record\",\"params\":{\"enabled\":false}}")); }

/* Sequence of the read frame, or 0 when the read failed. */
static uint64_t read_sequence(const char *line) {
    cJSON *root = submit(line);
    uint64_t sequence = 0;
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok"))) {
        const cJSON *result = result_of(root);
        TEST_ASSERT_EQUAL_STRING_LEN("ntcapture 1\n", cJSON_GetObjectItemCaseSensitive(result, "text")->valuestring, 12);
        sequence = (uint64_t)number(result, "sequence");
    }
    cJSON_Delete(root);
    return sequence;
}

/* ---- the frame of a step ---- */

static void test_sim_frame_read_returns_the_step_frame_not_idle_frames(void) {
    record(true);
    frame(false);
    frame(false);
    frame(true); /* the step */
    const uint64_t step = g_nt_gfx.counters.frame_sequence;
    const uint32_t sim = g_nt_app.frame;
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"gfx.read\",\"params\":{\"sim_frame\":%u}}", sim);
    /* Still recording: the step frame finalizes at the next begin_frame. */
    expect_error(line, "frame_not_ready");
    frame(false);
    TEST_ASSERT_EQUAL_UINT64(step, read_sequence(line));
    TEST_ASSERT_EQUAL_UINT64(step, read_sequence("{\"method\":\"gfx.read\"}"));
    /* Idle frames of the same sim frame never answer, even once the step frame is evicted. */
    for (int i = 0; i < 6; i++) {
        frame(false);
    }
    expect_error(line, "unknown_frame");
    TEST_ASSERT_EQUAL_UINT64(g_nt_gfx.counters.frame_sequence - 1U, read_sequence("{\"method\":\"gfx.read\"}"));
}

static void test_recording_while_paused_stores_no_advanced_frame(void) {
    record(true);
    frame(false);
    frame(false);
    frame(false);
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    const cJSON *frames = cJSON_GetObjectItemCaseSensitive(result_of(root), "frames");
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(frames));
    const cJSON *f = NULL;
    cJSON_ArrayForEach(f, frames) {
        TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(f, "advanced")));
        TEST_ASSERT_EQUAL_UINT32(g_nt_app.frame, (uint32_t)number(f, "sim_frame"));
    }
    cJSON_Delete(root);
    /* The current sim frame began before recording: no frame advanced to it. */
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"gfx.read\",\"params\":{\"sim_frame\":%u}}", g_nt_app.frame);
    expect_error(line, "unknown_frame");
}

/* ---- the ring ---- */

static nt_gfx_event_t s_source[NT_DEVAPI_GFX_RING_RECORDS];

static void store_crafted(uint64_t sequence, uint32_t count) {
    nt_gfx_capture_view_t view = {.events = s_source, .count = count};
    view.counters.frame_sequence = sequence;
    store_frame(&view, 0, false);
}

static void expect_ring(const uint64_t *sequences, uint32_t count) {
    TEST_ASSERT_EQUAL_UINT32(count, s_ring.count);
    for (uint32_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_UINT64(sequences[i], frame_at(i)->view.counters.frame_sequence);
        TEST_ASSERT_TRUE(frame_at(i)->first + frame_at(i)->view.count <= NT_DEVAPI_GFX_RING_RECORDS);
    }
}

static void test_ring_wraps_and_evicts_through_the_newest_overlap(void) {
    store_crafted(1, 40);
    store_crafted(2, 18); /* [40,58) */
    store_crafted(3, 24); /* wraps to [0,24), evicts 1 */
    store_crafted(4, 16); /* [24,40) */
    expect_ring((const uint64_t[]){2, 3, 4}, 3);
    TEST_ASSERT_EQUAL_UINT32(40, frame_at(0)->first);
    /* 30 records wrap to 0 and overlap 3 and 4, so 2, 3 and 4 all leave. */
    store_crafted(5, 30);
    expect_ring((const uint64_t[]){5}, 1);
    TEST_ASSERT_EQUAL_UINT32(0, frame_at(0)->first);
    /* Exact fit at the end, then the frame-count limit. */
    store_crafted(6, 34);
    expect_ring((const uint64_t[]){5, 6}, 2);
    for (uint64_t seq = 7; seq <= 9; seq++) {
        store_crafted(seq, 0);
    }
    expect_ring((const uint64_t[]){6, 7, 8, 9}, 4);
}

static void test_recorded_frames_stay_gapless_over_many_wraps(void) {
    record(true);
    for (int i = 0; i < 40; i++) {
        frame((i % 3) == 0);
    }
    TEST_ASSERT_EQUAL_UINT32(NT_DEVAPI_GFX_RING_FRAMES, s_ring.count);
    for (uint32_t i = 1; i < s_ring.count; i++) {
        TEST_ASSERT_EQUAL_UINT64(frame_at(i - 1U)->view.counters.frame_sequence + 1U, frame_at(i)->view.counters.frame_sequence);
    }
}

/* ---- on / off / reset ---- */

static void test_off_keeps_the_ring_and_stores_no_frame_in_flight(void) {
    record(true);
    frame(false);
    frame(false);
    const uint32_t stored = s_ring.count;
    record(false);
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(stored, s_ring.count);
    TEST_ASSERT_NOT_EQUAL_UINT64(0, read_sequence("{\"method\":\"gfx.read\"}"));
}

static void test_a_frame_open_at_on_is_never_stored(void) {
    record(true);
    frame(false);
    /* Between frames, off then on: the frame gfx records next began after on. */
    record(false);
    record(true);
    const uint64_t on = g_nt_gfx.counters.frame_sequence;
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT64(on + 1U, frame_at(0)->view.counters.frame_sequence);
    /* From C between begin_frame and update: that frame is not recording and is skipped. */
    nt_gfx_begin_frame();
    nt_devapi_gfx_record(true);
    const uint64_t open = g_nt_gfx.counters.frame_sequence;
    nt_devapi_update();
    nt_gfx_end_frame();
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(1, s_ring.count);
    TEST_ASSERT_EQUAL_UINT64(open + 1U, frame_at(0)->view.counters.frame_sequence);
}

static void test_reset_turns_recording_off(void) {
    record(true);
    frame(false);
    nt_devapi_run_reset_hooks();
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result_of(root), "recording")));
    cJSON_Delete(root);
}

/* ---- errors ---- */

static void test_not_ready_and_unknown_frames(void) {
    expect_error("{\"method\":\"gfx.read\"}", "unknown_frame");
    record(true);
    expect_error("{\"method\":\"gfx.read\"}", "frame_not_ready");
    frame(false);
    frame(false);
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"gfx.read\",\"params\":{\"sequence\":%u}}", (unsigned)g_nt_gfx.counters.frame_sequence + 5U);
    expect_error(line, "frame_not_ready");
    expect_error("{\"method\":\"gfx.read\",\"params\":{\"sequence\":1}}", "unknown_frame");
    (void)snprintf(line, sizeof(line), "{\"method\":\"gfx.read\",\"params\":{\"sim_frame\":%u}}", g_nt_app.frame + 1U);
    expect_error(line, "frame_not_ready");
    record(false);
    expect_error(line, "unknown_frame");
    expect_error("{\"method\":\"gfx.read\",\"params\":{\"sequence\":1,\"sim_frame\":1}}", "bad_params");
    expect_error("{\"method\":\"gfx.read\",\"params\":{\"sequence\":-1}}", "bad_params");
    expect_error("{\"method\":\"gfx.record\",\"params\":{\"enabled\":1}}", "bad_params");
}

/* ---- lifecycle and overflow ---- */

static void test_reinit_zeroes_the_group(void) {
    record(true);
    frame(false);
    frame(false);
    tearDown();
    setUp();
    TEST_ASSERT_FALSE(s_ring.recording);
    TEST_ASSERT_EQUAL_UINT32(0, s_ring.count);
    record(true);
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(1, s_ring.count);
}

static int s_overflow_warnings;

static void count_overflow_warnings(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)domain;
    (void)user;
    s_overflow_warnings += level == NT_LOG_LEVEL_WARN && strstr(msg, "overflowed") != NULL;
}

static void test_an_overflowed_frame_is_stored_with_one_warning(void) {
    nt_gfx_shutdown();
    init_gfx(1);
    nt_log_add_sink(count_overflow_warnings, NULL);
    record(true);
    frame(false);
    frame(false);
    frame(false);
    nt_log_remove_sink(count_overflow_warnings, NULL);
    TEST_ASSERT_EQUAL_UINT32(2, s_ring.count);
    TEST_ASSERT_TRUE(frame_at(0)->view.overflow && frame_at(1)->view.overflow);
    TEST_ASSERT_EQUAL_INT(1, s_overflow_warnings);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sim_frame_read_returns_the_step_frame_not_idle_frames);
    RUN_TEST(test_recording_while_paused_stores_no_advanced_frame);
    RUN_TEST(test_ring_wraps_and_evicts_through_the_newest_overlap);
    RUN_TEST(test_recorded_frames_stay_gapless_over_many_wraps);
    RUN_TEST(test_off_keeps_the_ring_and_stores_no_frame_in_flight);
    RUN_TEST(test_a_frame_open_at_on_is_never_stored);
    RUN_TEST(test_reset_turns_recording_off);
    RUN_TEST(test_not_ready_and_unknown_frames);
    RUN_TEST(test_reinit_zeroes_the_group);
    RUN_TEST(test_an_overflowed_frame_is_stored_with_one_warning);
    return UNITY_END();
}
