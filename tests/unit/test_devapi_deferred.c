/* Proof of the deferred yield path under the tick/drain contract: submit()->NULL on defer,
   advancing one frame = tick THEN poll; the K-th frame yields {ok,result,request_id} with the
   original id. Covers number/string/absent ids, multi-slot ordering, and overflow rejection. */

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

/* One frame under the new contract: advance all slots once, then read the ready ones. */
static const char *advance_frame(void) {
    nt_devapi_deferred_tick();
    return nt_devapi_poll_response();
}

/* submit() of a deferring command returns NULL (no sync response). */
static void test_submit_defers_returns_null(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":7}");
    TEST_ASSERT_NULL(resp);
}

/* K-1 frames return NULL, the K-th yields {ok:true,result:{...},request_id:7}, then drained. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_yield_after_k_frames_number_id(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":7}"));

    for (int i = 0; i < DEFER_FRAMES - 1; i++) {
        TEST_ASSERT_NULL(advance_frame()); /* still pending */
    }

    const char *resp = advance_frame();
    TEST_ASSERT_NOT_NULL(resp); /* yields on the K-th frame */
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
    TEST_ASSERT_NULL(advance_frame()); /* frames=2 -> 1 pending frame */

    const char *resp = advance_frame();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsString(id)); /* string stays string */
    TEST_ASSERT_EQUAL_STRING("abc", id->valuestring);
    cJSON_Delete(root);
}

/* No request_id: the command still defers and still pops (exercises the id==NULL branch). */
static void test_yield_absent_id(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"params\":{\"frames\":1}}"));

    const char *resp = advance_frame(); /* frames=1 -> ready on the first frame */
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_NULL(id); /* no id echoed when the request carried none */
    cJSON_Delete(root);

    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* drained */
}

/* Regression for the over-decrement bug: two slots with DIFFERENT K must each pop on their own
   frame, and neither may pop early. id=1 defers 1 frame, id=2 defers 3 frames. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_multi_slot_distinct_frames(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"frames\":1}}"));
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":2,\"params\":{\"frames\":3}}"));

    /* Frame 1: only id=1 (frames=1) is ready; id=2 (frames=3) must NOT pop. */
    nt_devapi_deferred_tick();
    const char *r1 = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(r1);
    cJSON *root1 = cJSON_Parse(r1);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItemCaseSensitive(root1, "request_id")->valueint);
    cJSON_Delete(root1);
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* id=2 still pending, not popped early */

    /* Frame 2: id=2 now at frames_left=1, not ready. */
    TEST_ASSERT_NULL(advance_frame());

    /* Frame 3: id=2 reaches 0 and pops. */
    nt_devapi_deferred_tick();
    const char *r2 = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(r2);
    cJSON *root2 = cJSON_Parse(r2);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(root2, "request_id")->valueint);
    cJSON_Delete(root2);

    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* both drained */
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

    /* An earlier in-flight slot is intact: advance one frame and assert nothing pops yet. */
    /* The first enqueued (request_id 0) has frames=1000; the overflow rejection touched no slot. */
    TEST_ASSERT_NULL(advance_frame()); /* ticks all live slots once; none ready (>=999 left) */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_submit_defers_returns_null);
    RUN_TEST(test_yield_after_k_frames_number_id);
    RUN_TEST(test_yield_string_id);
    RUN_TEST(test_yield_absent_id);
    RUN_TEST(test_multi_slot_distinct_frames);
    RUN_TEST(test_overflow_rejected_structured);
    return UNITY_END();
}
