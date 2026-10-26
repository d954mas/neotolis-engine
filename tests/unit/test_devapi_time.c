/* L2 devapi time/render/frame group via submit() (no socket): discovery lists the commands with
   shapes, thin handlers forward to the nt_app L1 API, every bot-input range violation returns
   bad_params, render.info reports {enabled,draw_calls}, frame.current reports {frame,time,dt}, and
   the highest-value test: frame.wait{frames:N} yields after exactly N sim-advances and fails as stalled on a pause. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdint.h>
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
    nt_devapi_register_time();
    nt_devapi_register_discovery(); /* the test verifies time.* via endpoints / command.describe */
}

void tearDown(void) { nt_devapi_shutdown(); }

/* ---- helpers (clone of test_devapi_deferred.c cadence) ---- */

/* One host tick of the devapi: nt_devapi_update resolves the pending gate, then the drain yields it. */
static const char *drain(void) {
    nt_devapi_update();
    return nt_devapi_poll_response();
}

/* One loop iteration as nt_app_run does it (RUN: wall dt unless paused; MANUAL: one queued step), then the drain. */
static const char *advance_sim(void) {
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

/* Advance game time by `dt` seconds (what the RUN loop does each frame), then drain any slot whose
   game-time deadline was reached. For time.wait (time-based slots), not frame-based ones. */
static const char *advance_time(double dt) {
    g_nt_app.time += dt;
    return drain();
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

/* Parse `resp`, assert ok:false + error.code == code, free the tree. */
static void assert_error_code(const char *resp, const char *code) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_EQUAL_STRING(code, cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "error"), "code")->valuestring);
    cJSON_Delete(root);
}

/* Assert a clock reply: ok, the request_id, and result.frame == frame. */
static void assert_clock_reply(const char *resp, int request_id, uint32_t frame) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_UINT32(frame, (uint32_t)cJSON_GetObjectItemCaseSensitive(result, "frame")->valuedouble);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(result, "time")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(result, "dt")));
    TEST_ASSERT_EQUAL_INT(request_id, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
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

/* ---- discovery ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_discovery_lists_new_group(void) {
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.pause"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.resume"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.step"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.set_scale"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.set_mode"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.set_fps"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("time.wait"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("render.set_enabled"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("render.info"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("frame.current"));
    TEST_ASSERT_TRUE(endpoints_has_method_with_shapes("frame.wait"));
}

static void test_command_describe_time_step(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"command.describe\",\"params\":{\"method\":\"time.step\"}}");
    cJSON *root = parse_ok(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_STRING("time.step", cJSON_GetObjectItemCaseSensitive(result, "method")->valuestring);
    const cJSON *ps = cJSON_GetObjectItemCaseSensitive(result, "params_shape");
    TEST_ASSERT_TRUE(cJSON_IsString(ps) && strlen(ps->valuestring) > 0);
    cJSON_Delete(root);
}

/* ---- forwarding ---- */

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
    /* time.step forwarding/queueing is covered by test_step_deferred_resolves_after_count below
       (it is now a DEFERRED command: submit() returns NULL, not an immediate ok envelope). */
}

/* time.step is DEFERRED (lockstep contract): it queues advances on pending_steps immediately, but
   holds the reply until exactly `count` sim-advances have completed — so the reply's frame is the
   submit frame plus count. */
static void test_step_deferred_resolves_after_count(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"time.set_mode\",\"params\":{\"mode\":\"manual\"}}")));

    /* default count = 1 when omitted: queues 1, defers, resolves on the 1st sim-advance. */
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"request_id\":20}"));
    TEST_ASSERT_EQUAL_INT(1, g_nt_app.pending_steps); /* queued immediately */
    assert_clock_reply(advance_sim(), 20, 1);

    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"request_id\":21,\"params\":{\"count\":3}}"));
    TEST_ASSERT_EQUAL_INT(3, g_nt_app.pending_steps);
    for (int i = 0; i < 2; i++) {
        TEST_ASSERT_NULL(advance_sim()); /* advances 1 and 2: still pending */
    }
    assert_clock_reply(advance_sim(), 21, 4);
    TEST_ASSERT_NULL(drain()); /* drained */
}

/* Steps the game already queued run first: the reply waits for them too. */
static void test_step_waits_for_already_queued_steps(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    nt_app_step(2);
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"request_id\":22,\"params\":{\"count\":1}}"));
    TEST_ASSERT_NULL(advance_sim());
    TEST_ASSERT_NULL(advance_sim());
    assert_clock_reply(advance_sim(), 22, 3);
}

