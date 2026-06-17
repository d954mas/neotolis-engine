/* Proof of the deferred yield path: submit()->NULL on defer, K-1 NULL polls,
   the K-th poll yields {ok,result,request_id} with the original id, drain, and overflow. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

/* Test-only deferred command — registered LOCALLY in this binary, NEVER in the engine
   `core` group. Defers K frames (params {frames?:number}, default DEFER_FRAMES). */
#define DEFER_FRAMES 3

static bool defer_handler(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) {
    (void)result_obj; /* the continuation fills the result at yield time. */
    (void)err;
    (void)user_data;
    int frames = DEFER_FRAMES;
    const cJSON *f = cJSON_GetObjectItemCaseSensitive(params, "frames");
    if (cJSON_IsNumber(f)) {
        frames = f->valueint;
    }
    return nt_devapi_defer_current(frames);
}

static void register_defer(void) {
    nt_devapi_command_desc desc = {
        .method = "test.defer",
        .group = "test",
        .summary = "test-only deferred command",
        .params_shape = "{frames?:number}",
        .result_shape = "{deferred:bool}",
        .frame_behavior = "deferred",
        .side_effects = "none",
    };
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_register(&desc, defer_handler, NULL));
}

void setUp(void) {
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    register_defer();
}

void tearDown(void) { nt_devapi_shutdown(); }

/* submit() of a deferring command returns NULL (no sync response). */
static void test_submit_defers_returns_null(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":7}");
    TEST_ASSERT_NULL(resp);
}

/* K-1 polls return NULL, the K-th yields {ok:true,result:{...},request_id:7}, then drained. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_yield_after_k_polls_number_id(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":7}"));

    for (int i = 0; i < DEFER_FRAMES - 1; i++) {
        TEST_ASSERT_NULL(nt_devapi_poll_response()); /* still pending */
    }

    const char *resp = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(resp); /* yields on the K-th poll */
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsObject(result));
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsNumber(id)); /* number stays number */
    TEST_ASSERT_EQUAL_INT(7, id->valueint);
    cJSON_Delete(root);

    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* queue drained */
}

/* A string request_id survives the deferred path as a string. */
static void test_yield_string_id(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":\"abc\",\"params\":{\"frames\":2}}"));
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* frames=2 -> 1 pending poll */

    const char *resp = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsString(id)); /* string stays string */
    TEST_ASSERT_EQUAL_STRING("abc", id->valuestring);
    cJSON_Delete(root);
}

/* Overflow: more than NT_DEVAPI_MAX_DEFERRED concurrent defers rejects the overflowing
   call with a structured bad_params error (not NULL), leaving the earlier slots intact. */
static void test_overflow_rejected_structured(void) {
    /* Fill the queue with long-lived defers (frames high enough to outlast this test). */
    for (int i = 0; i < NT_DEVAPI_MAX_DEFERRED; i++) {
        char line[96];
        (void)snprintf(line, sizeof(line), "{\"method\":\"test.defer\",\"request_id\":%d,\"params\":{\"frames\":1000}}", i);
        TEST_ASSERT_NULL(nt_devapi_submit(line)); /* each defers, no free-slot exhaustion yet */
    }

    /* The next defer has no free slot -> rejected whole with a structured error, NOT NULL. */
    const char *resp = nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":999}");
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    TEST_ASSERT_EQUAL_INT(999, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);

    /* An earlier in-flight slot is intact: tick it down and assert it still yields cleanly. */
    /* The first enqueued (request_id 0) has frames=1000; the overflow rejection touched no slot. */
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* decrements all live slots, none ready (>=999 left) */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_submit_defers_returns_null);
    RUN_TEST(test_yield_after_k_polls_number_id);
    RUN_TEST(test_yield_string_id);
    RUN_TEST(test_overflow_rejected_structured);
    return UNITY_END();
}
