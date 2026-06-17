/* L2 devapi time/render/frame group via submit() (no socket): discovery lists the new commands
   with shapes (TIME-07), thin handlers forward to the nt_app L1 API, every bot-input range
   violation returns bad_params (TIME-05), render.info reports {enabled,draw_calls} (TIME-04 L2),
   frame.current reports {frame,time,dt} (D-14), and the highest-value D-11 test: frame.wait{frames:N}
   yields after exactly N sim-advances and NEVER during PAUSE. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "app/nt_app.h"
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

void setUp(void) {
    /* Known app state so forwarding + frame.current assertions are deterministic. */
    g_nt_app = (nt_app_t){.max_dt = 0.1F, .scale = 1.0F, .step_dt = 1.0F / 60.0F, .render_enabled = true};
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
}

void tearDown(void) { nt_devapi_shutdown(); }

/* ---- helpers (clone of test_devapi_deferred.c cadence) ---- */

/* One sim-advance under the managed-tick contract: tick the deferred queue once, then read the
   ready slot. The managed loop calls nt_devapi_managed_sim_tick() exactly once per sim-advance. */
static const char *advance_sim(void) {
    nt_devapi_managed_sim_tick();
    return nt_devapi_poll_response();
}

/* Parse `resp`, assert ok:false + error.code == "bad_params", free the tree. */
static void assert_bad_params(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(err, "code")->valuestring);
    cJSON_Delete(root);
}

/* Parse `resp`, assert ok:true, return the (borrowed) root — caller deletes. */
static cJSON *parse_ok(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    return root;
}

static bool endpoints_has_method_with_shapes(const char *method) {
    const char *resp = nt_devapi_submit("{\"method\":\"endpoints\",\"params\":{\"detail\":true}}");
    cJSON *root = parse_ok(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *commands = cJSON_GetObjectItemCaseSensitive(result, "commands");
    bool found = false;
    cJSON *cmd = NULL;
    cJSON_ArrayForEach(cmd, commands) {
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(cmd, "method");
        if (cJSON_IsString(m) && strcmp(m->valuestring, method) == 0) {
            const cJSON *ps = cJSON_GetObjectItemCaseSensitive(cmd, "params_shape");
            const cJSON *rs = cJSON_GetObjectItemCaseSensitive(cmd, "result_shape");
            found = cJSON_IsString(ps) && strlen(ps->valuestring) > 0 && cJSON_IsString(rs) && strlen(rs->valuestring) > 0;
            break;
        }
    }
    cJSON_Delete(root);
    return found;
}

/* ---- discovery (TIME-07) ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_discovery_lists_new_group(void) {
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.pause"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.resume"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.step"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.set_scale"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.set_mode"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.set_fps"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("render.set_enabled"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("render.info"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("frame.current"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("frame.wait"));
}

static void test_command_describe_time_step(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"time.step\"}}");
    cJSON *root = parse_ok(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_STRING("time", cJSON_GetObjectItemCaseSensitive(result, "group")->valuestring);
    const cJSON *ps = cJSON_GetObjectItemCaseSensitive(result, "params_shape");
    TEST_ASSERT_TRUE(cJSON_IsString(ps) && strlen(ps->valuestring) > 0);
    cJSON_Delete(root);
}

/* ---- forwarding (TIME-07 / TIME-04 L2) ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_forwarding_to_nt_app(void) {
    cJSON *root = NULL;

    root = parse_ok(nt_devapi_submit("{\"method\":\"time.pause\"}"));
    TEST_ASSERT_TRUE(g_nt_app.paused);
    cJSON_Delete(root);

    root = parse_ok(nt_devapi_submit("{\"method\":\"time.resume\"}"));
    TEST_ASSERT_FALSE(g_nt_app.paused);
    cJSON_Delete(root);

    root = parse_ok(nt_devapi_submit("{\"method\":\"time.set_mode\",\"params\":{\"mode\":\"manual\"}}"));
    TEST_ASSERT_EQUAL_INT(NT_APP_MODE_MANUAL, g_nt_app.mode);
    cJSON_Delete(root);

    root = parse_ok(nt_devapi_submit("{\"method\":\"time.set_mode\",\"params\":{\"mode\":\"run\"}}"));
    TEST_ASSERT_EQUAL_INT(NT_APP_MODE_RUN, g_nt_app.mode);
    cJSON_Delete(root);

    root = parse_ok(nt_devapi_submit("{\"method\":\"time.set_scale\",\"params\":{\"scale\":2.0}}"));
    TEST_ASSERT_TRUE(g_nt_app.scale > 1.99F && g_nt_app.scale < 2.01F);
    cJSON_Delete(root);

    /* set_fps writes target_dt directly: fps=60 -> 1/60, fps=0 -> uncapped (0). */
    root = parse_ok(nt_devapi_submit("{\"method\":\"time.set_fps\",\"params\":{\"fps\":60}}"));
    TEST_ASSERT_TRUE(g_nt_app.target_dt > (1.0F / 60.0F - 1e-5F) && g_nt_app.target_dt < (1.0F / 60.0F + 1e-5F));
    cJSON_Delete(root);

    root = parse_ok(nt_devapi_submit("{\"method\":\"time.set_fps\",\"params\":{\"fps\":0}}"));
    float zero = 0.0F;
    TEST_ASSERT_EQUAL_MEMORY(&zero, &g_nt_app.target_dt, sizeof(float)); /* exactly uncapped */
    cJSON_Delete(root);

    /* time.step in MANUAL queues advances on g_nt_app.pending_steps. */
    (void)parse_ok(nt_devapi_submit("{\"method\":\"time.set_mode\",\"params\":{\"mode\":\"manual\"}}"));
    root = parse_ok(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":3}}"));
    TEST_ASSERT_EQUAL_INT(3, g_nt_app.pending_steps);
    cJSON_Delete(root);

    /* time.step default count = 1 when omitted. */
    root = parse_ok(nt_devapi_submit("{\"method\":\"time.step\"}"));
    TEST_ASSERT_EQUAL_INT(4, g_nt_app.pending_steps);
    cJSON_Delete(root);
}

