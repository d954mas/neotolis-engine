/* L2 gfx group via submit() over nt_gfx_fake: the recording mode, the ring of recent frames, reads by
   sequence, and the frame_not_ready / unknown_frame split. The TU includes the group source with a small
   ring so eviction is reachable. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef NT_DEVAPI_GFX_RING_RECORDS
#undef NT_DEVAPI_GFX_RING_FRAMES
#define NT_DEVAPI_GFX_RING_RECORDS 64U
#define NT_DEVAPI_GFX_RING_FRAMES 4U

/* clang-format off */
#include "app/nt_app.h"
// NOLINTNEXTLINE(bugprone-suspicious-include): drive the static ring with crafted frames.
#include "devapi/nt_devapi_gfx.c"
#include "devapi/nt_devapi_internal.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "test_helpers/nt_assert_trap.h"
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

/* One host frame in devapi_host order; advance = the simulation stepped this frame, marker = its game
   issues one marker. Commands submitted after it run between frames, before the next hook. */
static void frame_with(bool advance, const char *marker) {
    if (advance) {
        g_nt_app.frame++;
    }
    nt_devapi_update();
    nt_gfx_begin_frame();
    if (marker != NULL) {
        nt_gfx_marker_push(marker);
        nt_gfx_marker_pop();
    }
    nt_gfx_end_frame();
}

static void frame(bool advance) { frame_with(advance, NULL); }

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

/* Sequence of the read frame, or 0 when the read failed; *marked when its text holds the step marker. */
static uint64_t read_marked(const char *line, bool *marked) {
    cJSON *root = submit(line);
    uint64_t sequence = 0;
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok"))) {
        const cJSON *result = result_of(root);
        const char *text = cJSON_GetObjectItemCaseSensitive(result, "text")->valuestring;
        TEST_ASSERT_EQUAL_STRING_LEN("ntcapture 1\n", text, 12);
        *marked = strstr(text, "MARKER_PUSH label=\"step\"") != NULL;
        sequence = (uint64_t)number(result, "sequence");
    }
    cJSON_Delete(root);
    return sequence;
}

static uint64_t read_sequence(const char *line) {
    bool marked = false;
    return read_marked(line, &marked);
}

static uint64_t read_by_sequence(uint64_t sequence, bool *marked) {
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"gfx.read\",\"params\":{\"sequence\":%u}}", (unsigned)sequence);
    return read_marked(line, marked);
}

static uint64_t last_stored(void) { return frame_at(s_ring.count - 1U)->view.counters.frame_sequence; }

/* ---- the frame of a step ---- */

/* Only the step frame issues the marker: the one advanced frame holds it, the idle frames around it do not. */
static void test_the_step_frame_is_the_one_advanced_frame(void) {
    const uint64_t first = g_nt_gfx.counters.frame_sequence + 1U;
    record(true);
    frame(false);
    frame(false);
    frame_with(true, "step");
    const uint32_t sim = g_nt_app.frame;
    frame(false);
    frame(false); /* stores the idle frame after the step */
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    const cJSON *frames = cJSON_GetObjectItemCaseSensitive(result_of(root), "frames");
    TEST_ASSERT_EQUAL_INT(4, cJSON_GetArraySize(frames));
    TEST_ASSERT_EQUAL_UINT64(first, (uint64_t)number(cJSON_GetArrayItem(frames, 0), "sequence"));
    TEST_ASSERT_EQUAL_UINT64(first + 3U, (uint64_t)number(cJSON_GetArrayItem(frames, 3), "sequence"));
    uint64_t step = 0;
    int advanced = 0;
    const cJSON *f = NULL;
    cJSON_ArrayForEach(f, frames) {
        bool marked = false;
        const uint64_t sequence = (uint64_t)number(f, "sequence");
        TEST_ASSERT_EQUAL_UINT64(sequence, read_by_sequence(sequence, &marked));
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f, "advanced"))) {
            advanced++;
            step = sequence;
            TEST_ASSERT_EQUAL_UINT32(sim, (uint32_t)number(f, "sim_frame"));
        }
        TEST_ASSERT_EQUAL(sequence == step, marked);
    }
    cJSON_Delete(root);
    TEST_ASSERT_EQUAL_INT(1, advanced);
    TEST_ASSERT_EQUAL_UINT64(first + 2U, step);
    TEST_ASSERT_EQUAL_UINT64(first + 3U, read_sequence("{\"method\":\"gfx.read\"}"));
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
}

