#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "debug_overlay/nt_debug_overlay.h"
#include "devapi/nt_devapi_internal.h"
#include "log/nt_log_ring.h"
#include "metrics/nt_metrics.h"

/* Observability command group: the log / perf / entity / resource namespaces (D-16). Every command
   is an IMMEDIATE read (D-15) — it serializes the Plan 01/03/04/05 L1 capabilities and returns on the
   same call; nothing in this group ever defers the response (OBS-06 reload dropped). Bot input is
   range/type-checked -> bad_params; only host-call invariants assert. Compiles out entirely when
   NT_DEVAPI_GROUP_OBS is absent. */

#ifdef NT_DEVAPI_GROUP_OBS

/* Pagination cap shared by entity.list / resource.list: oversized `limit` -> huge payload DoS
   (T-68-06-DOS). Mirrors the NT_DEVAPI_STEP_MAX fail-fast-ceiling style. Override per build with -D. */
#ifndef NT_DEVAPI_OBS_LIMIT_MAX
#define NT_DEVAPI_OBS_LIMIT_MAX 512
#endif

static void set_bad_params(nt_devapi_error *err, const char *message) {
    err->code = NT_DEVAPI_ERR_BAD_PARAMS;
    err->message = message;
}

// #region log.*
/* nt_log_level_t token mapping (stable JSON, not the numeric enum). */
static const char *log_level_token(nt_log_level_t level) {
    switch (level) {
    case NT_LOG_LEVEL_INFO:
        return "info";
    case NT_LOG_LEVEL_WARN:
        return "warn";
    case NT_LOG_LEVEL_ERROR:
        return "error";
    default:
        return "info";
    }
}

/* Parse an optional {level} string into a min_level filter. NT_LOG_LEVEL_INFO (0) is the ring's
   "no filter" sentinel (nt_log_ring_tail returns level >= min_level), so an absent param maps to it. */
static bool parse_level(const cJSON *jlevel, nt_log_level_t *out, nt_devapi_error *err) {
    if (jlevel == NULL) {
        *out = NT_LOG_LEVEL_INFO;
        return true;
    }
    if (!cJSON_IsString(jlevel) || jlevel->valuestring == NULL) {
        set_bad_params(err, "log.tail: level must be a string");
        return false;
    }
    const char *s = jlevel->valuestring;
    if (strcmp(s, "info") == 0) {
        *out = NT_LOG_LEVEL_INFO;
    } else if (strcmp(s, "warn") == 0) {
        *out = NT_LOG_LEVEL_WARN;
    } else if (strcmp(s, "error") == 0) {
        *out = NT_LOG_LEVEL_ERROR;
    } else {
        set_bad_params(err, "log.tail: level must be one of info/warn/error");
        return false;
    }
    return true;
}

/* log.tail{n?, level?}: newest-first {level,domain,msg} entries, up to n, optionally filtered by
   min level. Reads the dev-only nt_log_ring (D-05). n out of range / level unknown -> bad_params. */
static bool cmd_log_tail(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    uint16_t n = NT_LOG_RING_DEPTH;
    const cJSON *jn = cJSON_GetObjectItemCaseSensitive(params, "n");
    if (jn != NULL) {
        if (!cJSON_IsNumber(jn)) {
            set_bad_params(err, "log.tail: n must be a number");
            return false;
        }
        int v = jn->valueint;
        if (v < 0 || v > NT_LOG_RING_DEPTH) {
            set_bad_params(err, "log.tail: n out of range [0, NT_LOG_RING_DEPTH]");
            return false;
        }
        n = (uint16_t)v;
    }
    nt_log_level_t min_level = NT_LOG_LEVEL_INFO;
    if (!parse_level(cJSON_GetObjectItemCaseSensitive(params, "level"), &min_level, err)) {
        return false;
    }

    /* Newest-first tail into a stack buffer (no heap; ring-depth bounded). */
    static nt_log_ring_entry_t s_tail[NT_LOG_RING_DEPTH];
    uint16_t got = nt_log_ring_tail(n, min_level, s_tail);

    cJSON *arr = cJSON_AddArrayToObject(result, "entries");
    NT_ASSERT(arr != NULL);
    for (uint16_t i = 0; i < got; i++) {
        cJSON *e = cJSON_CreateObject();
        NT_ASSERT(e != NULL);
        devapi_add_string(e, "level", log_level_token(s_tail[i].level));
        devapi_add_string(e, "domain", s_tail[i].domain);
        devapi_add_string(e, "msg", s_tail[i].msg);
        cJSON_bool added = cJSON_AddItemToArray(arr, e);
        NT_ASSERT(added);
        (void)added;
    }
    return true;
}
// #endregion

