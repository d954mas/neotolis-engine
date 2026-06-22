#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi_internal.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "log/nt_log_ring.h"
#include "metrics/nt_metrics.h"
#include "resource/nt_resource.h"
#include "transform_comp/nt_transform_comp.h"

/* Observability command group: the log / perf / entity / resource namespaces. Every command
   is an IMMEDIATE read — it serializes the L1 capabilities and returns on the same call; nothing
   in this group ever defers the response. Bot input is range/type-checked -> bad_params; only
   host-call invariants assert. Compiles out entirely when NT_DEVAPI_GROUP_OBS is absent. */

#ifdef NT_DEVAPI_GROUP_OBS

/* Pagination cap shared by entity.list / resource.list: oversized `limit` -> huge payload DoS.
   Mirrors the NT_DEVAPI_STEP_MAX fail-fast-ceiling style. Override per build with -D. */
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

/* Parse the optional {n} count into [0, NT_LOG_RING_DEPTH]; absent -> full ring depth. Out of
   range / wrong type -> bad_params. Split out of cmd_log_tail to keep that handler simple
   (mirrors parse_level). */
static bool parse_tail_n(const cJSON *jn, uint16_t *out, nt_devapi_error *err) {
    if (jn == NULL) {
        *out = NT_LOG_RING_DEPTH;
        return true;
    }
    return nt_devapi_parse_u16_param_exact(jn, NT_LOG_RING_DEPTH, err, set_bad_params, "log.tail: n must be an integer in [0, NT_LOG_RING_DEPTH]", out);
}

/* Serialize one tail buffer into the entries array. Split out so cmd_log_tail stays simple. */
static void add_log_entries(cJSON *result, const nt_log_ring_entry_t *tail, uint16_t got) {
    cJSON *arr = cJSON_AddArrayToObject(result, "entries");
    NT_ASSERT(arr != NULL);
    for (uint16_t i = 0; i < got; i++) {
        cJSON *e = cJSON_CreateObject();
        NT_ASSERT(e != NULL);
        devapi_add_string(e, "level", log_level_token(tail[i].level));
        devapi_add_string(e, "domain", tail[i].domain);
        devapi_add_string(e, "msg", tail[i].msg);
        cJSON_bool added = cJSON_AddItemToArray(arr, e);
        NT_ASSERT(added);
        (void)added;
    }
}

/* log.tail{n?, level?}: newest-first {level,domain,msg} entries, up to n, optionally filtered by
   min level. Reads the dev-only nt_log_ring. n out of range / level unknown -> bad_params. */
static bool cmd_log_tail(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    uint16_t n = NT_LOG_RING_DEPTH;
    if (!parse_tail_n(cJSON_GetObjectItemCaseSensitive(params, "n"), &n, err)) {
        return false;
    }
    nt_log_level_t min_level = NT_LOG_LEVEL_INFO;
    if (!parse_level(cJSON_GetObjectItemCaseSensitive(params, "level"), &min_level, err)) {
        return false;
    }

    /* Newest-first tail into a static buffer (no heap; ring-depth bounded).
       INVARIANT: obs handlers must NEVER recurse into nt_devapi_submit — this static is shared across
       the single-threaded, non-re-entrant dispatch and would be clobbered mid-serialize on recursion. */
    static nt_log_ring_entry_t s_tail[NT_LOG_RING_DEPTH];
    uint16_t got = nt_log_ring_tail(n, min_level, s_tail);
    add_log_entries(result, s_tail, got);
    return true;
}
// #endregion

// #region perf.*
/* perf.snapshot: IMMEDIATE current-frame view from nt_metrics' last-pushed frame (NOT the windowed
   aggregates). fps is the rolling avg; frame_ms is the real last frame time (distinct from cpu_ms),
   null until the first valid frame; gpu_ms maps the < 0 sentinel (timer unsupported) to JSON null.
   user_counters enumerates the nt_metrics user counters with their EXACT stored value. */