/* ---- the ring ---- */

static nt_gfx_event_t s_source[NT_DEVAPI_GFX_RING_RECORDS];

/* Record i of frame `sequence` carries object sequence * 100 + i, so a wrong copy shows. */
static void store_crafted(uint64_t sequence, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        s_source[i].object = ((uint32_t)sequence * 100U) + i;
    }
    nt_gfx_capture_view_t view = {.events = s_source, .count = count};
    view.counters.frame_sequence = sequence;
    store_frame(&view, 0, false);
}

static uint32_t first_record(const stored_frame *f) { return (uint32_t)(f->view.events - s_records); }

static void expect_ring(const uint64_t *sequences, uint32_t count) {
    TEST_ASSERT_EQUAL_UINT32(count, s_ring.count);
    for (uint32_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_UINT64(sequences[i], frame_at(i)->view.counters.frame_sequence);
        TEST_ASSERT_TRUE(first_record(frame_at(i)) + frame_at(i)->view.count <= NT_DEVAPI_GFX_RING_RECORDS);
        for (uint32_t r = 0; r < frame_at(i)->view.count; r++) {
            TEST_ASSERT_EQUAL_UINT32(((uint32_t)sequences[i] * 100U) + r, frame_at(i)->view.events[r].object);
        }
    }
}

static void test_ring_wraps_and_evicts_through_the_newest_overlap(void) {
    store_crafted(1, 40);
    store_crafted(2, 18); /* [40,58) */
    store_crafted(3, 24); /* wraps to [0,24), evicts 1 */
    store_crafted(4, 16); /* [24,40) */
    expect_ring((const uint64_t[]){2, 3, 4}, 3);
    TEST_ASSERT_EQUAL_UINT32(40, first_record(frame_at(0)));
    /* 30 records wrap to 0 and overlap 3 and 4, so 2, 3 and 4 all leave. */
    store_crafted(5, 30);
    expect_ring((const uint64_t[]){5}, 1);
    TEST_ASSERT_EQUAL_UINT32(0, first_record(frame_at(0)));
    /* Exact fit at the end, then the frame-count limit. */
    store_crafted(6, 34);
    expect_ring((const uint64_t[]){5, 6}, 2);
    for (uint64_t seq = 7; seq <= 9; seq++) {
        store_crafted(seq, 1); /* wraps to 0 and evicts 5, then fills up to the frame limit */
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

/* Off between frames, as a client does after it sees a bug: the frame just ended is kept, none after it,
   and the list waits until it is stored. */
static void test_off_between_frames_stores_the_frame_just_ended(void) {
    record(true);
    frame(false);
    frame(false);
    const uint64_t ended = g_nt_gfx.counters.frame_sequence;
    record(false);
    expect_error("{\"method\":\"gfx.frames\"}", "frame_not_ready");
    expect_error("{\"method\":\"gfx.read\"}", "frame_not_ready");
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(2, s_ring.count);
    TEST_ASSERT_EQUAL_UINT64(ended, last_stored());
    TEST_ASSERT_EQUAL_UINT64(ended, read_sequence("{\"method\":\"gfx.read\"}"));
}

/* Off from game code inside a recorded frame: that frame is stored, none after it. */
static void test_off_from_game_code_stores_the_current_frame(void) {
    record(true);
    frame(false);
    nt_devapi_update();
    nt_gfx_begin_frame();
    const uint64_t current = g_nt_gfx.counters.frame_sequence;
    nt_devapi_gfx_record(false);
    nt_gfx_end_frame();
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(2, s_ring.count);
    TEST_ASSERT_EQUAL_UINT64(current, last_stored());
}

/* After off the list waits for the frame in flight, so every listed frame stays readable. */
static void test_frames_after_off_waits_for_the_frame_in_flight(void) {
    record(true);
    for (int i = 0; i < 6; i++) {
        frame(false); /* fills the 4-frame ring */
    }
    record(false);
    expect_error("{\"method\":\"gfx.frames\"}", "frame_not_ready");
    frame(false); /* stores the frame in flight, evicting the oldest */
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    const cJSON *f = NULL;
    cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(result_of(root), "frames")) {
        bool marked = false;
        TEST_ASSERT_EQUAL_UINT64((uint64_t)number(f, "sequence"), read_by_sequence((uint64_t)number(f, "sequence"), &marked));
    }
    cJSON_Delete(root);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(NT_DEVAPI_GFX_RING_FRAMES, s_ring.count);
}

/* Off, on, off in one batch between frames: nothing stays in flight, the list answers after one update. */
static void test_off_on_off_in_one_batch_settles(void) {
    record(true);
    frame(false);
    frame(false);
    record(false);
    record(true);
    record(false);
    frame(false);
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(result_of(root), "frames")));
    cJSON_Delete(root);
}

