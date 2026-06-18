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
    /* down + 2 moves + up = 4 entries. */
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"input.gesture\",\"params\":{\"id\":1,\"points\":[[0,0],[5,5]]}}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(4, cJSON_GetObjectItemCaseSensitive(result, "queued")->valueint);
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
    RUN_TEST(test_input_describe);
    return UNITY_END();
}
