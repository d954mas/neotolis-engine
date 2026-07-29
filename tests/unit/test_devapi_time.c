/* L2 devapi time/render/frame group via submit() (no socket): discovery lists the commands with
   shapes, thin handlers forward to the nt_app L1 API, every bot-input range violation returns
   bad_params, render.info reports {enabled,draw_calls}, frame.current reports {frame,time,dt}, and
   the highest-value test: frame.wait{frames:N} yields after exactly N sim-advances and NEVER during PAUSE. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "app/nt_app.h"
#include "devapi/nt_devapi_internal.h"
#include "devapi/nt_devapi_time_internal.h"
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

/* One game-frame advance: bump g_nt_app.frame (what the loop does on a sim-advance), then
   read any deferred slot whose game-frame deadline was reached. */
static const char *advance_sim(void) {
    g_nt_app.frame++;
    return nt_devapi_poll_response();
}

/* Advance game time by `dt` seconds (what the RUN loop does each frame), then drain any slot whose
   game-time deadline was reached. For time.wait (time-based slots), not frame-based ones. */
static const char *advance_time(double dt) {
    g_nt_app.time += dt;
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
    TEST_ASSERT_EQUAL_STRING("time", cJSON_GetObjectItemCaseSensitive(result, "group")->valuestring);
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
   holds the response until exactly `count` sim-advances have completed — so a follow-up read sees
   frame advanced by count, not by however many drained before the read. (Unit harness ticks the
   deferred queue directly via advance_sim(); the real loop also drains pending_steps.) */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_step_deferred_resolves_after_count(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"time.set_mode\",\"params\":{\"mode\":\"manual\"}}")));

    /* default count = 1 when omitted: queues 1, defers, resolves on the 1st sim-advance. */
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\"}"));
    TEST_ASSERT_EQUAL_INT(1, g_nt_app.pending_steps); /* queued immediately */
    TEST_ASSERT_NOT_NULL(advance_sim());              /* 1 advance resolves the default step's slot */

    /* explicit count = 3: deferred response withheld until the 3rd sim-advance. */
    g_nt_app.pending_steps = 0; /* isolate from the default-count check above */
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"request_id\":21,\"params\":{\"count\":3}}"));
    TEST_ASSERT_EQUAL_INT(3, g_nt_app.pending_steps);
    for (int i = 0; i < 2; i++) {
        TEST_ASSERT_NULL(advance_sim()); /* advances 1 and 2: still pending */
    }
    const char *resp = advance_sim(); /* 3rd advance yields {ok:true,result:{deferred:true},request_id:21} */
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "result"), "deferred")));
    TEST_ASSERT_EQUAL_INT(21, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* drained */
}

/* ---- render.* ---- */

static void test_render_info_reflects_flag(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"render.set_enabled\",\"params\":{\"enabled\":false}}"));
    TEST_ASSERT_FALSE(nt_app_render_enabled());
    cJSON_Delete(root);

    root = parse_ok(nt_devapi_submit("{\"method\":\"render.info\"}"));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result, "enabled")));
    /* no draw happened in this headless test -> draw_calls == 0 (nt_gfx_stub returns 0). */
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

/* frame.wait over the cap → structured bad_params (never spins, never enqueues a slot). */
static void test_frame_wait_over_cap_bad_params(void) {
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"frame.wait\",\"params\":{\"frames\":%d}}", NT_DEVAPI_FRAME_WAIT_MAX + 1);
    assert_bad_params(nt_devapi_submit(line));

    /* The rejected wait enqueued no slot: a sim-advance pops nothing. */
    TEST_ASSERT_NULL(advance_sim());
}

/* frame.wait{frames:0}: target == current frame, so it resolves on the very next drain with no
   sim-advance (the immediate-ack form), distinct from the over-cap rejection above. */
static void test_frame_wait_zero_resolves_immediately(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"frame.wait\",\"request_id\":5,\"params\":{\"frames\":0}}"));
    const char *resp = nt_devapi_poll_response(); /* ready immediately, no g_nt_app.frame advance */
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_EQUAL_INT(5, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* drained */
}

/* time.step{count} over the cap → bad_params, no backlog queued. cJSON clamps huge JSON
   numbers to INT_MAX, so an uncapped count would wedge the host for billions of frames. */
static void test_step_over_cap_bad_params(void) {
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"time.step\",\"params\":{\"count\":%lld}}", (long long)NT_DEVAPI_STEP_MAX + 1);
    assert_bad_params(nt_devapi_submit(line));
    TEST_ASSERT_EQUAL_INT(0, g_nt_app.pending_steps); /* rejected → nothing queued */
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
    const char *r = nt_devapi_poll_response(); /* the host drains the deferred queue each tick */
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
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* nothing left queued */
}

