/* L2 devapi input.* group via submit() (no socket): discovery lists the input commands with shapes,
   thin handlers forward to the L1 inject API, every bot-input violation returns bad_params (never
   asserts), input.text decodes UTF-8 -> codepoints (INPUT-06), and command.describe returns the
   full contract. Fire-and-forget: handlers return an immediate ok/queued envelope, never deferred. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "devapi/nt_devapi_internal.h"
#include "input/nt_input.h"
#include "input/nt_input_internal.h" /* inject API: fill the queue to probe L2 whole-or-nothing */
#include "unity.h"
/* clang-format on */

void setUp(void) { TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init()); }

void tearDown(void) { nt_devapi_shutdown(); }

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
    /* down@0 carries point[0]; a move per SUBSEQUENT point (F6: no redundant move@0 for point[0]) + up.
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

/* ---- input.text (INPUT-06) ---- */

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

/* ---- WR-02: out-of-domain numeric bot input -> bad_params (never silently coerced) ---- */

static void test_input_pointer_negative_id_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"down\",\"id\":-1,\"x\":1,\"y\":2}}")); }

static void test_input_click_negative_id_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":1,\"y\":2,\"id\":-5}}")); }

static void test_input_pointer_buttons_out_of_range_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.pointer\",\"params\":{\"action\":\"down\",\"id\":0,\"x\":1,\"y\":2,\"buttons\":8}}"));
}

static void test_input_click_button_out_of_range_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":1,\"y\":2,\"button\":8}}")); }

static void test_input_button_out_of_range_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":256}}")); }

/* ---- CR-01/WR-05: multi-event sugar is whole-or-nothing on a near-full inject queue ---- */

/* Fill the inject queue to leave exactly `free` slots, using future-scheduled keys (offset 1000)
   so a poll cannot drain them. Starts from a clean queue via nt_input_init. */
static void fill_inject_queue_leaving(uint32_t free) {
    nt_input_init();
    for (uint32_t i = 0; i < NT_INPUT_INJECT_QUEUE_MAX - free; i++) {
        TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_A, true, 1000));
    }
}

/* A click needs 2 entries; with only 1 free it must reject WHOLE and enqueue nothing (no orphan DOWN).
   Proof of "nothing enqueued": after the reject, exactly 1 free slot must remain — one 1-entry inject
   then succeeds and a second overflows (same idiom as the L1 test_overflow_rejects_whole). */
static void test_input_click_near_full_atomic(void) {
    fill_inject_queue_leaving(1U);
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.click\",\"params\":{\"x\":5,\"y\":6}}"));
    TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_B, true, 1000));  /* the 1 free slot survived */
    TEST_ASSERT_FALSE(nt_input_inject_key(NT_KEY_C, true, 1000)); /* now full -> proves click wrote nothing */
    nt_input_shutdown();
}

/* A 2-point gesture needs 3 entries (down + 1 move + up, F6); with only 2 free it rejects whole. */
static void test_input_gesture_near_full_atomic(void) {
    fill_inject_queue_leaving(2U);
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":1,\"points\":[[0,0],[5,5]]}}"));
    /* 2 free slots must remain intact: two 1-entry injects succeed, the third overflows. */
    TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_B, true, 1000));
    TEST_ASSERT_TRUE(nt_input_inject_key(NT_KEY_C, true, 1000));
    TEST_ASSERT_FALSE(nt_input_inject_key(NT_KEY_D, true, 1000));
    nt_input_shutdown();
}

/* ---- WR-05: input.text malformed-UTF-8 reject paths ---- */

/* Raw 0xFF byte (NOT a \u escape, which would produce valid UTF-8) -> malformed lead byte. cJSON
   copies high bytes between quotes verbatim, so the handler's decoder is what rejects it. */
static void test_input_text_bad_lead_byte_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xff\"}}")); }

/* A 2-byte lead (0xC0) with no continuation byte -> truncated sequence -> bad_params. */
static void test_input_text_truncated_continuation_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xc0\"}}")); }

/* ---- F1: structurally-valid-but-invalid UTF-8 scalars -> bad_params ---- */

/* Overlong: 0xC0 0x80 is a 2-byte form of U+0000 (below the 2-byte minimum 0x80). */
static void test_input_text_overlong_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xc0\x80\"}}")); }

/* UTF-16 surrogate: 0xED 0xA0 0x80 decodes to U+D800 (illegal scalar in UTF-8). */
static void test_input_text_surrogate_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xed\xa0\x80\"}}")); }

/* Out-of-range: 0xF4 0x90 0x80 0x80 decodes to U+110000 (> U+10FFFF). */
static void test_input_text_above_max_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\xf4\x90\x80\x80\"}}")); }

/* ---- F5: empty text is valid -> queued:0 (no-op), NOT bad_params ---- */

static void test_input_text_empty_queued_zero(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"\"}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
}