// #region perf.*
/* perf.snapshot: IMMEDIATE current-frame view from the LIVE overlay getters (NOT the metrics
   window). gpu_ms maps -1.0F (timer unsupported) to JSON null (D-11). user_counters enumerates the
   overlay counters. */
static bool cmd_perf_snapshot(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    devapi_add_number(result, "fps", (double)nt_debug_overlay_get_fps());
    devapi_add_number(result, "frame_ms", (double)nt_debug_overlay_get_cpu_ms());
    devapi_add_number(result, "cpu_ms", (double)nt_debug_overlay_get_cpu_ms());
    float gpu = nt_debug_overlay_get_gpu_ms();
    if (gpu < 0.0F) {
        cJSON_AddNullToObject(result, "gpu_ms");
    } else {
        devapi_add_number(result, "gpu_ms", (double)gpu);
    }
    devapi_add_number(result, "draw_calls", (double)nt_debug_overlay_get_draw_calls());

    cJSON *uc = cJSON_AddObjectToObject(result, "user_counters");
    NT_ASSERT(uc != NULL);
    uint16_t un = nt_debug_overlay_user_count();
    for (uint16_t i = 0; i < un; i++) {
        const char *name = NULL;
        double value = 0.0;
        bool is_float = false;
        nt_debug_overlay_user_get(i, &name, &value, &is_float);
        if (name != NULL) {
            devapi_add_number(uc, name, value);
        }
    }
    return true;
}

/* Fixed-channel name table: maps the {channels} filter token to the nt_metrics enum. The order is
   the enum order so perf.stats with no filter emits all channels in a stable order. */
static const char *const k_channel_names[NT_METRICS_CHANNEL_COUNT] = {
    "frame_ms", "cpu_ms", "gpu_ms", "draw_calls", "mem_total", "scratch_hwm", "scratch_used", "pool_occupancy",
};

/* Serialize one channel's windowed aggregates. Empty window (samples==0) emits null aggregates
   (Open Q3 RESOLVED schema): {samples:0, avg:null, ...}. */
static void add_channel_stats(cJSON *parent, const char *name, const nt_metrics_stats_t *st) {
    cJSON *o = cJSON_AddObjectToObject(parent, name);
    NT_ASSERT(o != NULL);
    devapi_add_number(o, "samples", (double)st->samples);
    if (st->samples == 0U) {
        cJSON_AddNullToObject(o, "avg");
        cJSON_AddNullToObject(o, "min");
        cJSON_AddNullToObject(o, "max");
        cJSON_AddNullToObject(o, "median");
        cJSON_AddNullToObject(o, "p95");
        cJSON_AddNullToObject(o, "p99");
        cJSON_AddNullToObject(o, "p99_9");
        return;
    }
    devapi_add_number(o, "avg", st->avg);
    devapi_add_number(o, "min", st->min);
    devapi_add_number(o, "max", st->max);
    devapi_add_number(o, "median", st->median);
    devapi_add_number(o, "p95", st->p95);
    devapi_add_number(o, "p99", st->p99);
    devapi_add_number(o, "p99_9", st->p99_9);
}

