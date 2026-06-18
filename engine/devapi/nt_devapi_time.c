#include <math.h>
#include <string.h>

#include "app/nt_app.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi_internal.h"
#include "stats/nt_stats.h"

/* time/render/frame command group. Bot input is range/type-checked → bad_params; never assert
   on untrusted input (invariants assert, untrusted input returns a structured error).
   Compiles out entirely when NT_DEVAPI_REGISTER_time is absent. */

#ifdef NT_DEVAPI_REGISTER_time

static void set_bad_params(nt_devapi_error *err, const char *message) {
    err->code = NT_DEVAPI_ERR_BAD_PARAMS;
    err->message = message;
}

// #region time.*
static bool cmd_time_pause(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    nt_app_pause();
    devapi_add_bool(result, "paused", true);
    return true;
}

static bool cmd_time_resume(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    nt_app_resume();
    devapi_add_bool(result, "paused", false);
    return true;
}

/* Lockstep-crunch step (default 1). Deferred: the response is held until all count sim-advances
   complete, so the caller observes frame already advanced by exactly count, not mid-drain. */
static bool cmd_time_step(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)result;
    (void)ud;
    int count = 1;
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(params, "count");
    if (c != NULL) {
        if (!cJSON_IsNumber(c)) {
            set_bad_params(err, "time.step: count must be a number");
            return false;
        }
        count = c->valueint;
    }
    if (count < 1 || count > NT_DEVAPI_STEP_MAX) {
        set_bad_params(err, "time.step: count out of range [1, NT_DEVAPI_STEP_MAX]");
        return false;
    }
    /* Only MANUAL drains pending_steps; queued into RUN they never drain, and under RUN+pause the
       deferred reply could never resolve → the caller would block until its socket timeout. */
    if (g_nt_app.mode != NT_APP_MODE_MANUAL) {
        set_bad_params(err, "time.step: only valid in 'manual' mode");
        return false;
    }
    nt_app_step(count);
    return nt_devapi_defer_current(count);
}

static bool cmd_time_set_scale(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(params, "scale");
    if (!cJSON_IsNumber(s)) {
        set_bad_params(err, "time.set_scale: scale must be a number");
        return false;
    }
    double scale = s->valuedouble;
    if (!isfinite(scale) || scale < 0.0) {
        set_bad_params(err, "time.set_scale: scale must be finite and >= 0");
        return false;
    }
    /* A huge finite double narrows to +inf in float; +inf scale → dt = wall_dt*inf poisons
       g_nt_app.time/dt. Reject any scale whose float form is not finite. */
    float fscale = (float)scale;
    if (!isfinite(fscale)) {
        set_bad_params(err, "time.set_scale: scale out of representable range");
        return false;
    }
    nt_app_set_scale(fscale);
    devapi_add_number(result, "scale", scale);
    return true;
}

static bool cmd_time_set_mode(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(params, "mode");
    if (!cJSON_IsString(m) || m->valuestring == NULL) {
        set_bad_params(err, "time.set_mode: mode must be a string");
        return false;
    }
    nt_app_mode_t mode;
    if (strcmp(m->valuestring, "run") == 0) {
        mode = NT_APP_MODE_RUN;
    } else if (strcmp(m->valuestring, "manual") == 0) {
        mode = NT_APP_MODE_MANUAL;
    } else {
        set_bad_params(err, "time.set_mode: mode must be 'run' or 'manual'");
        return false;
    }
    nt_app_set_mode(mode);
    devapi_add_string(result, "mode", m->valuestring);
    return true;
}

/* Writes g_nt_app.target_dt directly (existing field semantics): fps>0 → 1/fps, fps==0 → uncapped.
   No new engine setter needed — target_dt is the canonical frame-cap field. */
static bool cmd_time_set_fps(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *f = cJSON_GetObjectItemCaseSensitive(params, "fps");
    if (!cJSON_IsNumber(f)) {
        set_bad_params(err, "time.set_fps: fps must be a number");
        return false;
    }
    double fps = f->valuedouble;
    if (!isfinite(fps) || fps < 0.0) {
        set_bad_params(err, "time.set_fps: fps must be finite and >= 0");
        return false;
    }
    /* Compute the reciprocal in double, then validate the float-narrowed result: a finite-but-tiny
       fps (e.g. 1e-50) overflows 1/fps to +inf in float, which would spin-wait the managed loop
       forever. Reject any fps whose frame cap is not a finite, positive float. fps==0 = uncapped. */
    float target_dt = 0.0F;
    if (fps > 0.0) {
        target_dt = (float)(1.0 / fps);
        if (!isfinite(target_dt) || target_dt <= 0.0F) {
            set_bad_params(err, "time.set_fps: fps out of representable frame-cap range");
            return false;
        }
    }
    g_nt_app.target_dt = target_dt;
    devapi_add_number(result, "fps", fps);
    return true;
}
// #endregion

// #region render.*
static bool cmd_render_set_enabled(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(params, "enabled");
    if (!cJSON_IsBool(e)) {
        set_bad_params(err, "render.set_enabled: enabled must be a bool");
        return false;
    }
    bool enabled = cJSON_IsTrue(e);
    nt_app_set_render_enabled(enabled);
    devapi_add_bool(result, "enabled", enabled);
    return true;
}

