#include <math.h>
#include <stdint.h>
#include <string.h>

#include "app/nt_app.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi_internal.h"
#include "graphics/nt_gfx.h"

/* time/render/frame command group. Bot input is range/type-checked → bad_params; never assert
   on untrusted input (invariants assert, untrusted input returns a structured error). */

/* frame.current → {frame, time, dt}, also the reply of every clock deferral; g_nt_app.time = sum of applied
   dts = game-elapsed time. */
static bool cmd_frame_current(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    devapi_add_number(result, "frame", (double)g_nt_app.frame);
    devapi_add_number(result, "time", g_nt_app.time);
    devapi_add_number(result, "dt", (double)g_nt_app.dt);
    return true;
}

// #region time.*
static bool cmd_time_pause(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    nt_app_pause();
    devapi_add_bool(result, "paused", true);
    return true;
}

static bool cmd_time_resume(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    nt_app_resume();
    devapi_add_bool(result, "paused", false);
    return true;
}

/* Lockstep-crunch step. Count in frames (default 1) OR seconds (sugar: ceil(seconds/step_dt) frames
   — game-time and frames are linear in MANUAL since step_dt is fixed). Deferred: the reply waits for
   the steps already queued plus `count`, so the caller observes every queued step done. */
static bool cmd_time_step(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(params, "count");
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(params, "seconds");
    if (c != NULL && s != NULL) {
        return nt_devapi_fail(err, "time.step: count and seconds are mutually exclusive");
    }
    int count = 1;
    if (s != NULL) {
        if (!cJSON_IsNumber(s)) {
            return nt_devapi_fail(err, "time.step: seconds must be a number");
        }
        double seconds = s->valuedouble;
        if (!isfinite(seconds) || seconds <= 0.0) {
            return nt_devapi_fail(err, "time.step: seconds must be finite and > 0");
        }
        /* step_dt > 0 (L1 invariant); fmax: a positive quotient can underflow to 0 when step_dt > 1. */
        double steps = fmax(1.0, ceil(seconds / (double)g_nt_app.step_dt));
        if (steps > (double)INT32_MAX) {
            return nt_devapi_fail(err, "time.step: seconds too large (more than INT32_MAX steps)");
        }
        count = (int)steps; /* >= 1 (ceil of a positive) and <= INT32_MAX, so the cast is safe. */
    } else if (c != NULL) {
        uint32_t parsed = 0;
        const char *bad_count = "time.step: count must be an integer in [1, INT32_MAX]";
        if (!nt_devapi_parse_u32_param_exact(c, INT32_MAX, err, bad_count, &parsed)) {
            return false;
        }
        if (parsed == 0U) {
            return nt_devapi_fail(err, bad_count);
        }
        count = (int)parsed;
    }
    /* Only MANUAL drains pending_steps: a RUN step would resolve on RUN frames and leave its steps queued. */
    if (g_nt_app.mode != NT_APP_MODE_MANUAL) {
        return nt_devapi_fail(err, "time.step: only valid in 'manual' mode");
    }
    if (count > INT32_MAX - g_nt_app.pending_steps) {
        return nt_devapi_fail(err, "time.step: too many steps already queued");
    }
    if (!nt_devapi_defer(NT_DEVAPI_GATE_FRAMES, (double)(g_nt_app.pending_steps + count), cmd_frame_current, err)) {
        return false;
    }
    nt_app_step(count);
    return true;
}

static bool cmd_time_set_scale(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    /* The f32 parser also rejects a finite double that narrows to +inf: dt = wall_dt*inf would poison
       g_nt_app.time/dt. */
    const char *bad_scale = "time.set_scale: scale must be a finite float >= 0";
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(params, "scale");
    float fscale = 0.0F;
    if (!nt_devapi_parse_f32_param(s, err, bad_scale, &fscale)) {
        return false;
    }
    if (s->valuedouble < 0.0) { /* on the double: -1e-50 narrows to -0.0f and would pass */
        return nt_devapi_fail(err, bad_scale);
    }
    nt_app_set_scale(fscale);
    devapi_add_number(result, "scale", (double)fscale);
    return true;
}

