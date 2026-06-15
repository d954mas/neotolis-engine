/* Registry register / dup-reject / owned-copy survival. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

/* Local heap copy — avoids the MSVC-deprecated POSIX strdup. */
static char *heap_copy(const char *src) {
    size_t len = strlen(src) + 1U;
    char *dst = (char *)malloc(len);
    TEST_ASSERT_NOT_NULL(dst);
    memcpy(dst, src, len);
    return dst;
}

static bool dummy_handler(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) {
    (void)params;
    (void)result_obj;
    (void)err;
    (void)user_data;
    return true;
}

/* init auto-registers the compiled-in commands, so the registry is non-empty after
   setUp. Tests assert DELTAS from this baseline count. */
static int s_base_cmds;

void setUp(void) {
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    s_base_cmds = nt_devapi_registry_count();
}

void tearDown(void) { nt_devapi_shutdown(); }

static void test_init_double_init(void) {
    /* setUp already init'd successfully; a second init must fail. */
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_devapi_init());
}

static void test_register_seven_fields_round_trip(void) {
    nt_devapi_command_desc desc = {
        .method = "test.liveness",
        .group = "test",
        .summary = "liveness check",
        .params_shape = "{}",
        .result_shape = "{pong:bool}",
        .frame_behavior = "any",
        .side_effects = "none",
    };
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_register(&desc, dummy_handler, NULL));
    TEST_ASSERT_EQUAL_INT(s_base_cmds + 1, nt_devapi_registry_count());

    const nt_devapi_slot *slot = nt_devapi_registry_find("test.liveness");
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL_STRING("test.liveness", slot->method);
    TEST_ASSERT_EQUAL_STRING("test", slot->group);
    TEST_ASSERT_EQUAL_STRING("liveness check", slot->summary);
    TEST_ASSERT_EQUAL_STRING("{}", slot->params_shape);
    TEST_ASSERT_EQUAL_STRING("{pong:bool}", slot->result_shape);
    TEST_ASSERT_EQUAL_STRING("any", slot->frame_behavior);
    TEST_ASSERT_EQUAL_STRING("none", slot->side_effects);
    TEST_ASSERT_EQUAL_PTR(dummy_handler, slot->handler);
}

/* Descriptors are strdup-copied — mutating/freeing the source after
   register must not corrupt the stored copy. */
static void test_owned_copy_survives_source_free(void) {
    char *method = heap_copy("foo");
    char *summary = heap_copy("first summary");

    nt_devapi_command_desc desc = {
        .method = method,
        .group = "test",
        .summary = summary,
        .params_shape = "{}",
        .result_shape = "{}",
        .frame_behavior = "any",
        .side_effects = "none",
    };
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_register(&desc, dummy_handler, NULL));

    /* Corrupt then free the source buffers. */
    method[0] = 'X';
    summary[0] = 'X';
    free(method);
    free(summary);

    const nt_devapi_slot *slot = nt_devapi_registry_find("foo");
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL_STRING("foo", slot->method);
    TEST_ASSERT_EQUAL_STRING("first summary", slot->summary);
}

/* A duplicate method is rejected, not overwritten. */
static void test_dup_method_rejected_first_preserved(void) {
    nt_devapi_command_desc first = {
        .method = "foo",
        .group = "test",
        .summary = "the original",
        .params_shape = "{}",
        .result_shape = "{}",
        .frame_behavior = "any",
        .side_effects = "none",
    };
    nt_devapi_command_desc second = {
        .method = "foo",
        .group = "impostor",
        .summary = "the impostor",
        .params_shape = "{}",
        .result_shape = "{}",
        .frame_behavior = "any",
        .side_effects = "none",
    };

    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_register(&first, dummy_handler, NULL));
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_devapi_register(&second, dummy_handler, NULL));

    /* Exactly one NEW slot added (the dup was rejected), and it is the original. */
    TEST_ASSERT_EQUAL_INT(s_base_cmds + 1, nt_devapi_registry_count());
    const nt_devapi_slot *slot = nt_devapi_registry_find("foo");
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL_STRING("test", slot->group);
    TEST_ASSERT_EQUAL_STRING("the original", slot->summary);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_double_init);
    RUN_TEST(test_register_seven_fields_round_trip);
    RUN_TEST(test_owned_copy_survives_source_free);
    RUN_TEST(test_dup_method_rejected_first_preserved);
    return UNITY_END();
}