/* ---- render.* (TIME-04 L2 / D-03) ---- */

static void test_render_info_reflects_flag(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"render.set_enabled\",\"params\":{\"enabled\":false}}"));
    TEST_ASSERT_FALSE(nt_app_render_enabled());
    cJSON_Delete(root);

    root = parse_ok(nt_devapi_submit("{\"method\":\"render.info\"}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result, "enabled")));
    /* no draw happened in this headless test -> draw_calls == 0 (nt_stats_stub). */
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetObjectItemCaseSensitive(result, "draw_calls")->valueint);
    cJSON_Delete(root);
}

/* ---- frame.current (D-14) ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_frame_current_matches_app(void) {
    g_nt_app.frame = 42;
    g_nt_app.time = 1.5F;
    g_nt_app.dt = 1.0F / 60.0F;

    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"frame.current\"}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(42, cJSON_GetObjectItemCaseSensitive(result, "frame")->valueint);
    double t = cJSON_GetObjectItemCaseSensitive(result, "time")->valuedouble;
    TEST_ASSERT_TRUE(t > 1.49 && t < 1.51);
    double dt = cJSON_GetObjectItemCaseSensitive(result, "dt")->valuedouble;
    TEST_ASSERT_TRUE(dt > (1.0 / 60.0 - 1e-4) && dt < (1.0 / 60.0 + 1e-4));
    cJSON_Delete(root);
}

/* ---- bad_params (TIME-05 fail-fast, never assert, never spin) ---- */

