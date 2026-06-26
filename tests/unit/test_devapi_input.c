/* devapi input.* group via submit() (no socket). Scheduling: write g_nt_app.frame, call
   nt_devapi_update() (releases due entries only on a real sim-advance), then nt_input_poll(). */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "app/nt_app.h"
#include "devapi/nt_devapi_internal.h"
#include "devapi/nt_devapi_net.h" /* nt_devapi_update — ticks the schedule on a sim-advance */
#include "input/nt_input.h"
#include "input/nt_input_internal.h" /* inject API: fill the buffer to probe whole-or-nothing */
#include "window/nt_window.h"        /* g_nt_window.fb_height — the basis for the input.* Y-up flip */
#include "unity.h"
/* clang-format on */

/* Fixed framebuffer height so the input.* Y-up -> device flip is deterministic in the harness
   (the stub window never sizes itself). Device Y = TEST_FB_HEIGHT - Y-up. */
#define TEST_FB_HEIGHT 600U

/* The one input.* flip the handlers apply: a Y-up input coord lands at this device Y in the slot. */
#define EXPECT_DEVICE_Y(y_up) ((float)TEST_FB_HEIGHT - (float)(y_up))

void setUp(void) {
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_devapi_register_input();             /* re-registers input -> resets the schedule */
    nt_devapi_register_discovery();         /* the test verifies input.* via endpoints / command.describe */
    nt_input_init();                        /* clean immediate buffer + key/pointer state */
    g_nt_window.fb_height = TEST_FB_HEIGHT; /* deterministic basis for the Y-up flip */
}

void tearDown(void) { nt_devapi_shutdown(); }

/* ---- frame driving ---- */

/* One real sim-advance: bump the frame, run devapi update (which sees the advance and releases due
   schedule entries into nt_input's immediate buffer), then poll to apply that buffer. */
static void advance(void) {
    g_nt_app.frame++;
    nt_devapi_update();
    nt_input_poll();
}

/* A frozen tick (pause / manual-idle): frame UNCHANGED, so nt_devapi_update releases nothing. poll
   still runs (real device input would still flow), but no scheduled inject is released. */
static void tick_no_advance(void) {
    nt_devapi_update();
    nt_input_poll();
}

/* ---- helpers (clone of test_devapi_time.c cadence) ---- */

/* Parse `resp`, assert ok:false + error.code == "bad_params", free the tree. */
static void assert_bad_params(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(err, "code")->valuestring);
    cJSON_Delete(root);
}

/* Parse `resp`, assert ok:true, return the (borrowed) root — caller deletes. */
static cJSON *parse_ok(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    return root;
}

static bool endpoints_has_method_with_shapes(const char *method) {
    const char *resp = nt_devapi_submit("{\"method\":\"endpoints\",\"params\":{\"detail\":true}}");
    cJSON *root = parse_ok(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *commands = cJSON_GetObjectItemCaseSensitive(result, "commands");
    bool found = false;
    cJSON *cmd = NULL;
    cJSON_ArrayForEach(cmd, commands) {
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(cmd, "method");
        if (cJSON_IsString(m) && strcmp(m->valuestring, method) == 0) {
            const cJSON *ps = cJSON_GetObjectItemCaseSensitive(cmd, "params_shape");
            const cJSON *rs = cJSON_GetObjectItemCaseSensitive(cmd, "result_shape");
            found = cJSON_IsString(ps) && strlen(ps->valuestring) > 0 && cJSON_IsString(rs) && strlen(rs->valuestring) > 0;
            break;
        }
    }
    cJSON_Delete(root);
    return found;
}

/* ---- discovery ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_input_group_registers(void) {
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.key"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.pointer"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.move"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.click"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.wheel"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.gesture"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.button"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.set_player_enabled"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.text"));
}

/* ---- forwarding (ok envelopes; fire-and-forget, never deferred) ---- */

static void test_input_key_ok(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\"}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "ok")));
    cJSON_Delete(root);
}

static void test_input_key_hold_ok(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"SPACE\",\"hold\":3}}"));
    cJSON_Delete(root);
}

static void test_input_pointer_ok(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"down\",\"id\":0,\"x\":10,\"y\":20}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
}

static void test_input_click_queues_two(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":5,\"y\":6}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
}

