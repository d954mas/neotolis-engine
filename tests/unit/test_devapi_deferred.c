/* Proof of the deferred yield path: submit()->NULL on defer, advancing the game frame
   (g_nt_app.frame) then polling; the K-th frame yields {ok,result,request_id} with the original
   id. Covers number/string/absent ids, multi-slot ordering, and overflow rejection. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "app/nt_app.h"
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

/* Synthetic payload producer — no GL, no capture group — so the payload-yield path is provable in a
   pure unit binary. Exercises the owned-ctx lifecycle (set on defer, freed once on yield/reset);
   freed-once is asserted via s_producer_ctx_freed under ASan/LSan. */
static int s_producer_ctx_freed; /* incremented once per ctx free — proves no double / no leak. */

static void synthetic_ctx_free(void *ctx) {
    TEST_ASSERT_NOT_NULL(ctx);
    s_producer_ctx_freed++;
    free(ctx);
}

static cJSON *synthetic_producer(void *ctx) {
    TEST_ASSERT_NOT_NULL(ctx);                /* the owned ctx must reach the producer. */
    TEST_ASSERT_EQUAL_INT(0xA5, *(int *)ctx); /* and carry the handler-set sentinel. */
    cJSON *result = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(result);
    devapi_add_string(result, "probe", "ok");
    return result; /* owned by the slot; transferred into the yield envelope by poll_response. */
}

/* Defers 1 frame WITH a synthetic result producer + an owned ctx (a heap int sentinel). */
static bool defer_result_handler(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) {
    (void)params;
    (void)result_obj;
    (void)err;
    (void)user_data;
    int *ctx = (int *)malloc(sizeof(int));
    TEST_ASSERT_NOT_NULL(ctx);
    *ctx = 0xA5;
    return nt_devapi_defer_current_with_result(1, synthetic_producer, ctx, synthetic_ctx_free, "producer_failed", "synthetic producer failed");
}

static void register_defer_result(void) {
    nt_devapi_command_desc desc = {
        .method = "test.defer_result",
        .group = "test",
        .summary = "test-only deferred command with a synthetic result payload",
        .params_shape = "{}",
        .result_shape = "{probe:string}",
        .frame_behavior = "deferred",
        .side_effects = "none",
    };
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_register(&desc, defer_result_handler, NULL));
}

void setUp(void) {
    g_nt_app.frame = 0; /* deferred targets are g_nt_app.frame + N — start each test at a known frame. */
    s_producer_ctx_freed = 0;
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    register_defer();
    register_defer_result();
}

void tearDown(void) { nt_devapi_shutdown(); }

/* One game frame: advance the frame counter, then read any slot whose deadline was reached. */
static const char *advance_frame(void) {
    g_nt_app.frame++;
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
    g_nt_app.frame++;
    const char *r1 = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(r1);
    cJSON *root1 = cJSON_Parse(r1);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItemCaseSensitive(root1, "request_id")->valueint);
    cJSON_Delete(root1);
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* id=2 still pending, not popped early */

    /* Frame 2: id=2's target (frame 3) not yet reached. */
    TEST_ASSERT_NULL(advance_frame());

    /* Frame 3: id=2 reaches its target and pops. */
    g_nt_app.frame++;
    const char *r2 = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(r2);
    cJSON *root2 = cJSON_Parse(r2);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(root2, "request_id")->valueint);
    cJSON_Delete(root2);

    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* both drained */
}

/* A deferred command inside a batch is rejected with an error entry — never dropped from the
   array or leaked as the DEFERRED_SENTINEL pointer. The response stays 1:1 (length 2 here),
   each entry is ok:false bad_params, and no slot is enqueued. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_deferred_in_batch_rejected(void) {
    const char *resp = nt_devapi_submit("[{\"method\":\"test.defer\",\"request_id\":1},{\"method\":\"test.defer\",\"request_id\":2}]");
    TEST_ASSERT_NOT_NULL(resp); /* a batch always returns synchronously, never NULL */
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsArray(root));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(root)); /* ordered, 1:1 — nothing dropped */
    cJSON *e0 = cJSON_GetArrayItem(root, 0);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(e0, "ok")));
    cJSON *err0 = cJSON_GetObjectItemCaseSensitive(e0, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(err0, "code")->valuestring);
    cJSON_Delete(root);

    /* The rejected entries enqueued no deferred slot — the queue is empty. */
    TEST_ASSERT_NULL(advance_frame());
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
    TEST_ASSERT_NULL(advance_frame()); /* frame=1; the live slot targets frame 1000, nothing pops yet */
}

