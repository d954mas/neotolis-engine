/* The deferred reply core: submit()->NULL on defer, the reply yields once its gate opens (frames, game
   seconds, the pre-swap seam) with the original request_id; one reply in flight (busy); a gate that can no
   longer open answers stalled; reset frees what the slot owns. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "app/nt_app.h"
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

/* Test-only commands, registered locally in this binary: test.defer {gate, amount?, tag?} defers a reply that
   echoes the tag it keeps; test.defer_fail defers a reply that fails; test.echo answers at once. */
static int s_tag;

static bool reply_probe(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    TEST_ASSERT_NULL(params); /* a deferred reply runs with no params or user_data */
    TEST_ASSERT_NULL(ud);
    devapi_add_number(result, "tag", s_tag);
    return true;
}

static bool reply_fail(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) { return nt_devapi_fail_code(err, "probe_failed", "synthetic reply failed"); }

static nt_devapi_gate parse_gate(const cJSON *params) {
    const cJSON *g = cJSON_GetObjectItemCaseSensitive(params, "gate");
    if (cJSON_IsString(g) && strcmp(g->valuestring, "seconds") == 0) {
        return NT_DEVAPI_GATE_SECONDS;
    }
    if (cJSON_IsString(g) && strcmp(g->valuestring, "present") == 0) {
        return NT_DEVAPI_GATE_PRESENT;
    }
    return NT_DEVAPI_GATE_FRAMES;
}

static bool cmd_defer(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(params, "amount");
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(params, "tag");
    s_tag = cJSON_IsNumber(t) ? t->valueint : 7;
    nt_devapi_handler_fn reply = ud != NULL ? reply_fail : reply_probe;
    return nt_devapi_defer(parse_gate(params), cJSON_IsNumber(a) ? a->valuedouble : 3.0, reply, err);
}

static bool cmd_echo(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    devapi_add_bool(result, "echo", true);
    return true;
}

static const nt_devapi_command k_cmds[] = {
    {.method = "test.defer", .summary = "test-only command", .params_shape = "{}", .result_shape = "{}", .fn = cmd_defer},
    {.method = "test.echo", .summary = "test-only command", .params_shape = "{}", .result_shape = "{}", .fn = cmd_echo},
};
static const nt_devapi_command k_fail_cmds[] = {
    {.method = "test.defer_fail", .summary = "test-only command", .params_shape = "{}", .result_shape = "{}", .fn = cmd_defer},
};

static int s_fail_marker;

void setUp(void) {
    g_nt_app = (nt_app_t){.max_dt = 0.1F, .scale = 1.0F, .step_dt = 1.0F / 60.0F, .render_enabled = true};
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_devapi_register(k_cmds, NT_COUNTOF(k_cmds), NULL);
    nt_devapi_register(k_fail_cmds, NT_COUNTOF(k_fail_cmds), &s_fail_marker);
}

void tearDown(void) { nt_devapi_shutdown(); }

/* One host tick of the devapi: nt_devapi_update resolves the pending gate, then the drain yields it. */
static const char *drain(void) {
    nt_devapi_update();
    return nt_devapi_poll_response();
}

/* One loop iteration as nt_app_run does it (RUN: wall dt unless paused; MANUAL: one queued step), then the drain. */
static const char *tick(void) {
    float dt = 0.0F;
    bool advanced = false;
    if (g_nt_app.mode == NT_APP_MODE_RUN) {
        if (!g_nt_app.paused) {
            dt = (1.0F / 60.0F) * g_nt_app.scale;
            advanced = true;
        }
    } else if (g_nt_app.pending_steps > 0) {
        g_nt_app.pending_steps--;
        dt = g_nt_app.step_dt;
        advanced = true;
    }
    g_nt_app.dt = dt;
    g_nt_app.time += (double)dt;
    if (advanced) {
        g_nt_app.frame++;
    }
    return drain();
}

/* Parse a reply line; the caller deletes the tree. */
static cJSON *parse(const char *line) {
    TEST_ASSERT_NOT_NULL(line);
    cJSON *root = cJSON_Parse(line);
    TEST_ASSERT_NOT_NULL(root);
    return root;
}

static void assert_error(const char *line, const char *code, int request_id) {
    cJSON *root = parse(line);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_EQUAL_STRING(code, cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "error"), "code")->valuestring);
    TEST_ASSERT_EQUAL_INT(request_id, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
}

