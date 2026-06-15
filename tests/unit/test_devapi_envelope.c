/* submit() envelope, request_id echo, unknown-method, batch order +
   continue-on-error, pointer-lifetime contract. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

/* Trivial handler so the envelope tests don't depend on the core group.
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
    TEST_ASSERT_TRUE(cJSON_IsObject(result)); /* result is always an object */
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

/* ---- request_id echo: number / string / absent ---- */

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

/* A line protocol must reject trailing garbage after a valid value. */
static void test_trailing_garbage_rejected(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"ok\"} junk");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
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

/* ---- batch: order + continue-on-error ---- */

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

/* ---- pointer-lifetime contract ----
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

/* ---- non-object / non-array top-level inputs (the discriminator's else-branch) ---- */

static void test_bare_number_is_bad_params(void) {
    const char *resp = nt_devapi_submit("42"); /* valid JSON, but not an object or array */
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root); /* no crash on a non-object scalar */
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    cJSON_Delete(root);
}

static void test_json_null_is_bad_params(void) {
    const char *resp = nt_devapi_submit("null"); /* parses to a cJSON null node, not a NULL ptr */
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    cJSON_Delete(root);
}

static void test_empty_batch_returns_empty_array(void) {
    const char *resp = nt_devapi_submit("[]");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsArray(root)); /* empty array in -> empty array out */
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(root));
    cJSON_Delete(root);
}

static void test_batch_of_non_objects_each_bad_params(void) {
    const char *resp = nt_devapi_submit("[1,2]");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsArray(root));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(root));
    cJSON *e0 = cJSON_GetArrayItem(root, 0);
    cJSON *e1 = cJSON_GetArrayItem(root, 1);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(e0, "ok")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(e1, "ok")));
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(e0, "error"), "code")->valuestring);
    cJSON_Delete(root);
}

/* params is optional, but a present non-object params is rejected for any command. */
static void test_non_object_params_bad_params(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"ok\",\"params\":42}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    cJSON_Delete(root);
}

/* request_id must be a scalar; a present non-scalar (array/object) id is rejected and NOT echoed. */
static void test_non_scalar_request_id_rejected(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"ok\",\"request_id\":[1,2,3]}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(error, "code")->valuestring);
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "request_id")); /* invalid id not echoed */
    cJSON_Delete(root);
}

/* ---- request_id fidelity on the error + per-batch-entry paths ---- */

static void test_request_id_string_echoed_on_error(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"nope\",\"request_id\":\"xyz\"}");
    cJSON *root = cJSON_Parse(resp);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsString(id)); /* string id survives the error path unchanged */
    TEST_ASSERT_EQUAL_STRING("xyz", id->valuestring);
    cJSON_Delete(root);
}

static void test_batch_echoes_each_request_id(void) {
    const char *resp = nt_devapi_submit("[{\"method\":\"ok\",\"request_id\":1},{\"method\":\"ok\",\"request_id\":\"two\"}]");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(root));
    cJSON *id0 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(root, 0), "request_id");
    cJSON *id1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(root, 1), "request_id");
    TEST_ASSERT_TRUE(cJSON_IsNumber(id0)); /* each entry echoes its OWN id with its OWN type */
    TEST_ASSERT_EQUAL_INT(1, id0->valueint);
    TEST_ASSERT_TRUE(cJSON_IsString(id1));
    TEST_ASSERT_EQUAL_STRING("two", id1->valuestring);
    cJSON_Delete(root);
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
    RUN_TEST(test_trailing_garbage_rejected);
    RUN_TEST(test_request_id_echoed_on_error);
    RUN_TEST(test_batch_order_and_continue_on_error);
    RUN_TEST(test_d04_copy_before_next_submit);
    RUN_TEST(test_bare_number_is_bad_params);
    RUN_TEST(test_json_null_is_bad_params);
    RUN_TEST(test_empty_batch_returns_empty_array);
    RUN_TEST(test_batch_of_non_objects_each_bad_params);
    RUN_TEST(test_non_object_params_bad_params);
    RUN_TEST(test_non_scalar_request_id_rejected);
    RUN_TEST(test_request_id_string_echoed_on_error);
    RUN_TEST(test_batch_echoes_each_request_id);
    return UNITY_END();
}
