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

/* Host start in devapi_host order: gfx init (its work belongs to the first frame), then devapi registers. */
static void start(uint32_t capacity) {
    g_nt_app = (nt_app_t){.max_dt = 0.1F, .scale = 1.0F, .step_dt = 1.0F / 60.0F, .render_enabled = true};
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = capacity;
    nt_gfx_init(&desc);
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_devapi_register_gfx();
}

void setUp(void) { start(256); }

void tearDown(void) {
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
    nt_devapi_shutdown();
}

/* ---- helpers ---- */

/* One host frame in devapi_host order; advance = the simulation stepped this frame, marker = its game
   issues one marker. Commands submitted after it run between frames. A frame is listed two frames after
   it renders: the next begin_frame finishes it, the update after that stores it. */
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

/* A gfx.record command between frames. */
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

typedef enum { AT_BETWEEN, AT_BEFORE_BEGIN, AT_MID_FRAME } switch_at;

#define NONE INT32_MIN

/* One switch row: recording state before, the switches applied in order at one point of frame N, and the
   first and last frame of the ring relative to N once four more frames ran (NONE: empty). */
typedef struct {
    const char *switches; /* '1' on, '0' off */
    switch_at at;
    int32_t first, last;
    bool on_before;
} switch_row;

static void apply(const char *switches, switch_at at) {
    for (const char *c = switches; *c != '\0'; c++) {
        if (at == AT_BETWEEN) {
            record(*c == '1'); /* a command */
        } else {
            nt_gfx_capture_set_continuous(*c == '1'); /* game code */
        }
    }
}

static void run_row(const switch_row *row) {
    if (row->on_before) {
        record(true);
        frame(false);
        frame(false);
    }
    const uint64_t n = g_nt_gfx.counters.frame_sequence + 1U;
    if (row->at == AT_BETWEEN) {
        apply(row->switches, row->at);
    }
    nt_devapi_update();
    if (row->at == AT_BEFORE_BEGIN) {
        apply(row->switches, row->at);
    }
    nt_gfx_begin_frame();
    if (row->at == AT_MID_FRAME) {
        apply(row->switches, row->at);
    }
    nt_gfx_end_frame();
    for (int i = 0; i < 4; i++) {
        frame(false);
    }
    if (row->last == NONE) {
        TEST_ASSERT_EQUAL_UINT32(0, s_ring.count);
        return;
    }
    TEST_ASSERT_EQUAL_UINT64(n + (uint64_t)(int64_t)row->last, last_stored());
    TEST_ASSERT_EQUAL_UINT64(n + (uint64_t)(int64_t)row->first, frame_at(0)->view.counters.frame_sequence);
    if (!g_nt_gfx.capture_continuous) { /* nothing after the last stored frame */
        frame(false);
        frame(false);
        TEST_ASSERT_EQUAL_UINT64(n + (uint64_t)(int64_t)row->last, last_stored());
    }
}

/* On records from the next begin_frame; off still stores the frame recording at the call; off then on
   keeps the ring. Every row's frames are consecutive; the 4-frame ring keeps the newest. */
static void test_switch_rows(void) {
    /* clang-format off */
    static const switch_row rows[] = {
        {"1", AT_BETWEEN, 0, 2, false},
        {"1", AT_MID_FRAME, -1, 2, true},
        {"1", AT_BEFORE_BEGIN, 0, 2, false},
        {"1", AT_MID_FRAME, 1, 2, false},
        {"0", AT_BETWEEN, -2, -1, true},
        {"0", AT_BEFORE_BEGIN, -2, -1, true},
        {"0", AT_MID_FRAME, -2, 0, true},
        {"01", AT_BETWEEN, -1, 2, true},
        {"01", AT_MID_FRAME, -1, 2, true},
        {"010", AT_BETWEEN, -2, -1, true},
        {"010", AT_MID_FRAME, -2, 0, true},
        {"10", AT_BETWEEN, NONE, NONE, false},
    };
    /* clang-format on */
    for (size_t i = 0; i < NT_COUNTOF(rows); i++) {
        tearDown();
        setUp();
        run_row(&rows[i]);
    }
}