static void assert_probe(const char *line, int tag, int request_id) {
    cJSON *root = parse(line);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_EQUAL_INT(tag, cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "result"), "tag")->valueint);
    TEST_ASSERT_EQUAL_INT(request_id, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
}

// #region frames / seconds / ids
static void test_frames_gate_yields_after_k_frames(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":7,\"params\":{\"amount\":3,\"tag\":5}}"));
    TEST_ASSERT_NULL(tick());
    TEST_ASSERT_NULL(tick());
    assert_probe(tick(), 5, 7);
    TEST_ASSERT_NULL(drain());
}

static void test_zero_frames_resolves_on_next_drain(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":0}}"));
    nt_app_pause(); /* frozen, but the target is already reached: ready wins */
    assert_probe(drain(), 7, 1);
}

static void test_seconds_gate_yields_on_game_time(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":2,\"params\":{\"gate\":\"seconds\",\"amount\":0.05}}"));
    TEST_ASSERT_NULL(tick());   /* 1/60 */
    TEST_ASSERT_NULL(tick());   /* 2/60 */
    assert_probe(tick(), 7, 2); /* 3/60 >= 0.05 */
}

static void test_string_and_absent_ids(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":\"abc\",\"params\":{\"amount\":1}}"));
    cJSON *root = parse(tick());
    TEST_ASSERT_EQUAL_STRING("abc", cJSON_GetObjectItemCaseSensitive(root, "request_id")->valuestring);
    cJSON_Delete(root);

    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"params\":{\"amount\":1}}"));
    root = parse(tick());
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "request_id"));
    cJSON_Delete(root);
}

static void test_reply_failure_is_its_error(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer_fail\",\"request_id\":4,\"params\":{\"amount\":1}}"));
    assert_error(tick(), "probe_failed", 4);
}
// #endregion

// #region one reply in flight
static void test_busy_while_pending(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":2}}"));
    assert_error(nt_devapi_submit("{\"method\":\"test.echo\",\"request_id\":2}"), "busy", 2);
    assert_error(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":3}"), "busy", 3);
    TEST_ASSERT_NULL(tick());
    assert_probe(tick(), 7, 1); /* the first reply is untouched by the rejected ones */
    cJSON_Delete(parse(nt_devapi_submit("{\"method\":\"test.echo\"}")));
}

/* A reply that is resolved but not yet drained still holds the slot. */
static void test_busy_until_drained(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":0}}"));
    nt_devapi_update();
    assert_error(nt_devapi_submit("{\"method\":\"test.echo\",\"request_id\":2}"), "busy", 2);
    assert_probe(nt_devapi_poll_response(), 7, 1);
}

/* The drain only hands out what nt_devapi_update resolved: a freeze between ticks is not seen at poll time. */
static void test_poll_alone_does_not_resolve(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":0}}"));
    TEST_ASSERT_NULL(nt_devapi_poll_response());
    nt_app_pause();
    assert_probe(drain(), 7, 1); /* ready wins over the frozen sim */
}

static void test_batch_rejects_deferral_and_runs_the_rest(void) {
    cJSON *root = parse(nt_devapi_submit("[{\"method\":\"test.defer\",\"request_id\":1},{\"method\":\"test.echo\",\"request_id\":2}]"));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(root));
    cJSON *e0 = cJSON_GetArrayItem(root, 0);
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(e0, "error"), "code")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(root, 1), "ok")));
    cJSON_Delete(root);
    TEST_ASSERT_NULL(tick()); /* nothing pending */
    cJSON_Delete(parse(nt_devapi_submit("{\"method\":\"test.echo\"}")));
}

static void test_batch_while_pending_is_busy_per_entry(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":5}}"));
    cJSON *root = parse(nt_devapi_submit("[{\"method\":\"test.echo\",\"request_id\":2},{\"method\":\"test.echo\",\"request_id\":3}]"));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(root));
    for (int i = 0; i < 2; i++) {
        cJSON *e = cJSON_GetArrayItem(root, i);
        TEST_ASSERT_EQUAL_STRING("busy", cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(e, "error"), "code")->valuestring);
        TEST_ASSERT_EQUAL_INT(2 + i, cJSON_GetObjectItemCaseSensitive(e, "request_id")->valueint);
    }
    cJSON_Delete(root);
}
// #endregion