/* A backlog that would push the deadline past the wrap-safe range is rejected before any side effect. */
static void test_step_backlog_overflow_bad_params(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    g_nt_app.pending_steps = INT32_MAX - 2;
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":5}}"));
    TEST_ASSERT_EQUAL_INT(INT32_MAX - 2, g_nt_app.pending_steps);
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"frame.current\"}"))); /* nothing pending */
}

/* ---- render.* ---- */

static void test_render_info_reflects_flag(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"render.set_enabled\",\"params\":{\"enabled\":false}}"));
    TEST_ASSERT_FALSE(nt_app_render_enabled());
    cJSON_Delete(root);

    root = parse_ok(nt_devapi_submit("{\"method\":\"render.info\"}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result, "enabled")));
    /* no draw happened in this headless test -> draw_calls == 0 (nt_gfx_fake returns 0). */
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetObjectItemCaseSensitive(result, "draw_calls")->valueint);
    cJSON_Delete(root);
}

/* ---- frame.current ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_frame_current_matches_app(void) {
    g_nt_app.frame = 42;
    g_nt_app.time = 1.5;
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

/* ---- bad_params (fail-fast, never assert, never spin) ---- */

static void test_bad_params_cases(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":0}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":\"x\"}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.set_fps\",\"params\":{\"fps\":-1}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.set_scale\",\"params\":{\"scale\":-1}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.set_mode\",\"params\":{\"mode\":\"bogus\"}}"));
}

/* frames is bounded only by the wrap-safe frame compare: INT32_MAX + 1 is bad_params with no slot, INT32_MAX defers. */
static void test_frame_wait_int32_bound(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"frame.wait\",\"params\":{\"frames\":2147483648}}"));
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"params\":{\"frames\":2147483647}}"));
    TEST_ASSERT_NULL(advance_sim());
}

/* frame.wait{frames:0}: target == current frame, so it resolves on the very next drain with no
   sim-advance (the immediate-ack form). */
static void test_frame_wait_zero_resolves_immediately(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"request_id\":5,\"params\":{\"frames\":0}}"));
    assert_clock_reply(drain(), 5, 0); /* ready immediately, no g_nt_app.frame advance */
    TEST_ASSERT_NULL(drain());         /* drained */
}

/* The reply is read the tick its gate opens: later frames before the drain (web polls between rAFs) do not
   change it, so native and web report the same frame. */
static void test_clock_reply_is_read_when_the_gate_opens(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"request_id\":6,\"params\":{\"frames\":1}}"));
    g_nt_app.frame++;
    nt_devapi_update();
    g_nt_app.frame += 2;
    assert_clock_reply(nt_devapi_poll_response(), 6, 1);
}

/* frames is a frame count: a fractional value is rejected, not truncated. */
static void test_frame_wait_fractional_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"frame.wait\",\"params\":{\"frames\":1.7}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"frame.wait\",\"params\":{\"frames\":-1}}"));
}

/* A step count is bounded only by the int queue: past INT32_MAX steps is bad_params, INT32_MAX itself
   queues (a rejected step queued nothing, or the accepted one would be "too many steps already queued").
   MANUAL, so the mode check cannot mask the range check. */
static void test_step_count_int32_bound(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":2147483648}}"));
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":2147483647}}"));
    TEST_ASSERT_EQUAL_INT(INT32_MAX, g_nt_app.pending_steps);
}

/* The seconds sugar hits the same bound before its int cast; step_dt 1 makes seconds == steps. */
/* Any positive seconds steps at least once: a quotient that underflows to 0 still queues one step. */
static void test_step_seconds_tiny_steps_once(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    g_nt_app.step_dt = 2.0F;
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"seconds\":5e-324}}"));
    TEST_ASSERT_EQUAL_INT(1, g_nt_app.pending_steps);
}

static void test_step_seconds_int32_bound(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    g_nt_app.step_dt = 1.0F;
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"seconds\":2147483648}}"));
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"seconds\":2147483647}}"));
    TEST_ASSERT_EQUAL_INT(INT32_MAX, g_nt_app.pending_steps);
}

/* a finite-but-tiny fps must fail fast, never store a +inf frame cap (which would spin-wait the
   loop forever). target_dt must stay exactly its prior value (0 = uncapped from setUp). */
