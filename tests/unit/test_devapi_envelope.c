/* PROTO-06 / PROTO-07: submit() envelope, request_id echo, unknown-method,
   batch order + continue-on-error, D-04 pointer-lifetime contract. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

/* Trivial handler so the envelope tests don't depend on Task 2's core group.
   Echoes back a marker + (if present) the params "n" so we can assert routing. */
static bool ok_handler(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) {
    (void)err;
    (void)user_data;
    cJSON_AddBoolToObject(result_obj, "ran", true);
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(params, "n");
    if (cJSON_IsNumber(n)) {
        cJSON_AddNumberToObject(result_obj, "n", n->valuedouble);
    }
    return true;
}

static void register_ok(void) {
    nt_devapi_command_desc desc = {
        .method = "ok",
        .layer = "core",
        .summary = "test handler",
        .params_shape = "{n?:number}",
        .result_shape = "{ran:bool}",
        .frame_behavior = "any",
        .side_effects = "none",
    };
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_register(&desc, ok_handler, NULL));
}

void setUp(void) {
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    register_ok();
}

void tearDown(void) { nt_devapi_shutdown(); }

/* ---- single object envelope ---- */

static void test_single_ok_envelope(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"ok\"}");
    TEST_ASSERT_NOT_NULL(resp);

    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsObject(result)); /* D-05: result is always an object */
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "ran")));
    cJSON_Delete(root);
}

static void test_params_routed_to_handler(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"ok\",\"params\":{\"n\":42}}");
    cJSON *root = cJSON_Parse(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *n = cJSON_GetObjectItemCaseSensitive(result, "n");
    TEST_ASSERT_TRUE(cJSON_IsNumber(n));
    TEST_ASSERT_EQUAL_INT(42, n->valueint);
    cJSON_Delete(root);
}

/* ---- request_id echo: number / string / absent (Pitfall 5) ---- */

static void test_request_id_number_echoed(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"ok\",\"request_id\":7}");
    cJSON *root = cJSON_Parse(resp);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsNumber(id)); /* number stays number */
    TEST_ASSERT_EQUAL_INT(7, id->valueint);
    cJSON_Delete(root);
}

static void test_request_id_string_echoed(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"ok\",\"request_id\":\"abc\"}");
    cJSON *root = cJSON_Parse(resp);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsString(id)); /* string stays string */
    TEST_ASSERT_EQUAL_STRING("abc", id->valuestring);
    cJSON_Delete(root);
}

static void test_request_id_absent_omitted(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"ok\"}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "request_id")); /* absent → omitted */
    cJSON_Delete(root);
}

/* ---- error paths ---- */

static void test_unknown_method(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"does_not_exist\"}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root); /* no crash */
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("unknown_method", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    cJSON_Delete(root);
}

static void test_malformed_json_bad_params(void) {
    const char *resp = nt_devapi_submit("{not json");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root); /* the RESPONSE is well-formed even though input wasn't */
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    cJSON_Delete(root);
}

static void test_request_id_echoed_on_error(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"nope\",\"request_id\":99}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_EQUAL_INT(99, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
}

/* ---- batch: order + continue-on-error (D-07) ---- */

static void test_batch_order_and_continue_on_error(void) {
    const char *resp = nt_devapi_submit("[{\"method\":\"ok\"},{\"method\":\"nope\"},{\"method\":\"ok\"}]");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsArray(root)); /* array → array */
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(root));

    cJSON *e0 = cJSON_GetArrayItem(root, 0);
    cJSON *e1 = cJSON_GetArrayItem(root, 1);
    cJSON *e2 = cJSON_GetArrayItem(root, 2);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(e0, "ok")));  /* entry 0 ok */
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(e1, "ok"))); /* entry 1 err */
    TEST_ASSERT_EQUAL_STRING("unknown_method", cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(e1, "error"), "code")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(e2, "ok"))); /* entry 2 still ran */
    cJSON_Delete(root);
}

/* ---- D-04 pointer-lifetime contract ----
   The returned const char* is valid only until the next submit. The SAFE pattern
   (which this test asserts) is: copy the first result BEFORE calling submit again,
   then compare the copy. We never dereference the first pointer after submit #2 —
   under ASan that would be a documented-invalid read. */
static void test_d04_copy_before_next_submit(void) {
    const char *first = nt_devapi_submit("{\"method\":\"ok\",\"request_id\":1}");
    char copy[256];
    size_t n = strlen(first);
    TEST_ASSERT_LESS_THAN(sizeof(copy), n);
    memcpy(copy, first, n + 1U); /* copy BEFORE the next submit invalidates `first` */

    const char *second = nt_devapi_submit("{\"method\":\"ok\",\"request_id\":2}");
    TEST_ASSERT_NOT_NULL(second);

    /* Assert against the copy, never the stale `first` pointer. */
    cJSON *root = cJSON_Parse(copy);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);

    cJSON *root2 = cJSON_Parse(second);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(root2, "request_id")->valueint);
    cJSON_Delete(root2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_single_ok_envelope);
    RUN_TEST(test_params_routed_to_handler);
    RUN_TEST(test_request_id_number_echoed);
    RUN_TEST(test_request_id_string_echoed);
    RUN_TEST(test_request_id_absent_omitted);
    RUN_TEST(test_unknown_method);
    RUN_TEST(test_malformed_json_bad_params);
    RUN_TEST(test_request_id_echoed_on_error);
    RUN_TEST(test_batch_order_and_continue_on_error);
    RUN_TEST(test_d04_copy_before_next_submit);
    return UNITY_END();
}
