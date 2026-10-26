/* Registry: table registration by reference and its registration-bug asserts. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <string.h>

/* clang-format off */
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

#include "core/nt_assert.h" /* NT_ASSERT_MODE for the death-test guard */
#include "test_helpers/nt_assert_trap.h"

static bool dummy_handler(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) { return true; }

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

static int s_user_data;

static const nt_devapi_command k_test_cmds[] = {
    {"test.liveness", "liveness check", "{}", "{pong:bool}", dummy_handler},
    {"test.other", "other", "{n:number}", "{}", dummy_handler},
};

/* The table is stored by reference: each slot points at its descriptor and carries the table's user_data. */
static void test_register_table_by_reference(void) {
    nt_devapi_register(k_test_cmds, 2, &s_user_data);
    TEST_ASSERT_EQUAL_INT(s_base_cmds + 2, nt_devapi_registry_count());
    for (int i = 0; i < 2; i++) {
        const nt_devapi_slot *slot = nt_devapi_registry_find(k_test_cmds[i].method);
        TEST_ASSERT_NOT_NULL(slot);
        TEST_ASSERT_EQUAL_PTR(&k_test_cmds[i], slot->cmd);
        TEST_ASSERT_EQUAL_PTR(&s_user_data, slot->user_data);
    }
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* register before init is a caller bug (empty registry): the precondition asserts s_initialized. */
static void test_register_before_init_asserts(void) {
    nt_devapi_shutdown(); /* setUp init'd; drop back to uninitialized */
    NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_test_cmds, 1, NULL));
}

/* A duplicate method is a registration bug: it asserts instead of overwriting or returning an error. */
static void test_dup_method_asserts(void) {
    nt_devapi_register(k_test_cmds, 1, NULL);
    NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_test_cmds, 1, NULL));
}

/* Duplicates inside one table assert too. */
static const nt_devapi_command k_dup_cmds[] = {
    {"test.same", "first", "{}", "{}", dummy_handler},
    {"test.same", "second", "{}", "{}", dummy_handler},
};

static void test_dup_in_one_table_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_dup_cmds, NT_COUNTOF(k_dup_cmds), NULL)); }

/* Every descriptor field is required: discovery prints them all. */
static const nt_devapi_command k_incomplete_cmds[] = {
    {"test.incomplete", NULL, "{}", "{}", dummy_handler},
};

static void test_missing_field_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_incomplete_cmds, 1, NULL)); }

/* The count is checked against the free slots before any row is read. */
static void test_overflow_asserts(void) {
    NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_test_cmds, NT_DEVAPI_MAX_COMMANDS + 1U, NULL));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "NT_DEVAPI_MAX_COMMANDS"));
}

static bool fail_without_err(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) { return false; }

static bool defer_to_silent_fail(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) { return nt_devapi_defer(NT_DEVAPI_GATE_PRESENT, 0.0, fail_without_err, err); }

static const nt_devapi_command k_silent_fail_cmds[] = {
    {"test.silent_fail", "defers a reply that returns false without filling err", "{}", "{}", defer_to_silent_fail},
};

/* A failing handler must fill err; the dispatcher asserts instead of inventing an error. The reply runs
   at the pre-swap seam, outside submit, so the trap unwinds past no live request tree. */
static void test_fail_without_err_asserts(void) {
    nt_devapi_register(k_silent_fail_cmds, 1, NULL);
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.silent_fail\"}"));
    NT_TEST_EXPECT_ASSERT(nt_devapi_run_pre_swap_reply());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "err.code"));
}

/* submit before init is a caller bug: nt_devapi_submit asserts initialized (the registry is empty). */
static void test_submit_before_init_asserts(void) {
    nt_devapi_shutdown();
    NT_TEST_EXPECT_ASSERT((void)nt_devapi_submit("{\"method\":\"engine.info\"}"));
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_double_init);
    RUN_TEST(test_register_table_by_reference);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_register_before_init_asserts);
    RUN_TEST(test_dup_method_asserts);
    RUN_TEST(test_dup_in_one_table_asserts);
    RUN_TEST(test_missing_field_asserts);
    RUN_TEST(test_overflow_asserts);
    RUN_TEST(test_fail_without_err_asserts);
    RUN_TEST(test_submit_before_init_asserts);
#endif
    return UNITY_END();
}