static void test_set_fps_tiny_underflow_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.set_fps\",\"params\":{\"fps\":1e-50}}"));
    float zero = 0.0F;
    TEST_ASSERT_EQUAL_MEMORY(&zero, &g_nt_app.target_dt, sizeof(float)); /* unchanged, not +inf */
}

/* time.step is MANUAL-only: in RUN (setUp default) it must fail fast with bad_params and queue
   nothing — otherwise RUN never drains the steps and RUN+pause would hang the deferred reply. */
static void test_step_requires_manual_mode(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":5}}"));
    TEST_ASSERT_EQUAL_INT(0, g_nt_app.pending_steps); /* rejected before any side effect */
}

/* set_scale: a huge finite double narrows to +inf in float — reject so it can't
   poison g_nt_app.time/dt. scale stays at its setUp default (1.0). */
static void test_set_scale_huge_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.set_scale\",\"params\":{\"scale\":1e300}}"));
    TEST_ASSERT_TRUE(g_nt_app.scale > 0.99F && g_nt_app.scale < 1.01F); /* unchanged */
}

/* ---- yield after exactly N sim-advances (the highest-value test) ---- */

#define WAIT_FRAMES 3

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_frame_wait_yields_after_n_sim_advances(void) {
    /* submit frame.wait{frames:N} → deferred (submit returns NULL). */
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"request_id\":7,\"params\":{\"frames\":3}}"));

    /* N-1 sim-advances: still pending. */
    for (int i = 0; i < WAIT_FRAMES - 1; i++) {
        TEST_ASSERT_NULL(advance_sim());
    }

    /* The N-th sim-advance yields {frame,time,dt} with the request_id. */
    assert_clock_reply(advance_sim(), 7, WAIT_FRAMES);
    TEST_ASSERT_NULL(drain()); /* drained */
}

/* A freeze after submit, from game code or the host, fails the wait as stalled instead of hanging it. */
static void test_frame_wait_stalls_when_game_pauses(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"params\":{\"frames\":3}}"));
    TEST_ASSERT_NULL(advance_sim());
    nt_app_pause();
    assert_error_code(advance_sim(), "stalled");
}

static void test_frame_wait_stalls_when_game_switches_to_manual(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"params\":{\"frames\":3}}"));
    nt_app_set_mode(NT_APP_MODE_MANUAL);
    nt_app_step(10);
    assert_error_code(advance_sim(), "stalled");
}

/* A step whose queue a mode switch dropped fails as stalled, even when RUN frames pass its target. */
static void test_step_last_step_switched_to_run_stalls(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":1}}"));
    nt_app_set_mode(NT_APP_MODE_RUN); /* game code, before the step runs */
    assert_error_code(advance_sim(), "stalled");
}

/* A step whose queue a mode switch dropped fails as stalled. */
static void test_step_stalls_when_mode_switches_to_run(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":100}}"));
    TEST_ASSERT_NULL(advance_sim());
    nt_app_set_mode(NT_APP_MODE_RUN);
    TEST_ASSERT_EQUAL_INT(0, g_nt_app.pending_steps); /* the switch dropped the queued steps */
    assert_error_code(advance_sim(), "stalled");
}

static void test_time_wait_stalls_when_game_freezes_time(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.wait\",\"params\":{\"seconds\":1.0}}"));
    nt_app_set_scale(0.0F);
    assert_error_code(advance_sim(), "stalled");
}

/* One reply in flight: the bot cannot pause or switch mode under a pending wait; it gets busy, nothing changes. */
static void test_mutators_busy_while_reply_pending(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"params\":{\"frames\":3}}"));
    assert_error_code(nt_devapi_submit("{\"method\":\"time.pause\"}"), "busy");
    assert_error_code(nt_devapi_submit("{\"method\":\"time.set_mode\",\"params\":{\"mode\":\"manual\"}}"), "busy");
    TEST_ASSERT_FALSE(g_nt_app.paused);
    TEST_ASSERT_EQUAL_INT(NT_APP_MODE_RUN, g_nt_app.mode);
}

/* frame.wait is RUN-only: under PAUSE (frame frozen) and in MANUAL (frame advances only on
   time.step) a wait could never resolve, so it is rejected fail-fast instead of registering a
   slot that would block the caller. */