static bool cmd_time_set_mode(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(params, "mode");
    if (!cJSON_IsString(m) || m->valuestring == NULL) {
        return nt_devapi_fail(err, "time.set_mode: mode must be a string");
    }
    nt_app_mode_t mode;
    if (strcmp(m->valuestring, "run") == 0) {
        mode = NT_APP_MODE_RUN;
    } else if (strcmp(m->valuestring, "manual") == 0) {
        mode = NT_APP_MODE_MANUAL;
    } else {
        return nt_devapi_fail(err, "time.set_mode: mode must be 'run' or 'manual'");
    }
    nt_app_set_mode(mode);
    devapi_add_string(result, "mode", m->valuestring);
    return true;
}

/* Writes g_nt_app.target_dt directly (existing field semantics): fps>0 → 1/fps, fps==0 → uncapped.
   No new engine setter needed — target_dt is the canonical frame-cap field. */
static bool cmd_time_set_fps(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *f = cJSON_GetObjectItemCaseSensitive(params, "fps");
    if (!cJSON_IsNumber(f)) {
        return nt_devapi_fail(err, "time.set_fps: fps must be a number");
    }
    double fps = f->valuedouble;
    if (!isfinite(fps) || fps < 0.0) {
        return nt_devapi_fail(err, "time.set_fps: fps must be finite and >= 0");
    }
    /* Compute the reciprocal in double, then validate the float-narrowed result: a finite-but-tiny
       fps (e.g. 1e-50) overflows 1/fps to +inf in float, which would spin-wait the managed loop
       forever. Reject any fps whose frame cap is not a finite, positive float. fps==0 = uncapped. */
    float target_dt = 0.0F;
    if (fps > 0.0) {
        target_dt = (float)(1.0 / fps);
        if (!isfinite(target_dt) || target_dt <= 0.0F) {
            return nt_devapi_fail(err, "time.set_fps: fps out of representable frame-cap range");
        }
    }
    g_nt_app.target_dt = target_dt;
    devapi_add_number(result, "fps", fps);
    return true;
}

/* RUN + scale>0 only: in MANUAL/pause/scale==0 game time is frozen. Deadline is GAME time, not a frame
   count — RUN's variable dt can't be precomputed as frames. A later freeze fails the reply as stalled. */
static bool cmd_time_wait(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    if (g_nt_app.mode != NT_APP_MODE_RUN || g_nt_app.paused || g_nt_app.scale <= 0.0F) {
        return nt_devapi_fail(err, "time.wait: only valid while game time advances (RUN, unpaused, scale > 0)");
    }
    double seconds = 0.0;
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(params, "seconds");
    if (s != NULL) {
        if (!cJSON_IsNumber(s)) {
            return nt_devapi_fail(err, "time.wait: seconds must be a number");
        }
        seconds = s->valuedouble;
    }
    if (!isfinite(seconds) || seconds < 0.0) {
        return nt_devapi_fail(err, "time.wait: seconds must be finite and >= 0");
    }
    return nt_devapi_defer(NT_DEVAPI_GATE_SECONDS, seconds, cmd_frame_current, err);
}
// #endregion

// #region render.*
static bool cmd_render_set_enabled(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(params, "enabled");
    if (!cJSON_IsBool(e)) {
        return nt_devapi_fail(err, "render.set_enabled: enabled must be a bool");
    }
    bool enabled = cJSON_IsTrue(e);
    nt_app_set_render_enabled(enabled);
    devapi_add_bool(result, "enabled", enabled);
    return true;
}

static bool cmd_render_info(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    devapi_add_bool(result, "enabled", nt_app_render_enabled());
    /* Commands run between frames: counters hold the frame just rendered, complete. */
    devapi_add_number(result, "draw_calls", (double)nt_gfx_draw_calls(&g_nt_gfx.counters));
    return true;
}
// #endregion