static void test_input_gesture_queues_ordered(void) {
    /* down@0 carries point[0]; a move per SUBSEQUENT point (no redundant move@0 for point[0]) + up.
       2 points -> down + 1 move + up = 3 entries. */
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":1,\"points\":[[0,0],[5,5]]}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
}

static void test_input_set_player_enabled_ok(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.set_player_enabled\",\"params\":{\"enabled\":false}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result, "enabled")));
    cJSON_Delete(root);
    /* restore so a leaked gate state can't affect later tests in the same process */
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.set_player_enabled\",\"params\":{\"enabled\":true}}")));
}

/* ---- input.text ---- */

static void test_input_text_ok(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"hi\"}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
}

/* A 2-byte UTF-8 sequence (U+00E9 é) decodes to ONE codepoint. */
static void test_input_text_utf8_multibyte(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\\u00e9\"}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
}

/* ---- bad_params (fail-fast, never assert) ---- */

static void test_input_key_unknown_name_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"NOPE\"}}")); }

static void test_input_key_wrong_type_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":123}}")); }

static void test_input_pointer_bad_action_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"xyz\",\"id\":0,\"x\":1,\"y\":2}}")); }

static void test_input_pointer_bad_type_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"move\",\"id\":0,\"x\":1,\"y\":2,\"type\":\"finger\"}}"));
}

static void test_input_text_bad_type_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":42}}")); }

static void test_input_set_player_enabled_bad_type_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.set_player_enabled\",\"params\":{\"enabled\":1}}")); }

/* ---- out-of-domain numeric bot input -> bad_params (never silently coerced) ---- */

static void test_input_pointer_negative_id_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"down\",\"id\":-1,\"x\":1,\"y\":2}}")); }

static void test_input_click_negative_id_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":1,\"y\":2,\"id\":-5}}")); }

static void test_input_pointer_buttons_out_of_range_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"down\",\"id\":0,\"x\":1,\"y\":2,\"buttons\":8}}"));
}

static void test_input_click_button_out_of_range_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":1,\"y\":2,\"button\":8}}")); }

static void test_input_button_out_of_range_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":256}}")); }

/* ---- multi-event sugar is whole-or-nothing on a near-full SCHEDULE ---- */

/* Each input.key (no hold) stages exactly 1 schedule entry; we never advance(), so none drain.
   Leaves the schedule with `free` slots to probe whole-or-nothing reservation. */
static void fill_schedule_leaving(uint32_t free) {
    for (uint32_t i = 0; i < NT_DEVAPI_INPUT_SCHED_MAX - free; i++) {
        cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\"}}")));
    }
}

/* A click needs 2 entries; with only 1 free it must reject WHOLE and enqueue nothing (no orphan DOWN).
   Proof of "nothing enqueued": after the reject, exactly 1 free slot must remain — one 1-entry key
   then succeeds and a second overflows. */
static void test_input_click_near_full_atomic(void) {
    fill_schedule_leaving(1U);
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":5,\"y\":6}}"));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"B\"}}"))); /* the 1 free slot survived */
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"C\"}}"));      /* now full -> proves click wrote nothing */
}

/* A 2-point gesture needs 3 entries (down + 1 move + up); with only 2 free it rejects whole. */
static void test_input_gesture_near_full_atomic(void) {
    fill_schedule_leaving(2U);
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":1,\"points\":[[0,0],[5,5]]}}"));
    /* 2 free slots must remain intact: two 1-entry keys succeed, the third overflows. */
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"B\"}}")));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"C\"}}")));
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"D\"}}"));
}

/* ---- input.text malformed-UTF-8 reject paths ---- */

/* Raw 0xFF byte (NOT a \u escape, which would produce valid UTF-8) -> malformed lead byte. cJSON
   copies high bytes between quotes verbatim, so the handler's decoder is what rejects it. */
static void test_input_text_bad_lead_byte_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xff\"}}")); }

/* A 2-byte lead (0xC0) with no continuation byte -> truncated sequence -> bad_params. */
static void test_input_text_truncated_continuation_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xc0\"}}")); }

/* ---- structurally-valid-but-invalid UTF-8 scalars -> bad_params ---- */

/* Overlong: 0xC0 0x80 is a 2-byte form of U+0000 (below the 2-byte minimum 0x80). */
static void test_input_text_overlong_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xc0\x80\"}}")); }

