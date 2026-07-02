#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi_internal.h"
#include "entity/nt_entity.h"
#include "hash/nt_hash.h"
#include "introspect/nt_introspect.h"
#include "log/nt_log_ring.h"
#include "material/nt_material.h" /* NT_REF_MATERIAL label resolution in j_ref */
#include "metrics/nt_metrics.h"
#include "resource/nt_resource.h"

/* Observability command group (log / perf / entity / resource): every command is an IMMEDIATE read,
   never deferred. Bot input is range/type-checked -> bad_params; only host-call invariants assert.
   Compiles out entirely when NT_DEVAPI_GROUP_OBS is absent. */

#ifdef NT_DEVAPI_GROUP_OBS

/* Pagination cap shared by entity.list / resource.list: oversized `limit` -> huge payload DoS. */
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

/* Parse the optional {n} count into [0, NT_LOG_RING_DEPTH]; absent -> full ring depth.
   Out of range / wrong type -> bad_params. */
static bool parse_tail_n(const cJSON *jn, uint16_t *out, nt_devapi_error *err) {
    if (jn == NULL) {
        *out = NT_LOG_RING_DEPTH;
        return true;
    }
    return nt_devapi_parse_u16_param_exact(jn, NT_LOG_RING_DEPTH, err, set_bad_params, "log.tail: n must be an integer in [0, NT_LOG_RING_DEPTH]", out);
}

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

    /* Static (no heap) is safe only because dispatch is single-threaded and non-re-entrant:
       an obs handler must never recurse into nt_devapi_submit or this buffer is clobbered mid-serialize. */
    static nt_log_ring_entry_t s_tail[NT_LOG_RING_DEPTH];
    uint16_t got = nt_log_ring_tail(n, min_level, s_tail);
    add_log_entries(result, s_tail, got);
    return true;
}
// #endregion

// #region perf.*
/* perf.snapshot: immediate last-frame view (not the window). */
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
        devapi_add_null(result, "frame_ms");
    }
    devapi_add_number(result, "cpu_ms", (double)last.cpu_ms);
    if (last.gpu_ms < 0.0F) {
        devapi_add_null(result, "gpu_ms");
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
    "frame_ms", "cpu_ms", "gpu_ms", "draw_calls", "mem_used", "scratch_hwm", "scratch_used", "pool_occupancy",
};

/* Serialize one channel's windowed aggregates. Empty window (samples==0) emits null aggregates
   schema: {samples:0, avg:null, ...}. */
