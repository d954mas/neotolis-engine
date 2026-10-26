/* Registry: table registration, its registration-bug asserts, and the lifecycle hook slots. */

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

static void *s_seen_user_data;

static bool record_user_data(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) {
    s_seen_user_data = user_data;
    return true;
}

void setUp(void) {
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    s_seen_user_data = NULL;
}

void tearDown(void) { nt_devapi_shutdown(); }

static void test_init_double_init(void) {
    /* setUp already init'd successfully; a second init must fail. */
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_devapi_init());
}

static int s_user_data;

static const nt_devapi_command k_test_cmds[] = {
    {.method = "test.liveness", .summary = "liveness check", .params_shape = "{}", .result_shape = "{pong:bool}", .fn = record_user_data},
    {.method = "test.other", .summary = "other", .params_shape = "{n:number}", .result_shape = "{}", .fn = record_user_data},
};

/* Every handler of a table receives that table's user_data. */
static void test_table_user_data_reaches_each_handler(void) {
    nt_devapi_register(k_test_cmds, NT_COUNTOF(k_test_cmds), &s_user_data);
    for (size_t i = 0; i < NT_COUNTOF(k_test_cmds); i++) {
        char req[64];
        (void)snprintf(req, sizeof req, "{\"method\":\"%s\"}", k_test_cmds[i].method);
        s_seen_user_data = NULL;
        TEST_ASSERT_NOT_NULL(strstr(nt_devapi_submit(req), "\"ok\":true"));
        TEST_ASSERT_EQUAL_PTR(&s_user_data, s_seen_user_data);
    }
}

/* The registry fills to exactly NT_DEVAPI_MAX_COMMANDS (init registered the 2 discovery commands). */
static char s_fill_names[NT_DEVAPI_MAX_COMMANDS][16];
static nt_devapi_command s_fill_cmds[NT_DEVAPI_MAX_COMMANDS];

static void test_registry_fills_to_the_cap(void) {
    const size_t free_slots = NT_DEVAPI_MAX_COMMANDS - 2U;
    for (size_t i = 0; i < free_slots; i++) {
        (void)snprintf(s_fill_names[i], sizeof s_fill_names[i], "fill.%u", (unsigned)i);
        s_fill_cmds[i] = (nt_devapi_command){.method = s_fill_names[i], .summary = "s", .params_shape = "{}", .result_shape = "{}", .fn = dummy_handler};
    }
    nt_devapi_register(s_fill_cmds, free_slots, NULL);
    TEST_ASSERT_NOT_NULL(strstr(nt_devapi_submit("{\"method\":\"fill.0\"}"), "\"ok\":true"));
}

static int s_hook_order[4];
static int s_hook_calls;
static void poll_a(void) { s_hook_order[s_hook_calls++] = 1; }
static void tick_a(void) { s_hook_order[s_hook_calls++] = 2; }

/* nt_devapi_update runs the transport poll before the tick, whatever the set order; re-setting the
   same poll (a transport stop->start) is allowed. */
static void test_update_runs_poll_before_tick(void) {
    nt_devapi_set_tick(tick_a);
    nt_devapi_set_transport_poll(poll_a);
    nt_devapi_set_transport_poll(poll_a);
    s_hook_calls = 0;
    nt_devapi_update();
    TEST_ASSERT_EQUAL_INT(2, s_hook_calls);
    TEST_ASSERT_EQUAL_INT(1, s_hook_order[0]);
    TEST_ASSERT_EQUAL_INT(2, s_hook_order[1]);
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* register before init is a caller bug (empty registry): the precondition asserts s_initialized. */
static void test_register_before_init_asserts(void) {
    nt_devapi_shutdown(); /* setUp init'd; drop back to uninitialized */
    NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_test_cmds, NT_COUNTOF(k_test_cmds), NULL));
}

/* A duplicate method is a registration bug: it asserts instead of overwriting or returning an error. */
static void test_dup_method_asserts(void) {
    nt_devapi_register(k_test_cmds, NT_COUNTOF(k_test_cmds), NULL);
    NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_test_cmds, NT_COUNTOF(k_test_cmds), NULL));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "registry_find"));
}