static bool cmd_render_info(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    devapi_add_bool(result, "enabled", nt_app_render_enabled());
    devapi_add_number(result, "draw_calls", (double)nt_stats_get_draw_calls());
    return true;
}
// #endregion

// #region frame.*
/* frame.current → {frame, time, dt}; g_nt_app.time = sum of applied dts = game-elapsed time. */
static bool cmd_frame_current(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    devapi_add_number(result, "frame", (double)g_nt_app.frame);
    devapi_add_number(result, "time", (double)g_nt_app.time);
    devapi_add_number(result, "dt", (double)g_nt_app.dt);
    return true;
}

/* Range-check then ride the bounded deferred queue. PAUSE never advances the sim, so a wait
   during pause never resolves; over-cap fails fast (never spins). */
static bool cmd_frame_wait(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)result;
    (void)ud;
    int frames = 1;
    const cJSON *f = cJSON_GetObjectItemCaseSensitive(params, "frames");
    if (f != NULL) {
        if (!cJSON_IsNumber(f)) {
            set_bad_params(err, "frame.wait: frames must be a number");
            return false;
        }
        frames = f->valueint;
    }
    if (frames < 0 || frames > NT_DEVAPI_FRAME_WAIT_MAX) {
        set_bad_params(err, "frame.wait: frames out of range [0, NT_DEVAPI_FRAME_WAIT_MAX]");
        return false;
    }
    return nt_devapi_defer_current(frames);
}
// #endregion

static const nt_devapi_command_desc k_time_cmds[] = {
    {
        .method = "time.pause",
        .group = "time",
        .summary = "pause the managed loop (RUN dt -> 0, frame frozen)",
        .params_shape = "{}",
        .result_shape = "{paused:bool}",
        .frame_behavior = "any",
        .side_effects = "pauses sim",
    },
    {
        .method = "time.resume",
        .group = "time",
        .summary = "resume the managed loop",
        .params_shape = "{}",
        .result_shape = "{paused:bool}",
        .frame_behavior = "any",
        .side_effects = "resumes sim",
    },
    {
        .method = "time.step",
        .group = "time",
        .summary = "MANUAL: advance exactly count fixed-dt sim frames (lockstep crunch); blocks until done",
        .params_shape = "{count?:number}",
        .result_shape = "{deferred:bool}",
        .frame_behavior = "deferred",
        .side_effects = "queues sim advances",
    },
    {
        .method = "time.set_scale",
        .group = "time",
        .summary = "RUN dt multiplier (observation only, not a determinism primitive)",
        .params_shape = "{scale:number}",
        .result_shape = "{scale:number}",
        .frame_behavior = "any",
        .side_effects = "sets dt scale",
    },
    {
        .method = "time.set_mode",
        .group = "time",
        .summary = "switch the managed loop between 'run' and 'manual' (lockstep)",
        .params_shape = "{mode:string}",
        .result_shape = "{mode:string}",
        .frame_behavior = "any",
        .side_effects = "sets loop mode",
    },
    {
        .method = "time.set_fps",
        .group = "time",
        .summary = "frame-rate cap: fps>0 -> 1/fps, fps=0 -> uncapped",
        .params_shape = "{fps:number}",
        .result_shape = "{fps:number}",
        .frame_behavior = "any",
        .side_effects = "sets target_dt",
    },
    {
        .method = "render.set_enabled",
        .group = "render",
        .summary = "toggle the loop-agnostic draw-pass gate",
        .params_shape = "{enabled:bool}",
        .result_shape = "{enabled:bool}",
        .frame_behavior = "any",
        .side_effects = "toggles render gate",
    },
    {
        .method = "render.info",
        .group = "render",
        .summary = "render-enabled flag + last-frame draw call count",
        .params_shape = "{}",
        .result_shape = "{enabled:bool,draw_calls:number}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "frame.current",
        .group = "frame",
        .summary = "current frame counter + game-elapsed time + last dt",
        .params_shape = "{}",
        .result_shape = "{frame:number,time:number,dt:number}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "frame.wait",
        .group = "frame",
        .summary = "defer the response until frames sim-advances elapse (frames=0 resolves on the next drain; PAUSE never advances -> never resolves)",
        .params_shape = "{frames?:number}",
        .result_shape = "{deferred:bool}",
        .frame_behavior = "deferred",
        .side_effects = "none",
    },
};

static const nt_devapi_handler_fn k_time_handlers[] = {
    cmd_time_pause, cmd_time_resume, cmd_time_step, cmd_time_set_scale, cmd_time_set_mode, cmd_time_set_fps, cmd_render_set_enabled, cmd_render_info, cmd_frame_current, cmd_frame_wait,
};
_Static_assert(sizeof(k_time_cmds) / sizeof(k_time_cmds[0]) == sizeof(k_time_handlers) / sizeof(k_time_handlers[0]), "time: descriptor/handler arrays must have equal length");

void nt_devapi_register_time(void) {
    /* Engine-internal dup is a build-time bug → assert NT_OK. Capture first: NT_ASSERT
       compiles out under NT_ASSERT_MODE=0, so the call must not live inside the macro. */
    int n = (int)(sizeof(k_time_cmds) / sizeof(k_time_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_time_cmds[i], k_time_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_REGISTER_time */