static void add_channel_stats(cJSON *parent, const char *name, const nt_metrics_stats_t *st) {
    cJSON *o = cJSON_AddObjectToObject(parent, name);
    NT_ASSERT(o != NULL);
    devapi_add_number(o, "samples", (double)st->samples);
    if (st->samples == 0U) {
        devapi_add_null(o, "avg");
        devapi_add_null(o, "min");
        devapi_add_null(o, "max");
        devapi_add_null(o, "median");
        devapi_add_null(o, "p95");
        devapi_add_null(o, "p99");
        devapi_add_null(o, "p99_9");
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
                set_bad_params(err, "perf.stats: unknown channel (valid: frame_ms, cpu_ms, gpu_ms, draw_calls, mem_used, scratch_hwm, scratch_used, pool_occupancy)");
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
/* JSON sink for nt_entity_introspect: renders the walk into a cJSON object tree. `stack` holds the
   current container — begin_group pushes a child, end_group pops; seeded at depth 1 with the entity
   object so a component group never pops past it. */
typedef struct {
    nt_introspect_sink base;
    cJSON *stack[NT_INTROSPECT_MAX_DEPTH];
    uint8_t depth;
    uint8_t skipped; /* begin_groups refused past MAX_DEPTH; end_group unwinds them before popping */
} obs_json_sink;

static cJSON *json_top(obs_json_sink *j) { return j->stack[j->depth - 1]; }

static void j_begin_group(nt_introspect_sink *s, const char *key) {
    obs_json_sink *j = (obs_json_sink *)s;
    if (j->depth >= NT_INTROSPECT_MAX_DEPTH) {
        /* Asserts-off safety net: refuse to overflow the fixed stack. A describe() nesting this deep is
           a bug; deeper fields fold into the current container instead of writing out of bounds. */
        NT_ASSERT(false && "nt_introspect: begin_group past NT_INTROSPECT_MAX_DEPTH");
        j->skipped++;
        return;
    }
    cJSON *child = cJSON_AddObjectToObject(json_top(j), key);
    NT_ASSERT(child != NULL);
    j->stack[j->depth++] = child;
}
static void j_end_group(nt_introspect_sink *s) {
    obs_json_sink *j = (obs_json_sink *)s;
    if (j->skipped > 0) {
        j->skipped--;
        return;
    }
    NT_ASSERT(j->depth > 1); /* never pop the seed entity object */
    j->depth--;
}
static void j_f32(nt_introspect_sink *s, const char *key, float v) { devapi_add_number(json_top((obs_json_sink *)s), key, (double)v); }
static void j_i64(nt_introspect_sink *s, const char *key, int64_t v) { devapi_add_number(json_top((obs_json_sink *)s), key, (double)v); }
static void j_u64(nt_introspect_sink *s, const char *key, uint64_t v) { devapi_add_number(json_top((obs_json_sink *)s), key, (double)v); }
/* 64-bit opaque id as a 0x-hex string: a JSON number is a double, so an id above 2^53 loses its low
   bits (mirrors j_ref/resource_id). */
static void j_u64_hex(nt_introspect_sink *s, const char *key, uint64_t v) {
    char hex[19];
    (void)snprintf(hex, sizeof(hex), "0x%016" PRIx64, v);
    devapi_add_string(json_top((obs_json_sink *)s), key, hex);
}
static void j_bool(nt_introspect_sink *s, const char *key, bool v) { devapi_add_bool(json_top((obs_json_sink *)s), key, v); }

static void j_floats(nt_introspect_sink *s, const char *key, const float *v, int count) {
    obs_json_sink *j = (obs_json_sink *)s;
    NT_ASSERT(v != NULL); /* describe() boundary: a present component's accessor never returns NULL */
    cJSON *arr = cJSON_AddArrayToObject(json_top(j), key);
    NT_ASSERT(arr != NULL);
    for (int i = 0; i < count; i++) {
        cJSON_bool added = cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)v[i]));
        NT_ASSERT(added);
        (void)added;
    }
}
static void j_str(nt_introspect_sink *s, const char *key, const char *v) { devapi_add_string(json_top((obs_json_sink *)s), key, v != NULL ? v : ""); }
static void j_enum(nt_introspect_sink *s, const char *key, const char *token) { devapi_add_string(json_top((obs_json_sink *)s), key, token != NULL ? token : ""); }

/* A ref is a typed id token, never expanded inline: {"ref":"<kind>","id":"0x..."}. Hex string id
   keeps the full 64 bits exact (a cJSON number is a double). */
static void j_ref(nt_introspect_sink *s, const char *key, nt_ref_kind_t kind, uint64_t id) {
    obs_json_sink *j = (obs_json_sink *)s;
    cJSON *ref = cJSON_AddObjectToObject(json_top(j), key);
    NT_ASSERT(ref != NULL);
    devapi_add_string(ref, "ref", nt_introspect_ref_kind_name(kind));
    char hex[19];
    (void)snprintf(hex, sizeof(hex), "0x%" PRIx64, id);
    devapi_add_string(ref, "id", hex);
    /* Resolve the material handle to its create-time label here — nt_material is already linked in the
       obs sink, so material_comp can stay a bare handle emitter. */
    if (kind == NT_REF_MATERIAL) {
        nt_material_t mat = {.id = (uint32_t)id};
        if (nt_material_valid(mat)) {
            const nt_material_info_t *info = nt_material_get_info(mat);
            if (info != NULL && info->label != NULL) {
                devapi_add_string(ref, "label", info->label);
            }
        }
    }
}

/* Resolving asset sink: a runtime handle -> its source resource_id (reverse scan) -> name (label),
   written flat into the component's own group. The resource/hash deps live HERE (devapi already links
   them), so resource-backed components stay decoupled. */
static void j_asset(nt_introspect_sink *s, uint8_t asset_type, uint32_t handle) {
    cJSON *o = json_top((obs_json_sink *)s);
    devapi_add_number(o, "handle", (double)handle);
    uint64_t src = nt_resource_source_of(asset_type, handle);
    if (src != 0) {
        char hex[19];
        (void)snprintf(hex, sizeof(hex), "0x%" PRIx64, src);
        devapi_add_string(o, "resource", hex);
        const char *name = nt_hash64_label((nt_hash64_t){.value = src});
        if (name != NULL) {
            devapi_add_string(o, "name", name);
        }
    }
}