// #region present gate
static void test_present_yields_after_the_seam(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":9,\"params\":{\"gate\":\"present\",\"tag\":11}}"));
    nt_app_pause();
    TEST_ASSERT_NULL(drain()); /* a frozen sim does not stall a present gate */
    nt_devapi_run_pre_swap_reply();
    nt_devapi_run_pre_swap_reply(); /* a second swap this frame keeps the first reply */
    assert_probe(drain(), 11, 9);
    nt_devapi_run_pre_swap_reply(); /* nothing pending: no-op */
    TEST_ASSERT_NULL(drain());
}

static void test_present_reply_built_before_render_off_still_yields(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":9,\"params\":{\"gate\":\"present\"}}"));
    nt_devapi_run_pre_swap_reply();
    nt_app_set_render_enabled(false);
    assert_probe(drain(), 7, 9);
}
// #endregion

// #region stalled
static void test_frames_stall_on_pause(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":3}}"));
    TEST_ASSERT_NULL(tick());
    nt_app_pause(); /* game code pauses between drains */
    assert_error(tick(), "stalled", 1);
    cJSON_Delete(parse(nt_devapi_submit("{\"method\":\"test.echo\"}"))); /* slot freed */
}

static void test_frames_stall_on_mode_switch(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":3}}"));
    nt_app_set_mode(NT_APP_MODE_MANUAL);
    nt_app_step(10); /* the new mode advances, but not the one the reply waits in */
    assert_error(tick(), "stalled", 1);
}

static void test_frames_stall_in_idle_manual(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    nt_app_step(1);
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":2}}"));
    assert_error(tick(), "stalled", 1); /* the one queued step ran: frame 1 of 2, nothing left to advance */
}

/* The last queued step reaches the target and empties the queue: ready wins. */
static void test_last_manual_step_resolves(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    nt_app_step(2);
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":2}}"));
    TEST_ASSERT_NULL(tick());
    g_nt_app.pending_steps--; /* crunch: the loop runs the last step, the drain comes after an idle iteration */
    g_nt_app.frame++;
    assert_probe(tick(), 7, 1);
}

static void test_seconds_stall_on_scale_zero_frames_do_not(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"gate\":\"seconds\",\"amount\":1}}"));
    nt_app_set_scale(0.0F);
    assert_error(tick(), "stalled", 1);

    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":2,\"params\":{\"amount\":2}}"));
    TEST_ASSERT_NULL(tick()); /* RUN at scale 0 still advances the frame */
    assert_probe(tick(), 7, 2);
}

static void test_present_stalls_with_render_off(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":3,\"params\":{\"gate\":\"present\"}}"));
    nt_app_set_render_enabled(false);
    assert_error(drain(), "stalled", 3);
}
// #endregion

// #region reset
static void test_reset_frees_pending_and_built(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":1,\"params\":{\"amount\":5}}"));
    nt_devapi_deferred_reset(); /* owned id freed (LSan) */
    TEST_ASSERT_NULL(drain());

    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"test.defer\",\"request_id\":2,\"params\":{\"gate\":\"present\"}}"));
    nt_devapi_run_pre_swap_reply();
    nt_devapi_deferred_reset(); /* built entry freed (LSan) */
    TEST_ASSERT_NULL(drain());
    cJSON_Delete(parse(nt_devapi_submit("{\"method\":\"test.echo\"}")));
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_frames_gate_yields_after_k_frames);
    RUN_TEST(test_zero_frames_resolves_on_next_drain);
    RUN_TEST(test_seconds_gate_yields_on_game_time);
    RUN_TEST(test_string_and_absent_ids);
    RUN_TEST(test_reply_failure_is_its_error);
    RUN_TEST(test_busy_while_pending);
    RUN_TEST(test_busy_until_drained);
    RUN_TEST(test_poll_alone_does_not_resolve);
    RUN_TEST(test_batch_rejects_deferral_and_runs_the_rest);
    RUN_TEST(test_batch_while_pending_is_busy_per_entry);
    RUN_TEST(test_present_yields_after_the_seam);
    RUN_TEST(test_present_reply_built_before_render_off_still_yields);
    RUN_TEST(test_frames_stall_on_pause);
    RUN_TEST(test_frames_stall_on_mode_switch);
    RUN_TEST(test_frames_stall_in_idle_manual);
    RUN_TEST(test_last_manual_step_resolves);
    RUN_TEST(test_seconds_stall_on_scale_zero_frames_do_not);
    RUN_TEST(test_present_stalls_with_render_off);
    RUN_TEST(test_reset_frees_pending_and_built);
    return UNITY_END();
}