/* UTF-16 surrogate: 0xED 0xA0 0x80 decodes to U+D800 (illegal scalar in UTF-8). */
static void test_input_text_surrogate_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xed\xa0\x80\"}}")); }

/* Out-of-range: 0xF4 0x90 0x80 0x80 decodes to U+110000 (> U+10FFFF). */
static void test_input_text_above_max_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xf4\x90\x80\x80\"}}")); }

/* ---- empty text is valid -> queued:0 (no-op), NOT bad_params ---- */

static void test_input_text_empty_queued_zero(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\"}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
}

/* ---- > 32 codepoints can never land whole in the 32-slot char ring -> bad_params ---- */

/* Build an input.text request of `count` ASCII 'a's so the boundary derives from the real cap. */
static char *make_text_request(uint32_t count) {
    static const char k_prefix[] = "{\"method\":\"input.text\",\"params\":{\"text\":\"";
    static const char k_suffix[] = "\"}}";
    size_t prefix_len = sizeof(k_prefix) - 1;
    size_t suffix_len = sizeof(k_suffix) - 1;
    char *buf = malloc(prefix_len + count + suffix_len + 1);
    TEST_ASSERT_NOT_NULL(buf);
    memcpy(buf, k_prefix, prefix_len);
    memset(buf + prefix_len, 'a', count);
    memcpy(buf + prefix_len + count, k_suffix, suffix_len + 1);
    return buf;
}

/* One over NT_INPUT_CHAR_RING: can never land whole in the ring. queued must never overstate. */
static void test_input_text_exceeds_char_ring_bad_params(void) {
    char *req = make_text_request(NT_INPUT_CHAR_RING + 1U);
    assert_bad_params(nt_devapi_submit(req));
    free(req);
}

/* Exactly NT_INPUT_CHAR_RING codepoints is the boundary -> still accepted (queued:RING). */
static void test_input_text_at_char_ring_ok(void) {
    char *req = make_text_request(NT_INPUT_CHAR_RING);
    cJSON *root = parse_ok(nt_devapi_submit(req));
    free(req);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(NT_INPUT_CHAR_RING, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
}

/* ---- hold / frame_stride strict integral+sign parse (not silent valueint truncation) ---- */

static void test_input_key_hold_fractional_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\",\"hold\":2.9}}")); }

static void test_input_key_hold_negative_fractional_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\",\"hold\":-0.5}}")); }

static void test_input_gesture_frame_stride_fractional_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":1,\"points\":[[0,0],[5,5]],\"frame_stride\":1.5}}"));
}

/* A fractional button mask must NOT silently truncate to an int (symmetric with hold/id). */
static void test_input_button_fractional_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":2.5}}")); }

static void test_input_click_button_fractional_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":1,\"y\":2,\"button\":1.9}}")); }

static void test_input_pointer_buttons_fractional_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"down\",\"id\":0,\"x\":1,\"y\":2,\"buttons\":2.5}}"));
}

/* ---- non-finite coordinate / wheel-delta bot input -> bad_params (never stored as inf/nan) ---- */

/* 1e309 overflows strtod to +inf as a double -> rejected before narrowing. JSON has no NaN literal
   and cJSON's number scanner only consumes [0-9eE+-.] (a bare `nan` token fails the whole parse, not
   a number we could store), so the second non-finite vector is a FINITE double past FLT_MAX (1e300):
   isfinite(double) passes but isfinite((float)v) fails -> the float-narrow guard rejects it. */
static void test_input_move_inf_x_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.move\",\"params\":{\"x\":1e309,\"y\":0}}")); }

static void test_input_move_overfloat_y_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.move\",\"params\":{\"x\":0,\"y\":1e300}}")); }

static void test_input_click_inf_x_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":1e309,\"y\":0}}")); }

static void test_input_pointer_inf_y_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"down\",\"id\":0,\"x\":0,\"y\":1e309}}")); }

static void test_input_pointer_overfloat_x_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"move\",\"id\":0,\"x\":1e300,\"y\":0}}")); }

static void test_input_gesture_inf_point_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":1,\"points\":[[0,0],[1e309,5]]}}")); }

static void test_input_wheel_inf_dx_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.wheel\",\"params\":{\"dx\":1e309}}")); }

static void test_input_wheel_overfloat_dy_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.wheel\",\"params\":{\"dy\":1e300}}")); }