/* On records from the next frame that begins after a hook; off then on drops the old session's frame in flight. */
static void test_on_records_from_the_next_frame(void) {
    uint64_t next = g_nt_gfx.counters.frame_sequence + 1U;
    record(true);
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT64(next, frame_at(0)->view.counters.frame_sequence);
    /* On while on keeps the ring. */
    record(true);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(2, s_ring.count);
    /* Off then on clears it and drops the frame in flight. */
    record(false);
    record(true);
    TEST_ASSERT_EQUAL_UINT32(0, s_ring.count);
    next = g_nt_gfx.counters.frame_sequence + 1U;
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(1, s_ring.count);
    TEST_ASSERT_EQUAL_UINT64(next, frame_at(0)->view.counters.frame_sequence);
    /* From game code in frame N, the first stored frame is N + 1. */
    nt_devapi_gfx_record(false);
    frame(false);
    nt_devapi_update();
    nt_gfx_begin_frame();
    next = g_nt_gfx.counters.frame_sequence + 1U;
    nt_devapi_gfx_record(true);
    nt_gfx_end_frame();
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT64(next, frame_at(0)->view.counters.frame_sequence);
}

/* A native command arrives in the transport poll inside the update, before the hook of that update. */
static const char *s_polled_line;

static void poll_line(void) {
    if (s_polled_line != NULL) {
        TEST_ASSERT_NOT_NULL(nt_devapi_submit(s_polled_line));
        s_polled_line = NULL;
    }
}

static void test_commands_from_the_transport_poll_act_before_the_hook(void) {
    nt_devapi_set_transport_poll(poll_line);
    const uint64_t next = g_nt_gfx.counters.frame_sequence + 1U;
    s_polled_line = "{\"method\":\"gfx.record\",\"params\":{\"enabled\":true}}";
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT64(next, frame_at(0)->view.counters.frame_sequence);
    const uint64_t ended = g_nt_gfx.counters.frame_sequence;
    s_polled_line = "{\"method\":\"gfx.record\",\"params\":{\"enabled\":false}}";
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT64(ended, last_stored());
}

/* The host order is the contract: a skipped update asserts. */
static void test_a_skipped_update_asserts(void) {
    record(true);
    frame(false);
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
    NT_TEST_EXPECT_ASSERT(nt_devapi_update());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "once per frame"));
}

/* An update inside the requested frame, after its begin_frame, asserts. */
static void test_an_update_after_begin_frame_asserts(void) {
    record(true);
    nt_devapi_update();
    nt_gfx_begin_frame();
    NT_TEST_EXPECT_ASSERT(nt_devapi_update());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "between frames"));
}

