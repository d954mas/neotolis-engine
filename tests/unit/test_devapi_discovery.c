/* Discovery group — endpoints / command.describe / features over the registry,
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
    (void)params;
    (void)err;
    bool *flag = (bool *)ud;
    *flag = true;
    cJSON_AddBoolToObject(result, "poked", true);
    return true;
}

static const nt_devapi_command_desc k_game_poke = {
    .method = "game.poke",
    .group = "game",
    .summary = "test game-layer command (flips a flag)",
    .params_shape = "{}",
    .result_shape = "{poked:bool}",
    .frame_behavior = "any",
    .side_effects = "flips a test flag",
};

void setUp(void) {
    s_game_poked = false;
    /* init registers core + discovery groups. */
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    /* Register the game command through the public API only — the test touches
       no engine devapi source to add it (zero engine edits). */
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_register(&k_game_poke, cmd_game_poke, &s_game_poked));
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

static bool array_has_string(const cJSON *arr, const char *want) {
    const cJSON *m = NULL;
    cJSON_ArrayForEach(m, arr) {
        if (cJSON_IsString(m) && strcmp(m->valuestring, want) == 0) {
            return true;
        }
    }
    return false;
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

static void test_endpoints_cheap_has_three_fields(void) {
    const cJSON *commands = NULL;
    cJSON *root = submit_endpoints("{\"method\":\"endpoints\"}", &commands);

    /* cheap form: exactly method+group+summary. */
    const cJSON *ping = find_command(commands, "ping");
    TEST_ASSERT_NOT_NULL(ping);
    TEST_ASSERT_EQUAL_INT(3, count_object_fields(ping));
    TEST_ASSERT_TRUE(field_is_string(ping, "method"));
    TEST_ASSERT_TRUE(field_is_string(ping, "group"));
    TEST_ASSERT_TRUE(field_is_string(ping, "summary"));
    cJSON_Delete(root);
}

static void test_endpoints_detail_has_seven_fields(void) {
    const cJSON *commands = NULL;
    cJSON *root = submit_endpoints("{\"method\":\"endpoints\",\"params\":{\"detail\":true}}", &commands);

    const cJSON *ping = find_command(commands, "ping");
    TEST_ASSERT_NOT_NULL(ping);
    TEST_ASSERT_EQUAL_INT(7, count_object_fields(ping));
    TEST_ASSERT_TRUE(field_is_string(ping, "params_shape"));
    TEST_ASSERT_TRUE(field_is_string(ping, "result_shape"));
    TEST_ASSERT_TRUE(field_is_string(ping, "frame_behavior"));
    TEST_ASSERT_TRUE(field_is_string(ping, "side_effects"));
    cJSON_Delete(root);
}

/* detail is optional, but a present non-bool detail is rejected (not silently ignored). */
static void test_endpoints_non_bool_detail_bad_params(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"endpoints\",\"params\":{\"detail\":\"yes\"}}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    cJSON_Delete(root);
}

/* ---- command.describe ---- */

static void test_command_describe_ping(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"ping\"}}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(7, count_object_fields(result));
    TEST_ASSERT_EQUAL_STRING("ping", cJSON_GetObjectItemCaseSensitive(result, "method")->valuestring);
    TEST_ASSERT_EQUAL_STRING("core", cJSON_GetObjectItemCaseSensitive(result, "group")->valuestring);
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

/* ---- features ---- */

/* The active command groups depend on which NT_DEVAPI_GROUP_<X> options were ON at configure time.
   The test target is fed the SAME active-group defines that gate the nt_devapi lib (the per-group
   NT_DEVAPI_GROUP_<X> defines are PRIVATE to the lib, so they're re-supplied to this TU), so each
   group's presence + the expected distinct-group count are derived from the defines, not hardcoded.
   The `discovery` group is always present (this binary requires it) and `game` is registered in
   setUp via the public API — both are unconditional. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_features_lists_active_groups(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"features\"}");
    cJSON *root = cJSON_Parse(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *groups = cJSON_GetObjectItemCaseSensitive(result, "groups");
    TEST_ASSERT_TRUE(cJSON_IsArray(groups));

    /* Always present: discovery (required by this binary) + game (registered in setUp). */
    TEST_ASSERT_TRUE(array_has_string(groups, "discovery"));
    TEST_ASSERT_TRUE(array_has_string(groups, "game"));
    int expected = 2;

#ifdef NT_DEVAPI_GROUP_CORE
    TEST_ASSERT_TRUE(array_has_string(groups, "core"));
    expected += 1; /* core */
#else
    TEST_ASSERT_FALSE(array_has_string(groups, "core"));
#endif

#ifdef NT_DEVAPI_GROUP_TIME
    /* The time TU bundles three distinct group names: time / render / frame. */
    TEST_ASSERT_TRUE(array_has_string(groups, "time"));
    TEST_ASSERT_TRUE(array_has_string(groups, "render"));
    TEST_ASSERT_TRUE(array_has_string(groups, "frame"));
    expected += 3;
#else
    TEST_ASSERT_FALSE(array_has_string(groups, "time"));
    TEST_ASSERT_FALSE(array_has_string(groups, "render"));
    TEST_ASSERT_FALSE(array_has_string(groups, "frame"));
#endif

#ifdef NT_DEVAPI_GROUP_INPUT
    TEST_ASSERT_TRUE(array_has_string(groups, "input"));
    expected += 1; /* input */
#else
    TEST_ASSERT_FALSE(array_has_string(groups, "input"));
#endif

#ifdef NT_DEVAPI_GROUP_UI
    TEST_ASSERT_TRUE(array_has_string(groups, "ui"));
    expected += 1; /* ui */
#else
    TEST_ASSERT_FALSE(array_has_string(groups, "ui"));
#endif

#ifdef NT_DEVAPI_GROUP_OBS
    /* The obs TU bundles four distinct group names: log / perf / entity / resource. */
    TEST_ASSERT_TRUE(array_has_string(groups, "log"));
    TEST_ASSERT_TRUE(array_has_string(groups, "perf"));
    TEST_ASSERT_TRUE(array_has_string(groups, "entity"));
    TEST_ASSERT_TRUE(array_has_string(groups, "resource"));
    expected += 4;
#else
    TEST_ASSERT_FALSE(array_has_string(groups, "log"));
    TEST_ASSERT_FALSE(array_has_string(groups, "perf"));
    TEST_ASSERT_FALSE(array_has_string(groups, "entity"));
    TEST_ASSERT_FALSE(array_has_string(groups, "resource"));
#endif

    /* an absent group is not listed. */
    TEST_ASSERT_FALSE(array_has_string(groups, "phantom"));
    /* groups are distinct: each group's commands collapse to one entry. */
    TEST_ASSERT_EQUAL_INT(expected, cJSON_GetArraySize(groups));
    cJSON_Delete(root);
}

/* ---- game-layer registration discoverable + callable ---- */

static void test_game_command_discoverable(void) {
    const cJSON *commands = NULL;
    cJSON *root = submit_endpoints("{\"method\":\"endpoints\"}", &commands);
    const cJSON *poke = find_command(commands, "game.poke");
    TEST_ASSERT_NOT_NULL(poke);
    TEST_ASSERT_EQUAL_STRING("game", cJSON_GetObjectItemCaseSensitive(poke, "group")->valuestring);
    cJSON_Delete(root);
}

static void test_game_command_describe(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"game.poke\"}}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_STRING("game", cJSON_GetObjectItemCaseSensitive(result, "group")->valuestring);
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
    RUN_TEST(test_endpoints_cheap_has_three_fields);
    RUN_TEST(test_endpoints_detail_has_seven_fields);
    RUN_TEST(test_endpoints_non_bool_detail_bad_params);
    RUN_TEST(test_command_describe_ping);
    RUN_TEST(test_command_describe_missing_method_bad_params);
    RUN_TEST(test_command_describe_unknown_method);
    RUN_TEST(test_command_describe_message_distinguishes_absent_vs_nonstring);
    RUN_TEST(test_features_lists_active_groups);
    RUN_TEST(test_game_command_discoverable);
    RUN_TEST(test_game_command_describe);
    RUN_TEST(test_game_command_callable);
    RUN_TEST(test_absent_command_not_in_endpoints);
    return UNITY_END();
}