/* ---- scheduling: offset-0 applies on the next advancing update ---- */

/* input.key down@0 -> not visible until a sim-advance ticks the schedule + polls. */
static void test_sched_offset0_applies_on_advance(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\"}}"))); /* down@0 staged in schedule */
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));                                                 /* not yet — schedule untouched (the drain-race) */
    advance();
    TEST_ASSERT_TRUE(nt_input_key_is_pressed(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
}

/* A frozen tick releases NOTHING: an enqueued down@0 stays unscheduled while the frame is unchanged
   (pause / manual-idle), then lands on the next real advance. This is the new pause-freeze semantic
   (replaces the old frames_remaining-on-pause freeze inside nt_input).
   Order-independent: nt_devapi_input_reset re-seeds the advance clock against the current
   g_nt_app.frame, so the first tick_no_advance below can never see a spurious forced-advance no
   matter which test ran first (a prior advancing test left s_last_frame elsewhere). */
static void test_sched_pause_freeze(void) {
    nt_devapi_input_reset(); /* seed the clock to the current frame so a frozen tick stays frozen. */
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\"}}")));
    tick_no_advance(); /* frame frozen -> schedule releases nothing */
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
    tick_no_advance(); /* still frozen */
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
    advance(); /* real advance -> down@0 releases + applies */
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
}

/* Reset drops ONLY the devapi-owned schedule (a pending UP never resurfaces); already-APPLIED input
   is game-owned and stays held — the engine must not release it on a client drop. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_reset_drops_schedule_keeps_applied_input(void) {
    /* down@0 + up@100: the DOWN applies on the first advance, the UP is far in the future. */
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\",\"hold\":100}}")));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":4,\"y\":5,\"hold\":100}}")));
    advance(); /* DOWNs apply; key A held, left button held, UPs still scheduled */
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));

    nt_devapi_input_reset(); /* simulate client disconnect: drop the schedule, KEEP applied input */
    nt_input_poll();
    /* Applied input is game-owned -> still held after the reset (NOT released by the engine). */
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));

    /* The scheduled UP WAS dropped (devapi-owned) -> it must not resurface on later advances; the
       key/button stay held because nothing releases them (the engine no longer does). */
    advance();
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* input.key{hold:3} = down@0 + up@3: held across exactly 3 advances, released on the 4th. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_sched_hold_releases_after_n_advances(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\",\"hold\":3}}")));
    advance(); /* down@0 applies; up frames_remaining 3->2 */
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    advance(); /* up 2->1 */
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    advance(); /* up 1->0 */
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    advance(); /* up==0 releases */
    TEST_ASSERT_FALSE(nt_input_key_is_down(NT_KEY_A));
    TEST_ASSERT_TRUE(nt_input_key_is_released(NT_KEY_A));
}

/* input.click default hold=1: DOWN frame N (pressed+down), UP frame N+1 (released, not down). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_sched_click_default_hold(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":5,\"y\":6}}")));
    advance(); /* down@0 applies; up frames_remaining 1->0 (not released yet) */
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    advance(); /* up==0 releases */
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* gesture ordering across frames: down@0, move@1, up@2 (2 points, stride 1 -> last_offset=1; up@1).
   3 points keep the offsets clean: down@0 + move@1 + move@2 + up@2. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_sched_gesture_ordered_across_frames(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":3,\"type\":\"touch\",\"points\":[[0,0],[10,0],[20,0]]}}")));
    nt_pointer_t *slot = NULL;
    advance(); /* down@0 lands at (0,0) */
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == 3U) {
            slot = &g_nt_input.pointers[i];
        }
    }
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(slot->x >= -0.001F && slot->x <= 0.001F);
    advance(); /* move@1 -> x=10 */
    TEST_ASSERT_TRUE(slot->x >= 9.999F && slot->x <= 10.001F);
    advance(); /* move@2 -> x=20; up@2 also due this tick (last_offset=2) -> release */
    TEST_ASSERT_TRUE(slot->x >= 19.999F && slot->x <= 20.001F);
    TEST_ASSERT_TRUE(slot->buttons[NT_BUTTON_LEFT].is_released);
}

/* input.move on the default mouse slot applies its position on the next advancing tick (by value,
   not just queued count) — exercises sched_release_one's NT_INJECT_POINTER_MOVE branch.
   The input.* {x,y} is Y-up: y=34 must land at device Y = fb_height-34, locking the one flip. */
