/* Discovery group — endpoints / command.describe over the registry,
   plus the zero-engine-edits game-layer path. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

/* A game command registered via the PUBLIC API with no engine edit.
   Its handler flips a flag through user_data so we can prove it is callable. */
static bool s_game_poked;

static bool cmd_game_poke(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    bool *flag = (bool *)ud;
    *flag = true;
    cJSON_AddBoolToObject(result, "poked", true);
    return true;
}

static const nt_devapi_command k_game_cmds[] = {
    {.method = "game.poke", .summary = "test game-layer command (flips a flag)", .params_shape = "{}", .result_shape = "{poked:bool}", .fn = cmd_game_poke},
};

void setUp(void) {
    s_game_poked = false;
    /* register_default registers all compiled-in groups (core, discovery, ...). */
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_devapi_register_default();
    /* Register the game command through the public API only — the test touches
       no engine devapi source to add it (zero engine edits). */
    nt_devapi_register(k_game_cmds, NT_COUNTOF(k_game_cmds), &s_game_poked);
}

void tearDown(void) { nt_devapi_shutdown(); }

/* ---- helpers ---- */

static const cJSON *find_command(const cJSON *commands, const char *method) {
    const cJSON *c = NULL;
    cJSON_ArrayForEach(c, commands) {
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(c, "method");
        if (cJSON_IsString(m) && strcmp(m->valuestring, method) == 0) {
            return c;
        }
    }
    return NULL;
}

static int count_object_fields(const cJSON *obj) {
    int n = 0;
    const cJSON *f = NULL;
    cJSON_ArrayForEach(f, obj) { n++; }
    return n;
}

static bool field_is_string(const cJSON *obj, const char *key) { return cJSON_IsString(cJSON_GetObjectItemCaseSensitive(obj, key)); }

/* Submit an endpoints request line; return the parsed response root (caller frees)
   and the commands array via out-param. Keeps the ok/object/array shape checks out
   of the per-field tests (result is object-wrapped, commands is an array). */
static cJSON *submit_endpoints(const char *request, const cJSON **commands_out) {
    cJSON *root = cJSON_Parse(nt_devapi_submit(request));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsObject(result));
    const cJSON *commands = cJSON_GetObjectItemCaseSensitive(result, "commands");
    TEST_ASSERT_TRUE(cJSON_IsArray(commands));
    *commands_out = commands;
    return root;
}

/* ---- endpoints ---- */

/* Every row carries all four descriptor fields, and the list is the whole registry. */
static void test_endpoints_has_four_fields(void) {
    const cJSON *commands = NULL;
    cJSON *root = submit_endpoints("{\"method\":\"endpoints\"}", &commands);

    TEST_ASSERT_EQUAL_INT(nt_devapi_registry_count(), cJSON_GetArraySize(commands));
    const cJSON *ping = find_command(commands, "ping");
    TEST_ASSERT_NOT_NULL(ping);
    TEST_ASSERT_EQUAL_INT(4, count_object_fields(ping));
    TEST_ASSERT_TRUE(field_is_string(ping, "method"));
    TEST_ASSERT_TRUE(field_is_string(ping, "summary"));
    TEST_ASSERT_TRUE(field_is_string(ping, "params_shape"));
    TEST_ASSERT_TRUE(field_is_string(ping, "result_shape"));
    cJSON_Delete(root);
}

/* ---- command.describe ---- */

static void test_command_describe_ping(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"ping\"}}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(4, count_object_fields(result));
    TEST_ASSERT_EQUAL_STRING("ping", cJSON_GetObjectItemCaseSensitive(result, "method")->valuestring);
    TEST_ASSERT_EQUAL_STRING("liveness check", cJSON_GetObjectItemCaseSensitive(result, "summary")->valuestring);
    cJSON_Delete(root);
}

static void test_command_describe_missing_method_bad_params(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{}}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    cJSON_Delete(root);
}

static void test_command_describe_unknown_method(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"nope.nope\"}}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("unknown_method", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    cJSON_Delete(root);
}

/* The two bad_params sub-cases carry distinct messages: absent vs wrong-typed method. */
static void test_command_describe_message_distinguishes_absent_vs_nonstring(void) {
    cJSON *r1 = cJSON_Parse(nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{}}"));
    cJSON *e1 = cJSON_GetObjectItemCaseSensitive(r1, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(e1, "code")->valuestring);
    TEST_ASSERT_EQUAL_STRING("missing params.method", cJSON_GetObjectItemCaseSensitive(e1, "message")->valuestring);
    cJSON_Delete(r1);

    cJSON *r2 = cJSON_Parse(nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":5}}"));
    cJSON *e2 = cJSON_GetObjectItemCaseSensitive(r2, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(e2, "code")->valuestring);
    TEST_ASSERT_EQUAL_STRING("non-string params.method", cJSON_GetObjectItemCaseSensitive(e2, "message")->valuestring);
    cJSON_Delete(r2);
}