static void test_frame_wait_rejected_when_not_running(void) {
    nt_app_pause();
    assert_bad_params(nt_devapi_submit("{\"method\":\"frame.wait\",\"params\":{\"frames\":3}}"));
    nt_app_resume();

    g_nt_app.mode = NT_APP_MODE_MANUAL;
    assert_bad_params(nt_devapi_submit("{\"method\":\"frame.wait\",\"params\":{\"frames\":3}}"));

    /* Neither rejected wait registered a slot: a sim-advance pops nothing. */
    TEST_ASSERT_NULL(advance_sim());
}

/* ---- end-to-end: the REAL nt_app_run(MANUAL) loop drains time.step ---- */
/* The deferred-resolve tests above hand-bump g_nt_app.frame via advance_sim(). This drives the
   actual loop so the loop itself drains pending_steps and advances the frame — the integration
   that the live one-step-per-poll bug slipped through. */
#define E2E_STEP_COUNT 5
#define E2E_ITER_CAP 64 /* safety net: a drain bug must FAIL the test, never hang it */

static const char *s_e2e_resp; /* deferred reply captured in-loop (dispatch-core buffer; parse before next call) */
static int s_e2e_iters;
static uint32_t s_e2e_start_frame;

static void e2e_frame_fn(void) {
    if (s_e2e_iters == 0) {
        /* submit from inside the live loop: queues pending_steps + defers, target = frame + count. */
        s_e2e_start_frame = g_nt_app.frame;
        TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"request_id\":31,\"params\":{\"count\":5}}"));
    }
    s_e2e_iters++;
    const char *r = drain(); /* the host drains the deferred queue each tick */
    if (r != NULL) {
        s_e2e_resp = r;
        nt_app_quit();
        return;
    }
    if (s_e2e_iters >= E2E_ITER_CAP) {
        nt_app_quit(); /* drain bug → bail so asserts fail, not an infinite loop */
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_step_drains_through_real_loop(void) {
    s_e2e_resp = NULL;
    s_e2e_iters = 0;
    g_nt_app.mode = NT_APP_MODE_MANUAL;

    nt_app_run(e2e_frame_fn);

    /* The loop (not the test) advanced the frame by exactly count, and fully drained the backlog. */
    TEST_ASSERT_EQUAL_UINT32(s_e2e_start_frame + E2E_STEP_COUNT, g_nt_app.frame);
    TEST_ASSERT_EQUAL_INT(0, g_nt_app.pending_steps);
    TEST_ASSERT_NOT_NULL(s_e2e_resp); /* resolved before the iter cap */
    cJSON *root = cJSON_Parse(s_e2e_resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_EQUAL_INT(31, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
    TEST_ASSERT_NULL(drain()); /* nothing left queued */
}

/* ---- time.wait (game-time deadline; the RUN-only sibling of frame.wait) ---- */

/* RUN, scale 1: time.wait{seconds} resolves once g_nt_app.time crosses the deadline, NOT on a frame
   count — proves the time-based deferred path. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_time_wait_yields_after_seconds(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.wait\",\"request_id\":11,\"params\":{\"seconds\":0.5}}"));
    TEST_ASSERT_NULL(advance_time(0.2));          /* time 0.2 < 0.5 */
    TEST_ASSERT_NULL(advance_time(0.2));          /* time 0.4 < 0.5 */
    assert_clock_reply(advance_time(0.2), 11, 0); /* time 0.6 >= 0.5 -> resolves */
    TEST_ASSERT_NULL(drain());
}

/* time.wait{seconds:0}: deadline == current time, so it resolves on the next drain with no time
   advance (immediate-ack), mirroring frame.wait{frames:0}. */
static void test_time_wait_zero_resolves_immediately(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.wait\",\"request_id\":13,\"params\":{\"seconds\":0}}"));
    const char *resp = drain(); /* ready immediately, no g_nt_app.time advance */
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_EQUAL_INT(13, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
    TEST_ASSERT_NULL(drain()); /* drained */
}

/* time.wait is RUN + scale>0 only: in MANUAL / paused / scale==0 game time is frozen, so a passive
   wait could never resolve. All three reject fail-fast and register no slot. scale==0 is the case
   frame.wait would NOT catch (the frame advances while time stays put). */
static void test_time_wait_rejected_when_time_frozen(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.wait\",\"params\":{\"seconds\":1.0}}"));
    g_nt_app.mode = NT_APP_MODE_RUN;

    nt_app_pause();
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.wait\",\"params\":{\"seconds\":1.0}}"));
    nt_app_resume();

    nt_app_set_scale(0.0F);
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.wait\",\"params\":{\"seconds\":1.0}}"));

    TEST_ASSERT_NULL(advance_time(2.0)); /* time 2.0 >> any 1.0s deadline -> proves none of the 3 registered a slot */
}