static bool cmd_perf_snapshot(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    nt_metrics_frame_t last;
    nt_metrics_last(&last);
    devapi_add_number(result, "fps", (double)nt_metrics_fps());
    /* frame_ms is the real last frame time, distinct from cpu_ms. The host pushes <= 0 (or a
       non-finite) on the first frame (no dt yet); emit null then, mirroring gpu_ms. */
    if (isfinite(last.frame_ms) && last.frame_ms > 0.0F) {
        devapi_add_number(result, "frame_ms", (double)last.frame_ms);
    } else {
        cJSON_AddNullToObject(result, "frame_ms");
    }
    devapi_add_number(result, "cpu_ms", (double)last.cpu_ms);
    if (last.gpu_ms < 0.0F) {
        cJSON_AddNullToObject(result, "gpu_ms");
    } else {
        devapi_add_number(result, "gpu_ms", (double)last.gpu_ms);
    }
    devapi_add_number(result, "draw_calls", (double)last.draw_calls);

    /* nt_metrics keeps the exact uint64, but cJSON numbers are IEEE-754 doubles, so a counter above
       2^53 loses its low bits on the wire — a JSON number-format limit, not a store defect. */
    cJSON *uc = cJSON_AddObjectToObject(result, "user_counters");
    NT_ASSERT(uc != NULL);
    uint16_t un = nt_metrics_user_count();
    for (uint16_t i = 0; i < un; i++) {
        const char *name = NULL;
        uint64_t u = 0;
        double f = 0.0;
        bool is_float = false;
        nt_metrics_user_get(i, &name, &u, &f, &is_float);
        if (name != NULL) {
            devapi_add_number(uc, name, is_float ? f : (double)u);
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
   schema: {samples:0, avg:null, ...}. */
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

/* perf.stats{channels?, budget_ms?}: windowed aggregates from nt_metrics (NOT the last-frame snapshot).
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
                /* Unknown channel: bad_params listing valid channels. */
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

/* perf.reset: clear the metrics window without tearing down state. */
static bool cmd_perf_reset(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    nt_metrics_reset();
    devapi_add_bool(result, "reset", true);
    return true;
}
// #endregion

// #region pagination
/* Resolve optional {offset, limit} against `total`. limit is capped at NT_DEVAPI_OBS_LIMIT_MAX.
   Bad offset/limit -> bad_params. On success out_begin..out_end bound the page
   into [0, total]. */
static bool resolve_page(const cJSON *params, uint32_t total, const char *who, uint32_t *out_begin, uint32_t *out_end, nt_devapi_error *err) {
    uint32_t offset = 0;
    const cJSON *jo = cJSON_GetObjectItemCaseSensitive(params, "offset");
    if (jo != NULL && !nt_devapi_parse_u32_param_exact(jo, UINT32_MAX, err, set_bad_params, who, &offset)) {
        return false;
    }
    uint32_t limit = NT_DEVAPI_OBS_LIMIT_MAX;
    const cJSON *jl = cJSON_GetObjectItemCaseSensitive(params, "limit");
    if (jl != NULL) {
        if (!nt_devapi_parse_u32_param_exact(jl, UINT32_MAX, err, set_bad_params, who, &limit)) {
            return false;
        }
        if (limit > NT_DEVAPI_OBS_LIMIT_MAX) {
            limit = NT_DEVAPI_OBS_LIMIT_MAX; /* clamp, not reject — bot pages via total */
        }
    }
    uint32_t begin = offset < total ? offset : total;
    uint32_t end = begin + limit;
    if (end > total) {
        end = total;
    }
    *out_begin = begin;
    *out_end = end;
    return true;
}
// #endregion

// #region entity.*
/* Append a float[n] as a JSON number array under `key`. NULL src emits zeros. Split out so the
   entity serializers stay simple. */
static void add_float_array(cJSON *obj, const char *key, const float *src, int n) {
    cJSON *a = cJSON_AddArrayToObject(obj, key);
    NT_ASSERT(a != NULL);
    for (int k = 0; k < n; k++) {
        cJSON_bool added = cJSON_AddItemToArray(a, cJSON_CreateNumber(src != NULL ? (double)src[k] : 0.0));
        NT_ASSERT(added);
        (void)added;
    }
}

/* entity "position": [x,y,z] when it has a transform, else null. */
static void add_entity_position(cJSON *o, nt_entity_t e) {
    if (nt_transform_comp_has(e)) {
        add_float_array(o, "position", nt_transform_comp_position(e), 3);
    } else {
        cJSON_AddNullToObject(o, "position");
    }
}

/* entity "drawable": {visible, color:[r,g,b,a]} when it has a drawable, else null. */
static void add_entity_drawable(cJSON *o, nt_entity_t e) {
    if (!nt_drawable_comp_has(e)) {
        cJSON_AddNullToObject(o, "drawable");
        return;
    }
    cJSON *d = cJSON_AddObjectToObject(o, "drawable");
    NT_ASSERT(d != NULL);
    const bool *vis = nt_drawable_comp_visible(e);
    devapi_add_bool(d, "visible", vis != NULL && *vis);
    add_float_array(d, "color", nt_drawable_comp_color(e), 4);
}

/* Serialize one entity's compact view (id/index/generation/alive/enabled + optional position +
   drawable) into the entities array. Split out so cmd_entity_list stays under the cognitive-complexity
   ceiling and the two-pass loop body stays simple. */
static void add_entity_entry(cJSON *arr, nt_entity_t e) {
    cJSON *o = cJSON_CreateObject();
    NT_ASSERT(o != NULL);
    devapi_add_number(o, "id", (double)e.id);
    devapi_add_number(o, "index", (double)nt_entity_index(e));
    devapi_add_number(o, "generation", (double)nt_entity_generation(e));
    devapi_add_bool(o, "alive", true);
    devapi_add_bool(o, "enabled", nt_entity_is_enabled(e));
    add_entity_position(o, e);
    add_entity_drawable(o, e);

    cJSON_bool added = cJSON_AddItemToArray(arr, o);
    NT_ASSERT(added);
    (void)added;
}

/* True if the live slot at `idx` (1..nt_entity_max()) passes the only_drawable filter; writes the
   resolved handle to *out. False for a dead slot or a filtered-out entity. */
static bool entity_slot_matches(uint16_t idx, bool only_drawable, nt_entity_t *out) {
    nt_entity_t e = nt_entity_at_index(idx);
    if (e.id == NT_ENTITY_INVALID.id) {
        return false;
    }
    if (only_drawable && !nt_drawable_comp_has(e)) {
        return false;
    }
    *out = e;
    return true;
}

/* entity.list{offset?, limit?, only_drawable?}: live entities with compact fields (no world matrix).
   Two heap-free passes over 1..nt_entity_max() via nt_entity_at_index: pass 1 counts the honest
   filtered total, pass 2 emits the [begin,end) page resolved against it — so the whole live range is
   pageable, not just a fixed prefix. Optional only_drawable filter. Bad offset/limit -> bad_params. */
static bool cmd_entity_list(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    bool only_drawable = false;
    const cJSON *jod = cJSON_GetObjectItemCaseSensitive(params, "only_drawable");
    if (jod != NULL) {
        if (!cJSON_IsBool(jod)) {
            set_bad_params(err, "entity.list: only_drawable must be a bool");
            return false;
        }
        only_drawable = cJSON_IsTrue(jod);
    }

    uint16_t emax = nt_entity_max();

    /* Pass 1: honest count of all matching live entities (no working buffer, no cap). */
    uint32_t total = 0;
    for (uint16_t idx = 1; idx <= emax; idx++) {
        nt_entity_t e;
        if (entity_slot_matches(idx, only_drawable, &e)) {
            total++;
        }
    }

    uint32_t begin = 0;
    uint32_t end = 0;
    if (!resolve_page(params, total, "entity.list: offset/limit must be non-negative numbers", &begin, &end, err)) {
        return false;
    }

    devapi_add_number(result, "total", (double)total);
    cJSON *arr = cJSON_AddArrayToObject(result, "entities");
    NT_ASSERT(arr != NULL);

    /* Pass 2: emit only entities whose running matched-index falls in [begin, end). */
    uint32_t matched = 0;
    for (uint16_t idx = 1; idx <= emax; idx++) {
        nt_entity_t e;
        if (!entity_slot_matches(idx, only_drawable, &e)) {
            continue;
        }
        if (matched >= begin && matched < end) {
            add_entity_entry(arr, e);
        }
        matched++;
    }
    return true;
}
// #endregion

// #region resource.*
/* nt_pack_state_t token mapping (stable JSON). */
static const char *pack_state_token(uint8_t state) {
    switch (state) {
    case NT_PACK_STATE_NONE:
        return "none";
    case NT_PACK_STATE_REQUESTED:
        return "requested";
    case NT_PACK_STATE_DOWNLOADING:
        return "downloading";
    case NT_PACK_STATE_LOADED:
        return "loaded";
    case NT_PACK_STATE_READY:
        return "ready";
    case NT_PACK_STATE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

/* nt_asset_state_t token mapping. The enum is internal (nt_resource_internal.h must not cross the
   boundary), so the numeric values are mapped here verbatim: 0=registered,1=failed,2=loading,3=ready. */
static const char *asset_state_token(uint8_t state) {
    switch (state) {
    case 0:
        return "registered";
    case 1:
        return "failed";
    case 2:
        return "loading";
    case 3:
        return "ready";
    default:
        return "unknown";
    }
}

/* Parse the optional {pack_id} filter. Absent -> filter off. On a value, resolve it to the raw
   packs[] slot index (NtAssetMeta.pack_index space) so the asset filter can match exactly; an
   unmounted/unknown pack_id leaves filter on with no matching slot (empty result, not an error). */
static bool resolve_pack_filter(const cJSON *params, bool *out_filter, uint16_t *out_slot, uint32_t *out_id, nt_devapi_error *err) {
    *out_filter = false;
    *out_slot = UINT16_MAX; /* sentinel: no mounted pack matches (UINT16_MAX is never a real slot) */
    *out_id = 0;
    const cJSON *jpid = cJSON_GetObjectItemCaseSensitive(params, "pack_id");
    if (jpid == NULL) {
        return true;
    }
    uint32_t pack_id = 0;
    if (!nt_devapi_parse_u32_param_exact(jpid, UINT32_MAX, err, set_bad_params, "resource.list: pack_id must be a non-negative integer", &pack_id)) {
        return false;
    }
    *out_filter = true;
    *out_id = pack_id;
    uint16_t pack_count = nt_resource_pack_count();
    for (uint16_t i = 0; i < pack_count; i++) {
        nt_resource_pack_info_t info;
        if (nt_resource_pack_info(i, &info) && info.id == pack_id) {
            *out_slot = info.pack_index; /* raw slot — matches the asset's pack_index */
            break;
        }
    }
    return true;
}

/* Serialize one asset entry into the assets array. Split out so cmd_resource_list stays simple. */
static void add_asset_entry(cJSON *assets, const nt_resource_asset_info_t *ai) {
    cJSON *o = cJSON_CreateObject();
    NT_ASSERT(o != NULL);
    /* 64-bit hash as a 0x-hex string: a JSON double loses the low bits above 2^53, so the id
       could not round-trip. entity/pack ids stay numbers (uint32, exact in a double). */
    char rid_hex[19]; /* "0x" + 16 hex + NUL */
    (void)snprintf(rid_hex, sizeof(rid_hex), "0x%016" PRIx64, ai->resource_id);
    devapi_add_string(o, "resource_id", rid_hex);
    devapi_add_number(o, "type", (double)ai->type);
    devapi_add_string(o, "state", asset_state_token(ai->state));
    /* pack_index = the raw packs[] slot (matches resource.list's pack_id filter resolution), not the
       public pack_id; named explicitly so a bot does not mistake it for the pack's id. */
    devapi_add_number(o, "pack_index", (double)ai->pack_index);
    cJSON_bool added = cJSON_AddItemToArray(assets, o);
    NT_ASSERT(added);
    (void)added;
}

/* Emit the flat assets[] for the (optionally pack-filtered) scope. asset_total is the HONEST count
   for the filtered scope; the emitted prefix is bounded at NT_DEVAPI_OBS_LIMIT_MAX (DoS cap) and
   assets_truncated flags when more match than were emitted. */
static void add_assets_section(cJSON *result, bool filter_pack, uint16_t filter_slot) {
    cJSON *assets = cJSON_AddArrayToObject(result, "assets");
    NT_ASSERT(assets != NULL);
    uint16_t asset_count = nt_resource_asset_count();
    uint32_t emitted = 0;
    uint32_t scoped_total = 0;
    for (uint16_t i = 0; i < asset_count; i++) {
        nt_resource_asset_info_t ai;
        if (!nt_resource_asset_info(i, &ai)) {
            continue;
        }
        if (filter_pack && ai.pack_index != filter_slot) {
            continue;
        }
        scoped_total++;
        if (emitted < NT_DEVAPI_OBS_LIMIT_MAX) {
            add_asset_entry(assets, &ai);
            emitted++;
        }
    }
    devapi_add_number(result, "asset_total", (double)scoped_total);
    devapi_add_bool(result, "assets_truncated", scoped_total > emitted);
}

/* resource.list{offset?, limit?, pack_id?, include_assets?}: always packs[]; flat assets[] only when
   include_assets true. Pagination over packs with `total`; optional pack_id filter.
   When pack_id filters, assets[] is restricted to that pack too. State values mapped to stable
   tokens. Bad pack_id/offset/limit -> bad_params. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_resource_list(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    bool include_assets = false;
    const cJSON *jia = cJSON_GetObjectItemCaseSensitive(params, "include_assets");
    if (jia != NULL) {
        if (!cJSON_IsBool(jia)) {
            set_bad_params(err, "resource.list: include_assets must be a bool");
            return false;
        }
        include_assets = cJSON_IsTrue(jia);
    }
    bool filter_pack = false;
    uint16_t filter_slot = UINT16_MAX;
    uint32_t pack_id = 0;
    if (!resolve_pack_filter(params, &filter_pack, &filter_slot, &pack_id, err)) {
        return false;
    }

    uint16_t pack_count = nt_resource_pack_count();
    uint32_t total = filter_pack ? 0 : (uint32_t)pack_count;
    if (filter_pack) {
        for (uint16_t i = 0; i < pack_count; i++) {
            nt_resource_pack_info_t info;
            if (nt_resource_pack_info(i, &info) && info.id == pack_id) {
                total++;
            }
        }
    }

    uint32_t begin = 0;
    uint32_t end = 0;
    if (!resolve_page(params, total, "resource.list: offset/limit must be non-negative numbers", &begin, &end, err)) {
        return false;
    }

    devapi_add_number(result, "total", (double)total);
    cJSON *packs = cJSON_AddArrayToObject(result, "packs");
    NT_ASSERT(packs != NULL);
    uint32_t matched = 0; /* running index over the filtered pack set, for pagination */
    for (uint16_t i = 0; i < pack_count; i++) {
        nt_resource_pack_info_t info;
        if (!nt_resource_pack_info(i, &info)) {
            continue;
        }
        if (filter_pack && info.id != pack_id) {
            continue;
        }
        if (matched < begin || matched >= end) {
            matched++;
            continue;
        }
        matched++;
        cJSON *o = cJSON_CreateObject();
        NT_ASSERT(o != NULL);
        devapi_add_number(o, "id", (double)info.id);
        devapi_add_string(o, "state", pack_state_token(info.state));
        devapi_add_number(o, "priority", (double)info.priority);
        devapi_add_number(o, "asset_count", (double)info.asset_count);
        devapi_add_bool(o, "mounted", info.mounted != 0U);
        cJSON_bool added = cJSON_AddItemToArray(packs, o);
        NT_ASSERT(added);
        (void)added;
    }

    if (include_assets) {
        add_assets_section(result, filter_pack, filter_slot);
    }
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
        .summary = "immediate current-frame perf view from nt_metrics' last frame (frame_ms/gpu_ms null until valid)",
        .params_shape = "{}",
        .result_shape = "{fps:number,frame_ms:number|null,cpu_ms:number,gpu_ms:number|null,draw_calls:number,user_counters:object}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "perf.stats",
        .group = "perf",
        .summary = "windowed perf aggregates per channel (avg/median/p95/p99/p99_9) + fps-lows + over_budget_pct",
        .params_shape = "{channels?:[string], budget_ms?:number}",
        .result_shape = "{channels:object,user_channels:object,fps_low_1pct:number,fps_low_01pct:number,over_budget_pct:number,budget_ms:number}",
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
    {
        .method = "entity.list",
        .group = "entity",
        .summary = "live entities with compact fields (id/generation/enabled/position/drawable); fully paginated with honest total",
        .params_shape = "{offset?:number, limit?:number, only_drawable?:bool}",
        .result_shape = "{total:number,entities:[{id,index,generation,alive,enabled,position,drawable}]}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "resource.list",
        .group = "resource",
        .summary = "mounted packs (id/state/priority/asset_count); flat assets[] (capped, pack_id-filtered) when include_assets; paginated with total",
        .params_shape = "{offset?:number, limit?:number, pack_id?:number, include_assets?:bool}",
        .result_shape = "{total:number,packs:[{id,state,priority,asset_count,mounted}],assets?:[{resource_id:string,type,state,pack_index}],asset_total?:number,assets_truncated?:bool}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
};

static const nt_devapi_handler_fn k_obs_handlers[] = {
    cmd_log_tail, cmd_perf_snapshot, cmd_perf_stats, cmd_perf_reset, cmd_entity_list, cmd_resource_list,
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