static void obs_json_sink_init(obs_json_sink *j, cJSON *root) {
    j->base = (nt_introspect_sink){
        .begin_group = j_begin_group,
        .end_group = j_end_group,
        .field_f32 = j_f32,
        .field_i64 = j_i64,
        .field_u64 = j_u64,
        .field_u64_hex = j_u64_hex,
        .field_bool = j_bool,
        .field_floats = j_floats,
        .field_str = j_str,
        .field_enum = j_enum,
        .field_ref = j_ref,
        .field_asset = j_asset,
    };
    j->stack[0] = root;
    j->depth = 1;
    j->skipped = 0;
}

/* Serialize one entity (core fields + each present component as a named group) into the entities
   array via the generic introspection walk — no component-specific code here. Pass 2 only reaches
   proven-live slots, so the live shape carries no `alive` field: every entry is live by construction. */
static void add_entity_entry(cJSON *arr, nt_entity_t e) {
    cJSON *o = cJSON_CreateObject();
    NT_ASSERT(o != NULL);
    obs_json_sink sink;
    obs_json_sink_init(&sink, o);
    nt_entity_introspect(e, &sink.base);

    cJSON_bool added = cJSON_AddItemToArray(arr, o);
    NT_ASSERT(added);
    (void)added;
}

/* Per-clause term cap: bounds the stack id arrays + a DoS ceiling on filter size. Override with -D. */
#ifndef NT_DEVAPI_QUERY_MAX_TERMS
#define NT_DEVAPI_QUERY_MAX_TERMS 16
#endif
/* n_all/n_any/n_none are uint8_t, so the cap must fit in uint8_t (else the counter wraps past it). */
_Static_assert(NT_DEVAPI_QUERY_MAX_TERMS > 0 && NT_DEVAPI_QUERY_MAX_TERMS <= UINT8_MAX, "NT_DEVAPI_QUERY_MAX_TERMS must be in (0, UINT8_MAX]");

/* Component-set filter: an entity passes if it has EVERY `all`, at least ONE `any` (when any present),
   and NONE of `none`. Component ids resolved once from names (never matched by string per entity). */
typedef struct {
    nt_comp_id_t all[NT_DEVAPI_QUERY_MAX_TERMS];
    nt_comp_id_t any[NT_DEVAPI_QUERY_MAX_TERMS];
    nt_comp_id_t none[NT_DEVAPI_QUERY_MAX_TERMS];
    uint8_t n_all, n_any, n_none;
} entity_filter;

/* Resolve a JSON array of component-name strings, APPENDING their ids to `ids` (count in *n). Unknown
   name / non-string element / over the per-clause cap -> bad_params. A NULL/absent clause is empty. */
static bool append_filter_terms(const cJSON *arr, nt_comp_id_t *ids, uint8_t *n, nt_devapi_error *err) {
    if (arr == NULL) {
        return true;
    }
    if (!cJSON_IsArray(arr)) {
        set_bad_params(err, "entity.list: all/any/none must be arrays of component names");
        return false;
    }
    const cJSON *el = NULL;
    cJSON_ArrayForEach(el, arr) {
        if (*n >= NT_DEVAPI_QUERY_MAX_TERMS) {
            set_bad_params(err, "entity.list: too many filter terms");
            return false;
        }
        if (!cJSON_IsString(el) || el->valuestring == NULL) {
            set_bad_params(err, "entity.list: filter terms must be component-name strings");
            return false;
        }
        nt_comp_id_t id = nt_entity_storage_find(el->valuestring);
        if (id == NT_COMP_ID_INVALID) {
            set_bad_params(err, "entity.list: unknown component in filter");
            return false;
        }
        ids[(*n)++] = id;
    }
    return true;
}

/* Parse {component?, all?, any?, none?} into resolved ids. `component:"x"` is sugar for all:["x"]. */
static bool parse_entity_filter(const cJSON *params, entity_filter *f, nt_devapi_error *err) {
    f->n_all = 0;
    f->n_any = 0;
    f->n_none = 0;
    const cJSON *jcomp = cJSON_GetObjectItemCaseSensitive(params, "component");
    if (jcomp != NULL) {
        if (!cJSON_IsString(jcomp) || jcomp->valuestring == NULL) {
            set_bad_params(err, "entity.list: component must be a string");
            return false;
        }
        nt_comp_id_t id = nt_entity_storage_find(jcomp->valuestring);
        if (id == NT_COMP_ID_INVALID) {
            set_bad_params(err, "entity.list: unknown component");
            return false;
        }
        f->all[f->n_all++] = id;
    }
    return append_filter_terms(cJSON_GetObjectItemCaseSensitive(params, "all"), f->all, &f->n_all, err) &&
           append_filter_terms(cJSON_GetObjectItemCaseSensitive(params, "any"), f->any, &f->n_any, err) &&
           append_filter_terms(cJSON_GetObjectItemCaseSensitive(params, "none"), f->none, &f->n_none, err);
}