/* Off between frames, as a client does after it sees a bug: the list waits until the frame just ended is
   stored, two updates later. */
static void test_off_lists_after_the_frame_just_ended_is_stored(void) {
    record(true);
    frame(false);
    frame(false);
    const uint64_t ended = g_nt_gfx.counters.frame_sequence;
    record(false);
    expect_error("{\"method\":\"gfx.frames\"}", "frame_not_ready");
    expect_error("{\"method\":\"gfx.read\"}", "frame_not_ready");
    frame(false);
    expect_error("{\"method\":\"gfx.read\"}", "frame_not_ready"); /* the frame before it was stored */
    frame(false);
    TEST_ASSERT_EQUAL_UINT64(ended, read_sequence("{\"method\":\"gfx.read\"}"));
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    TEST_ASSERT_NOT_NULL(result_of(root)); /* the list answers again */
    cJSON_Delete(root);
}

/* After off the list waits for the frame in flight, so every listed frame stays readable. */
static void test_frames_after_off_waits_for_the_frame_in_flight(void) {
    record(true);
    for (int i = 0; i < 6; i++) {
        frame(false); /* fills the 4-frame ring */
    }
    record(false);
    expect_error("{\"method\":\"gfx.frames\"}", "frame_not_ready");
    frame(false);
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

/* Recording is game state: a client leaving does not change it. */
static void test_a_client_reset_keeps_recording(void) {
    record(true);
    frame(false);
    nt_devapi_run_reset_hooks();
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result_of(root), "recording")));
    cJSON_Delete(root);
}

/* Game code turns recording on before a step: sim frames and advance per frame, marker in the step frame only. */
static void test_the_step_frame_from_game_code(void) {
    g_nt_app.frame = 7; /* a sim that ran before recording */
    frame(false);
    const uint64_t first = g_nt_gfx.counters.frame_sequence + 1U;
    nt_gfx_capture_set_continuous(true);
    frame(false);
    frame_with(true, "step");
    frame(false);
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(3, s_ring.count);
    for (uint32_t i = 0; i < 3; i++) {
        const stored_frame *f = frame_at(i);
        bool marked = false;
        TEST_ASSERT_EQUAL_UINT64(first + i, read_by_sequence(first + i, &marked));
        TEST_ASSERT_EQUAL(i == 1, f->advanced);
        TEST_ASSERT_EQUAL(i == 1, marked);
        TEST_ASSERT_EQUAL_UINT32(i == 0 ? 7U : 8U, f->sim_frame);
    }
}

/* Recording on in the first update after registration: the first frame's advance is known. */
static void test_the_first_frame_after_registration_knows_its_advance(void) {
    g_nt_app.frame++;
    nt_devapi_update();
    record(true);
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
    frame(true);
    frame(false);
    frame(false);
    TEST_ASSERT_EQUAL_UINT32(2, s_ring.count);
    TEST_ASSERT_TRUE(frame_at(0)->advanced);
    TEST_ASSERT_TRUE(frame_at(1)->advanced);
}

/* A frame recorded by other code (a one-shot request) is stored like any recorded frame, and the game
   switches recording with gfx's continuous capture; frames not recorded leave a gap in sequences. */
static void test_frames_recorded_by_other_code_are_stored(void) {
    const uint64_t first = g_nt_gfx.counters.frame_sequence + 1U;
    nt_gfx_capture_request();
    frame(false);
    frame(false);
    frame(false);
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result_of(root), "recording")));
    cJSON_Delete(root);
    nt_gfx_capture_set_continuous(true);
    for (int i = 0; i < 4; i++) {
        frame(false);
    }
    const uint64_t expected[] = {first, first + 3U, first + 4U};
    TEST_ASSERT_EQUAL_UINT32(3, s_ring.count);
    for (uint32_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT64(expected[i], frame_at(i)->view.counters.frame_sequence);
    }
    root = submit("{\"method\":\"gfx.frames\"}");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result_of(root), "recording")));
    cJSON_Delete(root);
}