/* Recording is game state: a client leaving does not change it. */
static void test_a_client_reset_keeps_recording(void) {
    record(true);
    frame(false);
    nt_devapi_run_reset_hooks();
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result_of(root), "recording")));
    cJSON_Delete(root);
}

/* ---- errors ---- */

static void test_not_ready_and_unknown_frames(void) {
    expect_error("{\"method\":\"gfx.read\"}", "unknown_frame");
    record(true);
    expect_error("{\"method\":\"gfx.read\"}", "frame_not_ready");
    frame(false);
    expect_error("{\"method\":\"gfx.read\"}", "frame_not_ready"); /* requested, not stored yet */
    frame(false);
    TEST_ASSERT_EQUAL_UINT64(last_stored(), read_sequence("{\"method\":\"gfx.read\"}"));
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"gfx.read\",\"params\":{\"sequence\":%u}}", (unsigned)g_nt_gfx.counters.frame_sequence);
    expect_error(line, "unknown_frame"); /* the frame just ended: stored by the next update */
    expect_error("{\"method\":\"gfx.read\",\"params\":{\"sequence\":1}}", "unknown_frame");
    expect_error("{\"method\":\"gfx.read\",\"params\":{\"sequence\":4294967296}}", "unknown_frame");
    expect_error("{\"method\":\"gfx.read\",\"params\":{\"sequence\":9007199254740992}}", "bad_params");
    expect_error("{\"method\":\"gfx.read\",\"params\":{\"sequence\":1.5}}", "bad_params");
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

/* A command between frames can still issue gfx work into the frame just ended (recorder.start makes
   buffers there); a read before it must not cut that work out of the stored frame. */
static void test_gfx_work_after_a_read_stays_in_the_frame(void) {
    record(true);
    frame(false);
    frame(false);
    const uint64_t ended = g_nt_gfx.counters.frame_sequence;
    cJSON_Delete(submit("{\"method\":\"gfx.frames\"}"));
    const uint8_t data[16] = {0};
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = sizeof(data), .data = data});
    frame(false);
    const stored_frame *f = find_sequence(ended);
    TEST_ASSERT_NOT_NULL(f);
    bool created = false;
    for (uint32_t i = 0; i < f->view.count; i++) {
        created |= f->view.events[i].kind == NT_GFX_EVENT_OP && f->view.events[i].operation == NT_GFX_OP_CREATE && f->view.events[i].object_kind == NT_GFX_OBJECT_BUFFER;
    }
    TEST_ASSERT_TRUE(created);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_the_step_frame_is_the_one_advanced_frame);
    RUN_TEST(test_recording_while_paused_stores_no_advanced_frame);
    RUN_TEST(test_ring_wraps_and_evicts_through_the_newest_overlap);
    RUN_TEST(test_recorded_frames_stay_gapless_over_many_wraps);
    RUN_TEST(test_off_between_frames_stores_the_frame_just_ended);
    RUN_TEST(test_off_from_game_code_stores_the_current_frame);
    RUN_TEST(test_frames_after_off_waits_for_the_frame_in_flight);
    RUN_TEST(test_off_on_off_in_one_batch_settles);
    RUN_TEST(test_on_records_from_the_next_frame);
    RUN_TEST(test_commands_from_the_transport_poll_act_before_the_hook);
    RUN_TEST(test_a_skipped_update_asserts);
    RUN_TEST(test_an_update_after_begin_frame_asserts);
    RUN_TEST(test_gfx_work_after_a_read_stays_in_the_frame);
    RUN_TEST(test_a_client_reset_keeps_recording);
    RUN_TEST(test_not_ready_and_unknown_frames);
    RUN_TEST(test_reinit_zeroes_the_group);
    RUN_TEST(test_an_overflowed_frame_is_stored_with_one_warning);
    return UNITY_END();
}