static void test_sched_move_applies_on_advance(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.move\",\"params\":{\"x\":12,\"y\":34}}")));
    advance();
    nt_pointer_t *slot = NULL;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == NT_INPUT_INJECT_POINTER_ID_BASE) {
            slot = &g_nt_input.pointers[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(slot->x >= 11.999F && slot->x <= 12.001F);
    float ey = EXPECT_DEVICE_Y(34); /* Y-up 34 -> device fb_height-34. */
    TEST_ASSERT_TRUE(slot->y >= ey - 0.001F && slot->y <= ey + 0.001F);
}

/* The input.* Y-up contract, locked end to end: a click at Y-up (10, 20) lands the pressed mouse
   slot at device (10, fb_height-20) — read==write with ui.*'s Y-up convention, differing only in the
   height basis (input = framebuffer px). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_input_yup_flip_lands_at_device_y(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":10,\"y\":20}}")));
    advance(); /* down@0 applies: the slot is created + pressed at the flipped device position. */
    nt_pointer_t *slot = NULL;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == NT_INPUT_INJECT_POINTER_ID_BASE) {
            slot = &g_nt_input.pointers[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(slot->x >= 9.999F && slot->x <= 10.001F); /* x passes through unflipped. */
    float ey = EXPECT_DEVICE_Y(20);                            /* y is Y-up -> device fb_height-20. */
    TEST_ASSERT_TRUE(slot->y >= ey - 0.001F && slot->y <= ey + 0.001F);
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* input.wheel lands on the mouse slot after an advance: a move creates the slot, the wheel delta
   accumulates onto it — exercises sched_release_one's NT_INJECT_WHEEL branch by value. */
static void test_sched_wheel_applies_on_advance(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.move\",\"params\":{\"x\":5,\"y\":5}}")));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.wheel\",\"params\":{\"dy\":3}}")));
    advance(); /* move creates the mouse slot, wheel accumulates on it (same advancing tick). */
    nt_pointer_t *slot = NULL;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == NT_INPUT_INJECT_POINTER_ID_BASE) {
            slot = &g_nt_input.pointers[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(slot->wheel_dy >= 2.999F && slot->wheel_dy <= 3.001F);
}

/* input.wheel{dx,dy,x,y}: self-contained positioned scroll. The handler enqueues a MOVE to (x,y) on
   the default mouse slot then the wheel, so after one advance the slot exists at (x,y) and carries
   the wheel delta — no prior input.move needed. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_sched_wheel_with_coords_scrolls_at_xy(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.wheel\",\"params\":{\"dy\":2,\"x\":40,\"y\":50}}")));
    advance();
    nt_pointer_t *slot = NULL;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == NT_INPUT_INJECT_POINTER_ID_BASE) {
            slot = &g_nt_input.pointers[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(slot->x >= 39.999F && slot->x <= 40.001F);
    float ey = EXPECT_DEVICE_Y(50); /* positioned wheel y=50 is Y-up -> device fb_height-50. */
    TEST_ASSERT_TRUE(slot->y >= ey - 0.001F && slot->y <= ey + 0.001F);
    TEST_ASSERT_TRUE(slot->wheel_dy >= 1.999F && slot->wheel_dy <= 2.001F);
}

/* input.wheel without x/y applies at the mouse slot's APPLY-TIME position: a prior input.move(60,70)
   places the slot, then a coordless wheel lands there (not a phantom (0,0)). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_sched_wheel_no_coords_applies_at_moved_position(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.move\",\"params\":{\"x\":60,\"y\":70}}")));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.wheel\",\"params\":{\"dy\":4}}")));
    advance(); /* move creates the slot @ (60,70), wheel accumulates on it in stage order */
    nt_pointer_t *slot = NULL;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == NT_INPUT_INJECT_POINTER_ID_BASE) {
            slot = &g_nt_input.pointers[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(slot->x >= 59.999F && slot->x <= 60.001F);
    float ey = EXPECT_DEVICE_Y(70); /* the prior input.move y=70 is Y-up -> device fb_height-70. */
    TEST_ASSERT_TRUE(slot->y >= ey - 0.001F && slot->y <= ey + 0.001F);
    TEST_ASSERT_TRUE(slot->wheel_dy >= 3.999F && slot->wheel_dy <= 4.001F);
}

/* Non-finite x/y on a positioned wheel -> bad_params (never stored as inf). */
static void test_input_wheel_inf_x_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.wheel\",\"params\":{\"dy\":1,\"x\":1e309,\"y\":0}}")); }