/* perf.stats{channels?, budget_ms?}: windowed aggregates from nt_metrics (NOT the live overlay).
   Per requested (or all) fixed channels + user channels + fps-lows + over_budget_pct. Unknown
   channel / bad budget_ms -> bad_params (never assert). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_perf_stats(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;

    /* Optional explicit channel filter; absent -> all fixed channels. */
    const cJSON *jchannels = cJSON_GetObjectItemCaseSensitive(params, "channels");
    bool want[NT_METRICS_CHANNEL_COUNT];
    if (jchannels == NULL) {
        for (int i = 0; i < NT_METRICS_CHANNEL_COUNT; i++) {
            want[i] = true;
        }
    } else {
        if (!cJSON_IsArray(jchannels)) {
            set_bad_params(err, "perf.stats: channels must be an array of channel-name strings");
            return false;
        }
        for (int i = 0; i < NT_METRICS_CHANNEL_COUNT; i++) {
            want[i] = false;
        }
        const cJSON *jc = NULL;
        cJSON_ArrayForEach(jc, jchannels) {
            if (!cJSON_IsString(jc) || jc->valuestring == NULL) {
                set_bad_params(err, "perf.stats: channels must be strings");
                return false;
            }
            int found = -1;
            for (int i = 0; i < NT_METRICS_CHANNEL_COUNT; i++) {
                if (strcmp(jc->valuestring, k_channel_names[i]) == 0) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                /* Unknown channel: bad_params listing valid channels (T-68-06-CHAN). */
                set_bad_params(err, "perf.stats: unknown channel (valid: frame_ms, cpu_ms, gpu_ms, draw_calls, mem_total, scratch_hwm, scratch_used, pool_occupancy)");
                return false;
            }
            want[found] = true;
        }
    }

    /* Optional over-budget threshold; default 16.67ms (60fps frame). */
    double budget_ms = 1000.0 / 60.0;
    const cJSON *jbudget = cJSON_GetObjectItemCaseSensitive(params, "budget_ms");
    if (jbudget != NULL) {
        if (!cJSON_IsNumber(jbudget)) {
            set_bad_params(err, "perf.stats: budget_ms must be a number");
            return false;
        }
        budget_ms = jbudget->valuedouble;
        if (!isfinite(budget_ms) || budget_ms <= 0.0) {
            set_bad_params(err, "perf.stats: budget_ms must be finite and > 0");
            return false;
        }
    }

    cJSON *channels = cJSON_AddObjectToObject(result, "channels");
    NT_ASSERT(channels != NULL);
    for (int i = 0; i < NT_METRICS_CHANNEL_COUNT; i++) {
        if (!want[i]) {
            continue;
        }
        nt_metrics_stats_t st;
        nt_metrics_channel_stats((nt_metrics_channel_t)i, &st);
        add_channel_stats(channels, k_channel_names[i], &st);
    }

    /* User channels are always emitted (no fixed-name filter applies to them). */
    cJSON *user = cJSON_AddObjectToObject(result, "user_channels");
    NT_ASSERT(user != NULL);
    uint16_t un = nt_metrics_user_count();
    for (uint16_t i = 0; i < un; i++) {
        const char *name = nt_metrics_user_name(i);
        if (name == NULL) {
            continue;
        }
        nt_metrics_stats_t st;
        nt_metrics_user_stats(i, &st);
        add_channel_stats(user, name, &st);
    }

    devapi_add_number(result, "fps_low_1pct", nt_metrics_fps_low_1pct());
    devapi_add_number(result, "fps_low_01pct", nt_metrics_fps_low_01pct());
    devapi_add_number(result, "over_budget_pct", nt_metrics_over_budget_pct(budget_ms));
    devapi_add_number(result, "budget_ms", budget_ms);
    return true;
}

/* perf.reset: clear the metrics window without tearing down state (D-11). */
static bool cmd_perf_reset(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    nt_metrics_reset();
    devapi_add_bool(result, "reset", true);
    return true;
}
// #endregion

static const nt_devapi_command_desc k_obs_cmds[] = {
    {
        .method = "log.tail",
        .group = "log",
        .summary = "newest-first log entries from the dev ring, optionally filtered by min level",
        .params_shape = "{n?:number, level?:string}",
        .result_shape = "{entries:[{level:string,domain:string,msg:string}]}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "perf.snapshot",
        .group = "perf",
        .summary = "immediate current-frame perf view from the live overlay (gpu_ms null when unsupported)",
        .params_shape = "{}",
        .result_shape = "{fps:number,frame_ms:number,cpu_ms:number,gpu_ms:number|null,draw_calls:number,user_counters:object}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "perf.stats",
        .group = "perf",
        .summary = "windowed perf aggregates per channel (avg/median/p95/p99/p99_9) + fps-lows + over_budget_pct",
        .params_shape = "{channels?:[string], budget_ms?:number}",
        .result_shape = "{channels:object,user_channels:object,fps_low_1pct:number,fps_low_01pct:number,over_budget_pct:number}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "perf.reset",
        .group = "perf",
        .summary = "clear the metrics window (counts -> 0) without tearing down state",
        .params_shape = "{}",
        .result_shape = "{reset:bool}",
        .frame_behavior = "any",
        .side_effects = "clears perf window",
    },
};

static const nt_devapi_handler_fn k_obs_handlers[] = {
    cmd_log_tail,
    cmd_perf_snapshot,
    cmd_perf_stats,
    cmd_perf_reset,
};
_Static_assert(sizeof(k_obs_cmds) / sizeof(k_obs_cmds[0]) == sizeof(k_obs_handlers) / sizeof(k_obs_handlers[0]), "obs: descriptor/handler arrays must have equal length");

void nt_devapi_register_obs(void) {
    /* Engine-internal dup is a build-time bug -> assert NT_OK. Capture first: NT_ASSERT compiles
       out under NT_ASSERT_MODE=0, so the call must not live inside the macro. */
    int n = (int)(sizeof(k_obs_cmds) / sizeof(k_obs_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_obs_cmds[i], k_obs_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_GROUP_OBS */