// #region frame.*
/* frame.wait passively waits for autonomous RUN frames: in MANUAL the bot steps, and a paused sim has
   nothing to wait for. A pause or mode switch after submit fails the reply as stalled. */
static bool cmd_frame_wait(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    if (g_nt_app.mode == NT_APP_MODE_MANUAL || g_nt_app.paused) {
        return nt_devapi_fail(err, "frame.wait: only valid in running mode (use time.step in manual; resume if paused)");
    }
    uint32_t frames = 1U;
    const cJSON *f = cJSON_GetObjectItemCaseSensitive(params, "frames");
    if (f != NULL && !nt_devapi_parse_u32_param_exact(f, INT32_MAX, err, "frame.wait: frames must be an integer in [0, INT32_MAX]", &frames)) {
        return false;
    }
    return nt_devapi_defer(NT_DEVAPI_GATE_FRAMES, (double)frames, cmd_frame_current, err);
}
// #endregion

static const nt_devapi_command k_time_cmds[] = {
    {
        .method = "time.pause",
        .summary = "pause the managed loop (RUN dt -> 0, frame frozen)",
        .params_shape = "{}",
        .result_shape = "{paused:bool}",
        .fn = cmd_time_pause,
    },
    {
        .method = "time.resume",
        .summary = "resume the managed loop",
        .params_shape = "{}",
        .result_shape = "{paused:bool}",
        .fn = cmd_time_resume,
    },
    {
        .method = "time.step",
        .summary = "MANUAL: advance count fixed-dt frames OR seconds (= seconds/step_dt frames); replies once every queued step is done",
        .params_shape = "{count?:number, seconds?:number}",
        .result_shape = "{frame:number,time:number,dt:number}",
        .fn = cmd_time_step,
    },
    {
        .method = "time.set_scale",
        .summary = "RUN dt multiplier (observation only, not a determinism primitive)",
        .params_shape = "{scale:number}",
        .result_shape = "{scale:number}",
        .fn = cmd_time_set_scale,
    },
    {
        .method = "time.set_mode",
        .summary = "switch the managed loop between 'run' and 'manual' (lockstep)",
        .params_shape = "{mode:string}",
        .result_shape = "{mode:string}",
        .fn = cmd_time_set_mode,
    },
    {
        .method = "time.set_fps",
        .summary = "frame-rate cap: fps>0 -> 1/fps, fps=0 -> uncapped",
        .params_shape = "{fps:number}",
        .result_shape = "{fps:number}",
        .fn = cmd_time_set_fps,
    },
    {
        .method = "time.wait",
        .summary = "RUN-only: reply once seconds of GAME time elapse; rejected in manual/paused/scale=0, stalled if time freezes later",
        .params_shape = "{seconds?:number}",
        .result_shape = "{frame:number,time:number,dt:number}",
        .fn = cmd_time_wait,
    },
    {
        .method = "render.set_enabled",
        .summary = "toggle the loop-agnostic draw-pass gate",
        .params_shape = "{enabled:bool}",
        .result_shape = "{enabled:bool}",
        .fn = cmd_render_set_enabled,
    },
    {
        .method = "render.info",
        .summary = "render-enabled flag + last-frame draw call count",
        .params_shape = "{}",
        .result_shape = "{enabled:bool,draw_calls:number}",
        .fn = cmd_render_info,
    },
    {
        .method = "frame.current",
        .summary = "current frame counter + game-elapsed time + last dt",
        .params_shape = "{}",
        .result_shape = "{frame:number,time:number,dt:number}",
        .fn = cmd_frame_current,
    },
    {
        .method = "frame.wait",
        .summary = "RUN-only: reply once frames sim-advances elapse (frames=0 resolves on the next nt_devapi_update); rejected in manual/paused, stalled if the sim freezes later",
        .params_shape = "{frames?:integer}",
        .result_shape = "{frame:number,time:number,dt:number}",
        .fn = cmd_frame_wait,
    },
};

void nt_devapi_register_time(void) { nt_devapi_register(k_time_cmds, NT_COUNTOF(k_time_cmds), NULL); }