/* input.click hold=0 (same-frame instant click): down@0 + up@0 both release on one advancing tick,
   so the button is pressed AND released this frame, ending not-down. Exercises the sched up@0 path. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_sched_click_hold_zero_same_frame(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":5,\"y\":6,\"hold\":0}}")));
    advance(); /* down@0 + up@0 both due this tick */
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_LEFT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_LEFT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* ---- offline input.state{pop_text} drain coverage (no socket) ---- */

static void test_input_state_pop_text_drains_codepoints(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"pop_text\":true}}"))); /* clear any stale ring */
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"hi\"}}")));    /* enqueue char@0 'h','i' */
    advance();                                                                                               /* advance -> release + drain into the ring */
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"pop_text\":true}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *cps = cJSON_GetObjectItemCaseSensitive(result, "codepoints");
    TEST_ASSERT_TRUE(cJSON_IsArray(cps));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cps));
    TEST_ASSERT_EQUAL_INT(104, cJSON_GetArrayItem(cps, 0)->valueint);
    TEST_ASSERT_EQUAL_INT(105, cJSON_GetArrayItem(cps, 1)->valueint);
    cJSON_Delete(root);
}

/* ---- input.state (the observation read-back hook) ---- */

static void test_input_state_registers(void) { TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("input.state")); }

static void test_input_state_describe(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"input.state\"}}");
    cJSON *root = parse_ok(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_STRING("input", cJSON_GetObjectItemCaseSensitive(result, "group")->valuestring);
    cJSON_Delete(root);
}

/* No params -> ok with no key/text fields (read is valid with everything omitted). */
static void test_input_state_empty_ok(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(result, "down"));
    cJSON_Delete(root);
}

static void test_input_state_unknown_key_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"key\":\"NOPE\"}}")); }

static void test_input_state_bad_pop_text_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"pop_text\":1}}")); }

/* The drain-race, machine-observable: inject A -> input.state reads down==false (stale, no
   advance yet) -> advance -> input.state reads down==true. */
static void test_input_state_observes_injected_key(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"key\":\"A\"}}")); /* pre: not yet polled */
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result, "down")));
    cJSON_Delete(root);

    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\"}}"))); /* enqueue down@0 */
    advance();                                                                                         /* advance -> release applies the inject */
    root = parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"key\":\"A\"}}"));
    result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "down")));
    cJSON_Delete(root);
}

/* ---- input.button happy path — mask actually presses the mapped mouse button ---- */

/* input.button {buttons:2} on the reserved mouse slot -> after an advance the RIGHT button is down. */
static void test_input_button_right_presses_right(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":2}}")));
    advance();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_RIGHT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* Second input.button on the now-active slot exercises the MOVE branch (slot exists) and
   updates the mask: 4 -> MIDDLE down, RIGHT released (mask cleared its bit). */
static void test_input_button_move_branch_updates_mask(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":2}}")));
    advance();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":4}}"))); /* MOVE branch */
    advance();
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_MIDDLE));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_RIGHT));
}