/* ---- time.wait (game-time deadline; the RUN-only sibling of frame.wait) ---- */

/* RUN, scale 1: time.wait{seconds} resolves once g_nt_app.time crosses the deadline, NOT on a frame
   count — proves the time-based deferred path. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_time_wait_yields_after_seconds(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.wait\",\"request_id\":11,\"params\":{\"seconds\":0.5}}"));
    TEST_ASSERT_NULL(advance_time(0.2));  /* time 0.2 < 0.5 */
    TEST_ASSERT_NULL(advance_time(0.2));  /* time 0.4 < 0.5 */
    const char *resp = advance_time(0.2); /* time 0.6 >= 0.5 -> resolves */
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "result"), "deferred")));
    TEST_ASSERT_EQUAL_INT(11, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
    TEST_ASSERT_NULL(nt_devapi_poll_response());
}

/* time.wait{seconds:0}: deadline == current time, so it resolves on the next drain with no time
   advance (immediate-ack), mirroring frame.wait{frames:0}. */
static void test_time_wait_zero_resolves_immediately(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.wait\",\"request_id\":13,\"params\":{\"seconds\":0}}"));
    const char *resp = nt_devapi_poll_response(); /* ready immediately, no g_nt_app.time advance */
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_EQUAL_INT(13, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* drained */
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

/* Over the seconds cap -> bad_params, no slot (a wait longer than the read-timeout could never be
   received by the client). */
static void test_time_wait_over_cap_bad_params(void) {
    char line[96];
    (void)snprintf(line, sizeof(line), "{\"method\":\"time.wait\",\"params\":{\"seconds\":%f}}", NT_DEVAPI_TIME_WAIT_MAX_SECONDS + 1.0);
    assert_bad_params(nt_devapi_submit(line));
    TEST_ASSERT_NULL(advance_time(NT_DEVAPI_TIME_WAIT_MAX_SECONDS + 2.0));
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
    const char *resp = advance_sim();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "result"), "deferred")));
    TEST_ASSERT_EQUAL_INT(12, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
    TEST_ASSERT_NULL(nt_devapi_poll_response());
}

/* count and seconds are mutually exclusive; a sub-step duration still queues >= 1 frame (ceil). */
static void test_time_step_count_seconds_exclusive_and_ceil(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":2,\"seconds\":1.0}}"));
    TEST_ASSERT_EQUAL_INT(0, g_nt_app.pending_steps); /* rejected before any side effect */

    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"seconds\":0.001}}"));
    TEST_ASSERT_EQUAL_INT(1, g_nt_app.pending_steps); /* ceil(0.001 / (1/60)) == 1 */
}

static void test_step_queue_full_has_no_side_effect(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    for (int i = 0; i < NT_DEVAPI_MAX_DEFERRED; i++) {
        TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":1}}"));
    }
    TEST_ASSERT_EQUAL_INT(NT_DEVAPI_MAX_DEFERRED, g_nt_app.pending_steps);

    assert_bad_params(nt_devapi_submit("{\"method\":\"time.step\",\"params\":{\"count\":1}}"));
    TEST_ASSERT_EQUAL_INT(NT_DEVAPI_MAX_DEFERRED, g_nt_app.pending_steps);
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
    RUN_TEST(test_frame_wait_over_cap_bad_params);
    RUN_TEST(test_frame_wait_zero_resolves_immediately);
    RUN_TEST(test_step_over_cap_bad_params);
    RUN_TEST(test_set_fps_tiny_underflow_bad_params);
    RUN_TEST(test_step_requires_manual_mode);
    RUN_TEST(test_set_scale_huge_bad_params);
    RUN_TEST(test_frame_wait_yields_after_n_sim_advances);
    RUN_TEST(test_frame_wait_rejected_when_not_running);
    RUN_TEST(test_step_drains_through_real_loop);
    RUN_TEST(test_time_wait_yields_after_seconds);
    RUN_TEST(test_time_wait_zero_resolves_immediately);
    RUN_TEST(test_time_wait_rejected_when_time_frozen);
    RUN_TEST(test_time_wait_over_cap_bad_params);
    RUN_TEST(test_time_step_seconds_converts_to_frames);
    RUN_TEST(test_time_step_count_seconds_exclusive_and_ceil);
    RUN_TEST(test_step_queue_full_has_no_side_effect);
    return UNITY_END();
}