/* Payload yield: defer-with-result, advance the frame, drive the pre-swap seam (fills the slot's
   payload via the synthetic producer), then poll -> the entry carries the synthetic {probe:"ok"}
   payload AND the correlated request_id, NOT the legacy {deferred:true}. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_payload_yield_via_producer(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer_result\",\"request_id\":42}"));

    g_nt_app.frame++;                   /* reach the slot's 1-frame target. */
    nt_devapi_run_pre_swap_producers(); /* GL-valid seam fills the payload (synthetic — no GL). */
    /* The producer's input ctx is consumed + freed once at the seam (one-shot); the produced
       payload now lives on the slot, to be yielded next. */
    TEST_ASSERT_EQUAL_INT(1, s_producer_ctx_freed);

    const char *resp = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsObject(result));
    /* The stored payload was yielded, NOT the legacy {deferred:true}. */
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(result, "deferred"));
    cJSON *probe = cJSON_GetObjectItemCaseSensitive(result, "probe");
    TEST_ASSERT_TRUE(cJSON_IsString(probe));
    TEST_ASSERT_EQUAL_STRING("ok", probe->valuestring);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsNumber(id));
    TEST_ASSERT_EQUAL_INT(42, id->valueint); /* request_id correlation preserved. */
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_INT(1, s_producer_ctx_freed); /* still exactly once — yield does not re-free. */
    TEST_ASSERT_NULL(nt_devapi_poll_response());    /* queue drained. */
}

/* Legacy regression: a plain nt_devapi_defer_current(1) slot (no producer) still yields the
   content-free {deferred:true}; driving the pre-swap seam does not touch it (no producer). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_legacy_yield_unaffected(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":5,\"params\":{\"frames\":1}}"));

    g_nt_app.frame++;
    nt_devapi_run_pre_swap_producers(); /* a producer-less slot is untouched by the seam. */

    const char *resp = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsObject(result));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "deferred"))); /* legacy shape intact */
    TEST_ASSERT_EQUAL_INT(5, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
    TEST_ASSERT_NULL(nt_devapi_poll_response());
}

/* Reset-no-leak: defer-with-result, fill the payload via the seam, then reset WITHOUT polling.
   The seam already consumed the ctx; reset must free the unyielded owned PAYLOAD — LSan-clean
   (the leak the reset-no-leak case guards is the filled-but-never-yielded payload). */
static void test_reset_frees_filled_payload(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer_result\",\"request_id\":1}"));
    g_nt_app.frame++;
    nt_devapi_run_pre_swap_producers();             /* payload now owned by the slot, unyielded. */
    TEST_ASSERT_EQUAL_INT(1, s_producer_ctx_freed); /* ctx already consumed/freed at the seam. */

    nt_devapi_deferred_reset();                  /* must free the filled-but-unyielded payload (LSan). */
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* slot cleared — nothing pending. */
}

/* Reset-no-leak before fill: defer-with-result then reset WITHOUT driving the seam. The unfilled
   slot still owns its ctx (payload is NULL) — reset frees the ctx, no leak. */
static void test_reset_frees_unfilled_ctx(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer_result\",\"request_id\":2}"));
    /* no frame advance, no pre-swap fill: producer never ran, payload stays NULL. */
    nt_devapi_deferred_reset();
    TEST_ASSERT_EQUAL_INT(1, s_producer_ctx_freed); /* the unrun producer's ctx is still freed. */
    TEST_ASSERT_NULL(nt_devapi_poll_response());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_submit_defers_returns_null);
    RUN_TEST(test_yield_after_k_frames_number_id);
    RUN_TEST(test_yield_string_id);
    RUN_TEST(test_yield_absent_id);
    RUN_TEST(test_multi_slot_distinct_frames);
    RUN_TEST(test_deferred_in_batch_rejected);
    RUN_TEST(test_overflow_rejected_structured);
    RUN_TEST(test_payload_yield_via_producer);
    RUN_TEST(test_legacy_yield_unaffected);
    RUN_TEST(test_reset_frees_filled_payload);
    RUN_TEST(test_reset_frees_unfilled_ctx);
    return UNITY_END();
}