/* input.button applies the mask at the slot's APPLY-TIME position, not the live submit-time one: a
   move queued ahead of the button must win. input.move(100,100) then input.button(1) then one
   advance -> the button lands at (100,100), not the (0,0) a submit-time read would have baked. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_input_button_applies_at_pending_move_position(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.move\",\"params\":{\"x\":100,\"y\":100}}")));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":1}}")));
    advance(); /* move (creates slot @100,100) then buttons applied at that slot position, in order */
    nt_pointer_t *slot = NULL;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == NT_INPUT_INJECT_POINTER_ID_BASE) {
            slot = &g_nt_input.pointers[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(slot->x >= 99.999F && slot->x <= 100.001F);
    float ey = EXPECT_DEVICE_Y(100); /* the queued input.move y=100 is Y-up -> device fb_height-100. */
    TEST_ASSERT_TRUE(slot->y >= ey - 0.001F && slot->y <= ey + 0.001F);
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* ---- a single-point gesture applies point[0] exactly once at frame 0 (no double-apply) ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_input_gesture_single_point_applied_once(void) {
    /* 1 point -> down@0 only (no move, no double-apply); queued = 1 down + 0 moves + 1 up = 2. */
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":7,\"points\":[[7,7]]}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
    advance(); /* down@0 + up@0 both release this advancing tick (last_offset==0 for a single point) */
    /* The slot landed at (7,7) with zero accumulated delta — DOWN set it once, no redundant MOVE. */
    nt_pointer_t *slot = NULL;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].id == 7U) {
            slot = &g_nt_input.pointers[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(slot->x >= 6.999F && slot->x <= 7.001F);
    float ey = EXPECT_DEVICE_Y(7); /* gesture point y=7 is Y-up -> device fb_height-7. */
    TEST_ASSERT_TRUE(slot->y >= ey - 0.001F && slot->y <= ey + 0.001F);
    TEST_ASSERT_TRUE(slot->dx >= -0.001F && slot->dx <= 0.001F); /* no move@0 -> no delta */
    TEST_ASSERT_TRUE(slot->dy >= -0.001F && slot->dy <= 0.001F);
}

/* ---- single unified scheduler: a FULL schedule drains in one tick without a buffer overflow ---- */

/* Locks the inject-buffer over-subscription fix: there is ONE scheduler (the input group's), capped
   <= the 256-slot immediate buffer, so filling it completely with frame-0-due entries and advancing
   ONE tick releases ALL of them with no sched_tick trap (the buffer can always hold a full schedule).
   The ui group delegates to THIS scheduler, so two sibling groups can never over-subscribe the buffer
   by construction. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_sched_full_drains_in_one_tick(void) {
    nt_devapi_input_reset();                                                                                 /* clean schedule + seeded advance clock. */
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"pop_text\":true}}"))); /* clear any stale ring */
    /* Fill the WHOLE schedule with NT_DEVAPI_INPUT_SCHED_MAX frame-0-due key-down entries. */
    fill_schedule_leaving(0U);
    /* One more 1-entry command must now overflow (schedule full) -> whole-or-nothing reject. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"B\"}}"));
    /* One advance releases ALL due entries into the 256-slot immediate buffer; sched_tick must NOT
       trap (NT_ASSERT(ok)) — the single cap <= the buffer guarantees room for the whole schedule. */
    advance();
    /* All released + applied: the key set by fill_schedule_leaving (NT_KEY_A) is down, and the
       schedule is now empty so a fresh 1-entry command succeeds again. */
    TEST_ASSERT_TRUE(nt_input_key_is_down(NT_KEY_A));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"C\"}}")));
}

/* Over-reserving the schedule is whole-or-nothing: a command that needs more entries than the cap
   can ever fit is rejected as bad_params with NO partial inject (no orphan DOWN without its UP).
   A full schedule + a 2-entry click proves the preflight rejects before writing anything. */
static void test_sched_over_reserve_whole_or_nothing(void) {
    nt_devapi_input_reset();
    fill_schedule_leaving(1U); /* exactly 1 free slot */
    /* A click needs 2 entries -> rejects whole; the 1 free slot must survive intact. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":1,\"y\":2}}"));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"B\"}}"))); /* the free slot survived */
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"C\"}}"));      /* now full -> proves click wrote nothing */
}

/* ---- command.describe ---- */