/* ---- compiled-in groups ---- */

/* The listed commands depend on which NT_DEVAPI_GROUP_<X> options were ON at configure time. The test
   target is fed the SAME group defines that gate the nt_devapi lib, so each group's presence is derived
   from the defines, not hardcoded. discovery (this binary requires it) and game (setUp) are unconditional. */
static void expect_listed(const cJSON *commands, const char *method, bool listed) { TEST_ASSERT_EQUAL_MESSAGE(listed, find_command(commands, method) != NULL, method); }

static void test_endpoints_lists_compiled_groups(void) {
    const cJSON *commands = NULL;
    cJSON *root = submit_endpoints("{\"method\":\"endpoints\"}", &commands);
    expect_listed(commands, "endpoints", true);
    expect_listed(commands, "game.poke", true);
#ifdef NT_DEVAPI_GROUP_CORE
    expect_listed(commands, "ping", true);
#else
    expect_listed(commands, "ping", false);
#endif
#ifdef NT_DEVAPI_GROUP_TIME
    expect_listed(commands, "time.pause", true);
#else
    expect_listed(commands, "time.pause", false);
#endif
#ifdef NT_DEVAPI_GROUP_INPUT
    expect_listed(commands, "input.key", true);
#else
    expect_listed(commands, "input.key", false);
#endif
#ifdef NT_DEVAPI_GROUP_UI
    expect_listed(commands, "ui.tree", true);
#else
    expect_listed(commands, "ui.tree", false);
#endif
#ifdef NT_DEVAPI_GROUP_OBS
    expect_listed(commands, "log.tail", true);
#else
    expect_listed(commands, "log.tail", false);
#endif
#ifdef NT_DEVAPI_GROUP_ENTITY_WRITE
    expect_listed(commands, "entity.set", true);
#else
    expect_listed(commands, "entity.set", false);
#endif
#ifdef NT_DEVAPI_GROUP_CAPTURE
    expect_listed(commands, "capture.frame", true);
#else
    expect_listed(commands, "capture.frame", false);
#endif
    cJSON_Delete(root);
}

/* ---- game-layer registration discoverable + callable ---- */

static void test_game_command_discoverable(void) {
    const cJSON *commands = NULL;
    cJSON *root = submit_endpoints("{\"method\":\"endpoints\"}", &commands);
    TEST_ASSERT_NOT_NULL(find_command(commands, "game.poke"));
    cJSON_Delete(root);
}

static void test_game_command_describe(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"game.poke\"}}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_STRING("test game-layer command (flips a flag)", cJSON_GetObjectItemCaseSensitive(result, "summary")->valuestring);
    TEST_ASSERT_EQUAL_STRING("{}", cJSON_GetObjectItemCaseSensitive(result, "params_shape")->valuestring);
    TEST_ASSERT_EQUAL_STRING("{poked:bool}", cJSON_GetObjectItemCaseSensitive(result, "result_shape")->valuestring);
    cJSON_Delete(root);
}

static void test_game_command_callable(void) {
    TEST_ASSERT_FALSE(s_game_poked);
    const char *resp = nt_devapi_submit("{\"method\":\"game.poke\"}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "poked")));
    TEST_ASSERT_TRUE(s_game_poked); /* handler ran via user_data */
    cJSON_Delete(root);
}

/* ---- absent command is absent from endpoints ---- */

static void test_absent_command_not_in_endpoints(void) {
    const cJSON *commands = NULL;
    cJSON *root = submit_endpoints("{\"method\":\"endpoints\"}", &commands);
    /* nothing registered a "phantom.cmd" → it must not appear. */
    TEST_ASSERT_NULL(find_command(commands, "phantom.cmd"));
    cJSON_Delete(root);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_endpoints_has_four_fields);
    RUN_TEST(test_command_describe_ping);
    RUN_TEST(test_command_describe_missing_method_bad_params);
    RUN_TEST(test_command_describe_unknown_method);
    RUN_TEST(test_command_describe_message_distinguishes_absent_vs_nonstring);
    RUN_TEST(test_endpoints_lists_compiled_groups);
    RUN_TEST(test_game_command_discoverable);
    RUN_TEST(test_game_command_describe);
    RUN_TEST(test_game_command_callable);
    RUN_TEST(test_absent_command_not_in_endpoints);
    return UNITY_END();
}