/* ---- F2: > 32 codepoints can never land whole in the 32-slot char ring -> bad_params ---- */

/* 33 ASCII 'a's: one over NT_INPUT_CHAR_RING. queued must never overstate what lands. */
static void test_input_text_exceeds_char_ring_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}")); }

/* Exactly 32 codepoints is the boundary -> still accepted (queued:32). */
static void test_input_text_at_char_ring_ok(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(32, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
}

/* ---- F5: hold / frame_stride strict integral+sign parse (not silent valueint truncation) ---- */

static void test_input_key_hold_fractional_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\",\"hold\":2.9}}")); }

static void test_input_key_hold_negative_fractional_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\",\"hold\":-0.5}}")); }

static void test_input_gesture_frame_stride_fractional_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":1,\"points\":[[0,0],[5,5]],\"frame_stride\":1.5}}"));
}

/* ---- WR-05: offline input.state{pop_text} drain coverage (no socket) ---- */

static void test_input_state_pop_text_drains_codepoints(void) {
    nt_input_init();
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"pop_text\":true}}"))); /* clear any stale ring */
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.text\",\"params\":{\"text\":\"hi\"}}")));    /* enqueue char@0 'h','i' */
    nt_input_poll(1);                                                                                        /* new frame -> drain into the ring */
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"pop_text\":true}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *cps = cJSON_GetObjectItemCaseSensitive(result, "codepoints");
    TEST_ASSERT_TRUE(cJSON_IsArray(cps));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cps));
    TEST_ASSERT_EQUAL_INT(104, cJSON_GetArrayItem(cps, 0)->valueint);
    TEST_ASSERT_EQUAL_INT(105, cJSON_GetArrayItem(cps, 1)->valueint);
    cJSON_Delete(root);
    nt_input_shutdown();
}

/* ---- input.state (the observation hook, INPUT-04/05/06 read-back) ---- */

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

/* L1+L2: inject A (offset 0) -> poll(new frame) drains it -> input.state{key:A} reads down==true. */
static void test_input_state_observes_injected_key(void) {
    nt_input_init();
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"key\":\"A\"}}"))); /* pre: not yet polled */
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"key\":\"A\"}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result, "down")));
    cJSON_Delete(root);

    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\"}}"))); /* enqueue down@0 */
    nt_input_poll(1);                                                                                  /* new frame -> drain applies the inject */
    root = parse_ok(nt_devapi_submit("{\"method\":\"input.state\",\"params\":{\"key\":\"A\"}}"));
    result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "down")));
    cJSON_Delete(root);
    nt_input_shutdown();
}

/* ---- F7: input.button happy path — mask actually presses the mapped mouse button ---- */

/* input.button {buttons:2} on the reserved mouse slot -> after poll the RIGHT button is down
   (creates the slot via DOWN since none is active yet). The reserved slot is a mouse, so the
   nt_input_mouse_* convenience queries observe it by-value (not just the ok envelope). */
static void test_input_button_right_presses_right(void) {
    nt_input_init();
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":2}}")));
    nt_input_poll(1);
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_pressed(NT_BUTTON_RIGHT));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    nt_input_shutdown();
}

/* Second input.button on the now-active slot exercises the MOVE branch (slot exists) and
   updates the mask: 4 -> MIDDLE down, RIGHT released (mask cleared its bit). */
static void test_input_button_move_branch_updates_mask(void) {
    nt_input_init();
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":2}}")));
    nt_input_poll(1);
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.button\",\"params\":{\"buttons\":4}}"))); /* MOVE branch */
    nt_input_poll(2);
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_MIDDLE));
    TEST_ASSERT_FALSE(nt_input_mouse_is_down(NT_BUTTON_RIGHT));
    TEST_ASSERT_TRUE(nt_input_mouse_is_released(NT_BUTTON_RIGHT));
    nt_input_shutdown();
}

/* ---- F6: a single-point gesture applies point[0] exactly once at frame 0 (no double-apply) ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_input_gesture_single_point_applied_once(void) {
    nt_input_init();
    /* 1 point -> down@0 only (no move, no double-apply); queued = 1 down + 0 moves + 1 up = 2. */
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":7,\"points\":[[7,7]]}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
    cJSON_Delete(root);
    nt_input_poll(1); /* down@0 + up@0 both drain this poll (last_offset==0 for a single point) */
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
    TEST_ASSERT_TRUE(slot->y >= 6.999F && slot->y <= 7.001F);
    TEST_ASSERT_TRUE(slot->dx >= -0.001F && slot->dx <= 0.001F); /* no move@0 -> no delta */
    TEST_ASSERT_TRUE(slot->dy >= -0.001F && slot->dy <= 0.001F);
    nt_input_shutdown();
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
    RUN_TEST(test_input_button_right_presses_right);
    RUN_TEST(test_input_button_move_branch_updates_mask);
    RUN_TEST(test_input_gesture_single_point_applied_once);
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