/* Duplicates inside one table assert too. */
static const nt_devapi_command k_dup_cmds[] = {
    {.method = "test.same", .summary = "first", .params_shape = "{}", .result_shape = "{}", .fn = dummy_handler},
    {.method = "test.same", .summary = "second", .params_shape = "{}", .result_shape = "{}", .fn = dummy_handler},
};

static void test_dup_in_one_table_asserts(void) {
    NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_dup_cmds, NT_COUNTOF(k_dup_cmds), NULL));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "registry_find"));
}

/* Every descriptor field is required: discovery prints them all, dispatch calls fn. The field check runs
   before the duplicate lookup, so a NULL method never reaches strcmp. */
static const nt_devapi_command k_incomplete_cmds[] = {
    {.method = NULL, .summary = "s", .params_shape = "{}", .result_shape = "{}", .fn = dummy_handler},
    {.method = "test.no_summary", .summary = NULL, .params_shape = "{}", .result_shape = "{}", .fn = dummy_handler},
    {.method = "test.no_params", .summary = "s", .params_shape = NULL, .result_shape = "{}", .fn = dummy_handler},
    {.method = "test.no_result", .summary = "s", .params_shape = "{}", .result_shape = NULL, .fn = dummy_handler},
    {.method = "test.no_fn", .summary = "s", .params_shape = "{}", .result_shape = "{}", .fn = NULL},
};

static void test_missing_field_asserts(void) {
    for (size_t i = 0; i < NT_COUNTOF(k_incomplete_cmds); i++) {
        NT_TEST_EXPECT_ASSERT(nt_devapi_register(&k_incomplete_cmds[i], 1, NULL));
        TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "cmd->fn != NULL"));
    }
}

static void poll_b(void) {}

/* A second transport poll or tick is a host bug: each slot has one owner. */
static void test_second_hook_owner_asserts(void) {
    nt_devapi_set_transport_poll(poll_a);
    NT_TEST_EXPECT_ASSERT(nt_devapi_set_transport_poll(poll_b));
    nt_devapi_set_tick(tick_a);
    NT_TEST_EXPECT_ASSERT(nt_devapi_set_tick(tick_a));
}

/* One row past a full registry asserts before the row is read. */
static void test_one_past_the_cap_asserts(void) {
    test_registry_fills_to_the_cap();
    NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_test_cmds, 1, NULL));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "NT_DEVAPI_MAX_COMMANDS"));
}

/* The count is checked against the free slots before any row is read. */
static void test_overflow_asserts(void) {
    NT_TEST_EXPECT_ASSERT(nt_devapi_register(k_test_cmds, NT_DEVAPI_MAX_COMMANDS + 1U, NULL));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "NT_DEVAPI_MAX_COMMANDS"));
}

static bool fail_without_err(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) { return false; }

static bool defer_to_silent_fail(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data) { return nt_devapi_defer(NT_DEVAPI_GATE_PRESENT, 0.0, fail_without_err, err); }

static const nt_devapi_command k_silent_fail_cmds[] = {
    {.method = "test.silent_fail", .summary = "defers a reply that returns false without filling err", .params_shape = "{}", .result_shape = "{}", .fn = defer_to_silent_fail},
};

/* A failing handler must fill err; the dispatcher asserts instead of inventing an error. The reply runs
   at the pre-swap seam, outside submit, so the trap unwinds past no live request tree. */
static void test_fail_without_err_asserts(void) {
    nt_devapi_register(k_silent_fail_cmds, NT_COUNTOF(k_silent_fail_cmds), NULL);
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
    RUN_TEST(test_table_user_data_reaches_each_handler);
    RUN_TEST(test_registry_fills_to_the_cap);
    RUN_TEST(test_update_runs_poll_before_tick);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_register_before_init_asserts);
    RUN_TEST(test_dup_method_asserts);
    RUN_TEST(test_dup_in_one_table_asserts);
    RUN_TEST(test_missing_field_asserts);
    RUN_TEST(test_overflow_asserts);
    RUN_TEST(test_one_past_the_cap_asserts);
    RUN_TEST(test_second_hook_owner_asserts);
    RUN_TEST(test_fail_without_err_asserts);
    RUN_TEST(test_submit_before_init_asserts);
#endif
    return UNITY_END();
}