static void test_bad_params_cases(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":0}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":\"x\"}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.set_fps\",\"params\":{\"fps\":-1}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.set_scale\",\"params\":{\"scale\":-1}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.set_mode\",\"params\":{\"mode\":\"bogus\"}}"));
}

/* frame.wait over the cap → structured bad_params (never spins, never enqueues a slot). */
static void test_frame_wait_over_cap_bad_params(void) {
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"frame.wait\",\"params\":{\"frames\":%d}}", NT_DEVAPI_FRAME_WAIT_MAX + 1);
    assert_bad_params(nt_devapi_submit(line));

    /* The rejected wait enqueued no slot: a sim-advance pops nothing. */
    TEST_ASSERT_NULL(advance_sim());
}

/* CR-02: time.step{count} over the cap → bad_params, no backlog queued. cJSON clamps huge JSON
   numbers to INT_MAX, so an uncapped count would wedge the host for billions of frames. */
static void test_step_over_cap_bad_params(void) {
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"time.step\",\"params\":{\"count\":%lld}}", (long long)NT_DEVAPI_STEP_MAX + 1);
    assert_bad_params(nt_devapi_submit(line));
    TEST_ASSERT_EQUAL_INT(0, g_nt_app.pending_steps); /* rejected → nothing queued */
}

/* CR-01: a finite-but-tiny fps must fail fast, never store a +inf frame cap (which would spin-wait
   the managed loop forever). target_dt must stay exactly its prior value (0 = uncapped from setUp). */
static void test_set_fps_tiny_underflow_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.set_fps\",\"params\":{\"fps\":1e-50}}"));
    float zero = 0.0F;
    TEST_ASSERT_EQUAL_MEMORY(&zero, &g_nt_app.target_dt, sizeof(float)); /* unchanged, not +inf */
}

/* ---- D-11: yield after exactly N sim-advances (the highest-value test) ---- */

#define D11_WAIT_FRAMES 3

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_frame_wait_yields_after_n_sim_advances(void) {
    /* submit frame.wait{frames:N} → deferred (submit returns NULL). */
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"request_id\":7,\"params\":{\"frames\":3}}"));

    /* N-1 sim-advances: still pending. */
    for (int i = 0; i < D11_WAIT_FRAMES - 1; i++) {
        TEST_ASSERT_NULL(advance_sim());
    }

    /* The N-th sim-advance yields {ok:true,result:{deferred:true},request_id:7}. */
    const char *resp = advance_sim();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "deferred")));
    TEST_ASSERT_EQUAL_INT(7, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);

    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* drained */
}

/* D-11: PAUSE never advances the sim, so the managed loop never calls nt_devapi_managed_sim_tick().
   A frame.wait submitted while paused NEVER yields, no matter how many idle loop iterations run. */
static void test_frame_wait_never_yields_during_pause(void) {
    nt_app_pause();
    TEST_ASSERT_TRUE(g_nt_app.paused);

    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"request_id\":9,\"params\":{\"frames\":3}}"));

    /* Drive many idle loop iterations: paused -> no sim-advance -> NO tick -> never yields. */
    for (int i = 0; i < 100; i++) {
        /* The paused managed loop runs the frame fn but does NOT call managed_sim_tick. */
        TEST_ASSERT_NULL(nt_devapi_poll_response());
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_discovery_lists_new_group);
    RUN_TEST(test_command_describe_time_step);
    RUN_TEST(test_forwarding_to_nt_app);
    RUN_TEST(test_render_info_reflects_flag);
    RUN_TEST(test_frame_current_matches_app);
    RUN_TEST(test_bad_params_cases);
    RUN_TEST(test_frame_wait_over_cap_bad_params);
    RUN_TEST(test_step_over_cap_bad_params);
    RUN_TEST(test_set_fps_tiny_underflow_bad_params);
    RUN_TEST(test_frame_wait_yields_after_n_sim_advances);
    RUN_TEST(test_frame_wait_never_yields_during_pause);
    return UNITY_END();
}