/* Any finite wait defers: the client, not the engine, decides how long it is willing to wait. */
static void test_time_wait_long_defers(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.wait\",\"params\":{\"seconds\":1e9}}"));
    TEST_ASSERT_NULL(advance_time(10.0));
}

/* time.step{seconds} sugar: ceil(seconds/step_dt) frames. step_dt = 1/60 (setUp), so 1.0s = 60
   frames; deferral stays FRAME-based (resolves after exactly that many sim-advances). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_time_step_seconds_converts_to_frames(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"request_id\":12,\"params\":{\"seconds\":1.0}}"));
    TEST_ASSERT_EQUAL_INT(60, g_nt_app.pending_steps);
    for (int i = 0; i < 59; i++) {
        TEST_ASSERT_NULL(advance_sim());
    }
    assert_clock_reply(advance_sim(), 12, 60);
    TEST_ASSERT_NULL(drain());
}

/* count and seconds are mutually exclusive; a sub-step duration still queues >= 1 frame (ceil). */
static void test_time_step_count_seconds_exclusive_and_ceil(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":2,\"seconds\":1.0}}"));
    TEST_ASSERT_EQUAL_INT(0, g_nt_app.pending_steps); /* rejected before any side effect */

    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"seconds\":0.001}}"));
    TEST_ASSERT_EQUAL_INT(1, g_nt_app.pending_steps); /* ceil(0.001 / (1/60)) == 1 */
}

static void test_time_step_fractional_count_has_no_side_effect(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":1.9}}"));
    TEST_ASSERT_EQUAL_INT(0, g_nt_app.pending_steps);
}

/* A second step while the first reply is pending is busy and queues nothing. */
static void test_step_busy_has_no_side_effect(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":1}}"));
    assert_error_code(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":1}}"), "busy");
    TEST_ASSERT_EQUAL_INT(1, g_nt_app.pending_steps);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_discovery_lists_new_group);
    RUN_TEST(test_command_describe_time_step);
    RUN_TEST(test_forwarding_to_nt_app);
    RUN_TEST(test_step_deferred_resolves_after_count);
    RUN_TEST(test_render_info_reflects_flag);
    RUN_TEST(test_frame_current_matches_app);
    RUN_TEST(test_bad_params_cases);
    RUN_TEST(test_frame_wait_int32_bound);
    RUN_TEST(test_frame_wait_zero_resolves_immediately);
    RUN_TEST(test_step_count_int32_bound);
    RUN_TEST(test_step_seconds_int32_bound);
    RUN_TEST(test_step_seconds_tiny_steps_once);
    RUN_TEST(test_set_fps_tiny_underflow_bad_params);
    RUN_TEST(test_step_requires_manual_mode);
    RUN_TEST(test_set_scale_huge_bad_params);
    RUN_TEST(test_frame_wait_yields_after_n_sim_advances);
    RUN_TEST(test_frame_wait_rejected_when_not_running);
    RUN_TEST(test_step_drains_through_real_loop);
    RUN_TEST(test_time_wait_yields_after_seconds);
    RUN_TEST(test_time_wait_zero_resolves_immediately);
    RUN_TEST(test_time_wait_rejected_when_time_frozen);
    RUN_TEST(test_time_wait_long_defers);
    RUN_TEST(test_time_step_seconds_converts_to_frames);
    RUN_TEST(test_time_step_count_seconds_exclusive_and_ceil);
    RUN_TEST(test_time_step_fractional_count_has_no_side_effect);
    RUN_TEST(test_step_busy_has_no_side_effect);
    RUN_TEST(test_step_waits_for_already_queued_steps);
    RUN_TEST(test_step_backlog_overflow_bad_params);
    RUN_TEST(test_frame_wait_fractional_bad_params);
    RUN_TEST(test_clock_reply_is_read_when_the_gate_opens);
    RUN_TEST(test_frame_wait_stalls_when_game_pauses);
    RUN_TEST(test_frame_wait_stalls_when_game_switches_to_manual);
    RUN_TEST(test_step_stalls_when_mode_switches_to_run);
    RUN_TEST(test_step_last_step_switched_to_run_stalls);
    RUN_TEST(test_time_wait_stalls_when_game_freezes_time);
    RUN_TEST(test_mutators_busy_while_reply_pending);
    return UNITY_END();
}