static void test_input_describe(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"input.key\"}}");
    cJSON *root = parse_ok(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_STRING("input", cJSON_GetObjectItemCaseSensitive(result, "group")->valuestring);
    const cJSON *ps = cJSON_GetObjectItemCaseSensitive(result, "params_shape");
    TEST_ASSERT_TRUE(cJSON_IsString(ps) && strlen(ps->valuestring) > 0);
    cJSON_Delete(root);

    resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"input.text\"}}");
    root = parse_ok(resp);
    result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_STRING("input", cJSON_GetObjectItemCaseSensitive(result, "group")->valuestring);
    cJSON_Delete(root);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_input_group_registers);
    RUN_TEST(test_input_key_ok);
    RUN_TEST(test_input_key_hold_ok);
    RUN_TEST(test_input_pointer_ok);
    RUN_TEST(test_input_click_queues_two);
    RUN_TEST(test_input_gesture_queues_ordered);
    RUN_TEST(test_input_set_player_enabled_ok);
    RUN_TEST(test_input_text_ok);
    RUN_TEST(test_input_text_utf8_multibyte);
    RUN_TEST(test_input_key_unknown_name_bad_params);
    RUN_TEST(test_input_key_wrong_type_bad_params);
    RUN_TEST(test_input_pointer_bad_action_bad_params);
    RUN_TEST(test_input_pointer_bad_type_bad_params);
    RUN_TEST(test_input_text_bad_type_bad_params);
    RUN_TEST(test_input_set_player_enabled_bad_type_bad_params);
    RUN_TEST(test_input_pointer_negative_id_bad_params);
    RUN_TEST(test_input_click_negative_id_bad_params);
    RUN_TEST(test_input_pointer_buttons_out_of_range_bad_params);
    RUN_TEST(test_input_click_button_out_of_range_bad_params);
    RUN_TEST(test_input_button_out_of_range_bad_params);
    RUN_TEST(test_input_click_near_full_atomic);
    RUN_TEST(test_input_gesture_near_full_atomic);
    RUN_TEST(test_input_text_bad_lead_byte_bad_params);
    RUN_TEST(test_input_text_truncated_continuation_bad_params);
    RUN_TEST(test_input_text_overlong_bad_params);
    RUN_TEST(test_input_text_surrogate_bad_params);
    RUN_TEST(test_input_text_above_max_bad_params);
    RUN_TEST(test_input_text_empty_queued_zero);
    RUN_TEST(test_input_text_exceeds_char_ring_bad_params);
    RUN_TEST(test_input_text_at_char_ring_ok);
    RUN_TEST(test_input_key_hold_fractional_bad_params);
    RUN_TEST(test_input_key_hold_negative_fractional_bad_params);
    RUN_TEST(test_input_gesture_frame_stride_fractional_bad_params);
    RUN_TEST(test_input_button_fractional_bad_params);
    RUN_TEST(test_input_click_button_fractional_bad_params);
    RUN_TEST(test_input_pointer_buttons_fractional_bad_params);
    RUN_TEST(test_input_move_inf_x_bad_params);
    RUN_TEST(test_input_move_overfloat_y_bad_params);
    RUN_TEST(test_input_click_inf_x_bad_params);
    RUN_TEST(test_input_pointer_inf_y_bad_params);
    RUN_TEST(test_input_pointer_overfloat_x_bad_params);
    RUN_TEST(test_input_gesture_inf_point_bad_params);
    RUN_TEST(test_input_wheel_inf_dx_bad_params);
    RUN_TEST(test_input_wheel_overfloat_dy_bad_params);
    RUN_TEST(test_sched_offset0_applies_on_advance);
    RUN_TEST(test_sched_pause_freeze);
    RUN_TEST(test_reset_drops_schedule_keeps_applied_input);
    RUN_TEST(test_sched_hold_releases_after_n_advances);
    RUN_TEST(test_sched_click_default_hold);
    RUN_TEST(test_sched_gesture_ordered_across_frames);
    RUN_TEST(test_sched_move_applies_on_advance);
    RUN_TEST(test_input_yup_flip_lands_at_device_y);
    RUN_TEST(test_sched_wheel_applies_on_advance);
    RUN_TEST(test_sched_wheel_with_coords_scrolls_at_xy);
    RUN_TEST(test_sched_wheel_no_coords_applies_at_moved_position);
    RUN_TEST(test_input_wheel_inf_x_bad_params);
    RUN_TEST(test_sched_click_hold_zero_same_frame);
    RUN_TEST(test_input_button_right_presses_right);
    RUN_TEST(test_input_button_move_branch_updates_mask);
    RUN_TEST(test_input_button_applies_at_pending_move_position);
    RUN_TEST(test_input_gesture_single_point_applied_once);
    RUN_TEST(test_sched_full_drains_in_one_tick);
    RUN_TEST(test_sched_over_reserve_whole_or_nothing);
    RUN_TEST(test_input_state_pop_text_drains_codepoints);
    RUN_TEST(test_input_state_registers);
    RUN_TEST(test_input_state_describe);
    RUN_TEST(test_input_state_empty_ok);
    RUN_TEST(test_input_state_unknown_key_bad_params);
    RUN_TEST(test_input_state_bad_pop_text_bad_params);
    RUN_TEST(test_input_state_observes_injected_key);
    RUN_TEST(test_input_describe);
    return UNITY_END();
}