static bool entity_passes(nt_entity_t e, const entity_filter *f) {
    for (uint8_t i = 0; i < f->n_all; i++) {
        if (!nt_entity_has_comp(e, f->all[i])) {
            return false;
        }
    }
    if (f->n_any > 0) {
        bool found = false;
        for (uint8_t i = 0; i < f->n_any && !found; i++) {
            found = nt_entity_has_comp(e, f->any[i]);
        }
        if (!found) {
            return false;
        }
    }
    for (uint8_t i = 0; i < f->n_none; i++) {
        if (nt_entity_has_comp(e, f->none[i])) {
            return false;
        }
    }
    return true;
}

/* True if the live slot at `idx` (1..nt_entity_max()) exists and passes the filter; writes the handle
   to *out. False for a dead slot or a filtered-out entity. */
static bool entity_slot_matches(uint16_t idx, const entity_filter *f, nt_entity_t *out) {
    nt_entity_t e = nt_entity_at_index(idx);
    if (e.id == NT_ENTITY_INVALID.id) {
        return false;
    }
    if (!entity_passes(e, f)) {
        return false;
    }
    *out = e;
    return true;
}

/* entity.list{offset?, limit?, component?, all?, any?, none?} -> live entities (core fields + each
   present component as a named group; no world matrix). Filter: has every `all` + at least one `any`
   + none of `none`; `component:"x"` is sugar for all:["x"]. Unknown component / bad offset/limit ->
   bad_params. */
static bool cmd_entity_list(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    entity_filter f;
    if (!parse_entity_filter(params, &f, err)) {
        return false;
    }

    uint16_t emax = nt_entity_max();

    /* Pass 1: honest count of all matching live entities (no working buffer, no cap).
       uint32_t counter: emax may be UINT16_MAX, so a uint16_t idx would wrap past it and never end. */
    uint32_t total = 0;
    for (uint32_t idx = 1; idx <= emax; idx++) {
        nt_entity_t e;
        if (entity_slot_matches((uint16_t)idx, &f, &e)) {
            total++;
        }
    }

    uint32_t begin = 0;
    uint32_t end = 0;
    if (!resolve_page(params, total, "entity.list: offset/limit must be integers in [0, UINT32_MAX]", &begin, &end, err)) {
        return false;
    }

    devapi_add_number(result, "total", (double)total);
    cJSON *arr = cJSON_AddArrayToObject(result, "entities");
    NT_ASSERT(arr != NULL);

    /* Pass 2: emit only entities whose running matched-index falls in [begin, end). */
    uint32_t matched = 0;
    for (uint32_t idx = 1; idx <= emax; idx++) {
        nt_entity_t e;
        if (!entity_slot_matches((uint16_t)idx, &f, &e)) {
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
    /* blob_pins = PIN_BLOB pins held via this asset's pack (nonzero only when it is the published
       winner of a pinning slot, e.g. NT_ASSET_FONT); answers "which asset pins this pack blob". */
    devapi_add_number(o, "blob_pins", (double)ai->blob_pins);
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
    if (!resolve_page(params, total, "resource.list: offset/limit must be integers in [0, UINT32_MAX]", &begin, &end, err)) {
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
        .summary = "live entities: core fields (id/index/generation/enabled) + each present component as a named group (empty {} when it has no describe); fully paginated with honest total; "
                   "component-set filter: has every `all` + at least one `any` + none of `none` (`component` is sugar for all:[x])",
        .params_shape = "{offset?:number, limit?:number, component?:string, all?:[string], any?:[string], none?:[string]}",
        .result_shape = "{total:number,entities:[{id,index,generation,enabled,<component>:{...}}]}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "resource.list",
        .group = "resource",
        .summary = "mounted packs (id/state/priority/asset_count); flat assets[] (capped, pack_id-filtered) when include_assets; paginated with total",
        .params_shape = "{offset?:number, limit?:number, pack_id?:number, include_assets?:bool}",
        .result_shape = "{total:number,packs:[{id,state,priority,asset_count}],assets?:[{resource_id:string,type,state,pack_index,blob_pins}],asset_total?:number,assets_truncated?:bool}",
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