/* Two updates in one host frame store each frame once. */
static void test_a_doubled_update_stores_each_frame_once(void) {
    record(true);
    for (int i = 0; i < 6; i++) {
        nt_devapi_update();
        frame(i == 2);
    }
    TEST_ASSERT_EQUAL_UINT32(4, s_ring.count);
    for (uint32_t i = 1; i < s_ring.count; i++) {
        TEST_ASSERT_EQUAL_UINT64(frame_at(i - 1U)->view.counters.frame_sequence + 1U, frame_at(i)->view.counters.frame_sequence);
        TEST_ASSERT_EQUAL(frame_at(i)->sim_frame != frame_at(i - 1U)->sim_frame, frame_at(i)->advanced);
    }
}

/* ---- errors ---- */

static void test_not_ready_and_unknown_frames(void) {
    expect_error("{\"method\":\"gfx.read\"}", "unknown_frame");
    record(true);
    expect_error("{\"method\":\"gfx.read\"}", "frame_not_ready");
    frame(false);
    frame(false);
    expect_error("{\"method\":\"gfx.read\"}", "frame_not_ready"); /* the first frame is finished, stored by the next update */
    frame(false);
    TEST_ASSERT_EQUAL_UINT64(last_stored(), read_sequence("{\"method\":\"gfx.read\"}"));
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"gfx.read\",\"params\":{\"sequence\":%u}}", (unsigned)g_nt_gfx.counters.frame_sequence);
    expect_error(line, "unknown_frame"); /* the frame just ended: stored two updates later */
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
    TEST_ASSERT_FALSE(g_nt_gfx.capture_continuous);
    TEST_ASSERT_EQUAL_UINT32(0, s_ring.count);
    record(true);
    frame(false);
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
    tearDown();
    start(1);
    nt_log_add_sink(count_overflow_warnings, NULL);
    record(true);
    frame(false);
    frame(false);
    frame(false);
    frame(false);
    nt_log_remove_sink(count_overflow_warnings, NULL);
    TEST_ASSERT_EQUAL_UINT32(2, s_ring.count);
    TEST_ASSERT_TRUE(frame_at(0)->view.overflow && frame_at(1)->view.overflow);
    TEST_ASSERT_EQUAL_INT(NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_WARN ? 1 : 0, s_overflow_warnings);
    cJSON *root = submit("{\"method\":\"gfx.frames\"}");
    const cJSON *entry = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(result_of(root), "frames"), 0);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "overflow")));
    TEST_ASSERT_EQUAL_UINT32(frame_at(0)->view.count, (uint32_t)cJSON_GetObjectItemCaseSensitive(entry, "records")->valuedouble);
    TEST_ASSERT_EQUAL_UINT32(frame_at(0)->sim_frame, (uint32_t)cJSON_GetObjectItemCaseSensitive(entry, "sim_frame")->valuedouble);
    cJSON_Delete(root);
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
    RUN_TEST(test_switch_rows);
    RUN_TEST(test_off_lists_after_the_frame_just_ended_is_stored);
    RUN_TEST(test_frames_after_off_waits_for_the_frame_in_flight);
    RUN_TEST(test_the_step_frame_from_game_code);
    RUN_TEST(test_the_first_frame_after_registration_knows_its_advance);
    RUN_TEST(test_frames_recorded_by_other_code_are_stored);
    RUN_TEST(test_a_doubled_update_stores_each_frame_once);
    RUN_TEST(test_gfx_work_after_a_read_stays_in_the_frame);
    RUN_TEST(test_a_client_reset_keeps_recording);
    RUN_TEST(test_not_ready_and_unknown_frames);
    RUN_TEST(test_reinit_zeroes_the_group);
    RUN_TEST(test_an_overflowed_frame_is_stored_with_one_warning);
    return UNITY_END();
}
