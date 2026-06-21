/* L2 devapi obs group (log.* / perf.* / entity.* / resource.*) via submit() (no socket): each
   command is an IMMEDIATE read of the L1 capabilities. Asserts the serialized
   shapes (log.tail entries, perf.snapshot keys + gpu_ms null, perf.stats per-channel aggregates,
   entity.list pagination + total, resource.list packs/assets) and every bad_params path. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "debug_overlay/nt_debug_overlay.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "log/nt_log_ring.h"
#include "metrics/nt_metrics.h"
#include "resource/nt_resource.h"
#include "transform_comp/nt_transform_comp.h"
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

void setUp(void) {
    /* L1 modules the obs handlers read. Overlay + metrics + log ring are dev-only sources; entity +
       components + resource back entity.list / resource.list. */
    nt_debug_overlay_init(NULL);
    nt_metrics_init();
    nt_log_ring_init();
    /* nt_log_add_sink is idempotent: re-attaching the same (fn,user) across tests is a no-op,
       so this stays a single live sink with no static-flag workaround. */
    nt_log_add_sink(nt_log_ring_sink, NULL);

    nt_entity_desc_t edesc = nt_entity_desc_defaults();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_entity_init(&edesc));
    nt_transform_comp_desc_t tdesc = nt_transform_comp_desc_defaults();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_transform_comp_init(&tdesc));
    nt_drawable_comp_desc_t ddesc = nt_drawable_comp_desc_defaults();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_drawable_comp_init(&ddesc));
    nt_resource_desc_t rdesc = {0};
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_resource_init(&rdesc));

    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
}

void tearDown(void) {
    nt_devapi_shutdown();
    nt_resource_shutdown();
    nt_drawable_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    nt_log_ring_clear();
    nt_debug_overlay_shutdown();
}

/* ---- helpers ---- */

static void assert_bad_params(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(err, "code")->valuestring);
    cJSON_Delete(root);
}

static cJSON *parse_ok(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    return root;
}

static cJSON *result_of(cJSON *root) { return cJSON_GetObjectItemCaseSensitive(root, "result"); }

/* ---- discovery: the four obs group names are listed ---- */

static bool endpoints_has_method(const char *method) {
    const char *resp = nt_devapi_submit("{\"method\":\"endpoints\",\"params\":{\"detail\":true}}");
    cJSON *root = parse_ok(resp);
    cJSON *commands = cJSON_GetObjectItemCaseSensitive(result_of(root), "commands");
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

static void test_discovery_lists_obs_commands(void) {
    TEST_ASSERT_TRUE(endpoints_has_method("log.tail"));
    TEST_ASSERT_TRUE(endpoints_has_method("perf.snapshot"));
    TEST_ASSERT_TRUE(endpoints_has_method("perf.stats"));
    TEST_ASSERT_TRUE(endpoints_has_method("perf.reset"));
    TEST_ASSERT_TRUE(endpoints_has_method("entity.list"));
    TEST_ASSERT_TRUE(endpoints_has_method("resource.list"));
}

/* ---- log.tail ---- */

/* Content assertions need the real ring; the OFF mirror (NT_LOG_RING_ENABLED=0) compiles a no-op
   ring that always tails empty, so guard the populated-data checks (shape + bad_params stay live). */
#if NT_LOG_RING_ENABLED
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_log_tail_shape_and_n_cap(void) {
    nt_log_write(NT_LOG_LEVEL_INFO, "obs", "first");
    nt_log_write(NT_LOG_LEVEL_WARN, "obs", "second");
    nt_log_write(NT_LOG_LEVEL_ERROR, "obs", "third");

    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"log.tail\",\"params\":{\"n\":2}}"));
    cJSON *entries = cJSON_GetObjectItemCaseSensitive(result_of(root), "entries");
    TEST_ASSERT_TRUE(cJSON_IsArray(entries));
    TEST_ASSERT_TRUE(cJSON_GetArraySize(entries) <= 2);
    cJSON *e0 = cJSON_GetArrayItem(entries, 0);
    TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(e0, "level")));
    TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(e0, "domain")));
    TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(e0, "msg")));
    /* newest-first: the most recent write ("third") is entry 0. */
    TEST_ASSERT_EQUAL_STRING("third", cJSON_GetObjectItemCaseSensitive(e0, "msg")->valuestring);
    cJSON_Delete(root);
}

static void test_log_tail_level_filter(void) {
    nt_log_write(NT_LOG_LEVEL_INFO, "obs", "info-line");
    nt_log_write(NT_LOG_LEVEL_ERROR, "obs", "err-line");

    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"log.tail\",\"params\":{\"level\":\"error\"}}"));
    cJSON *entries = cJSON_GetObjectItemCaseSensitive(result_of(root), "entries");
    cJSON *e = NULL;
    cJSON_ArrayForEach(e, entries) {
        /* every returned entry is >= error (only the error token survives the filter). */
        TEST_ASSERT_EQUAL_STRING("error", cJSON_GetObjectItemCaseSensitive(e, "level")->valuestring);
    }
    cJSON_Delete(root);
}
#endif /* NT_LOG_RING_ENABLED */

static void test_log_tail_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"log.tail\",\"params\":{\"n\":-1}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"log.tail\",\"params\":{\"n\":\"x\"}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"log.tail\",\"params\":{\"level\":\"bogus\"}}"));
    /* strict numeric parse: fractional and non-finite are rejected, not truncated. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"log.tail\",\"params\":{\"n\":2.5}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"log.tail\",\"params\":{\"n\":1e400}}"));
}

/* ---- perf.snapshot ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_perf_snapshot_keys_and_gpu_null(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"perf.snapshot\"}"));
    cJSON *r = result_of(root);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(r, "fps")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(r, "frame_ms")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(r, "cpu_ms")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(r, "draw_calls")));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItemCaseSensitive(r, "user_counters"));
    /* gpu_ms: present, and null when the GPU timer is unsupported (overlay returns -1.0). */
    cJSON *gpu = cJSON_GetObjectItemCaseSensitive(r, "gpu_ms");
    TEST_ASSERT_NOT_NULL(gpu);
    TEST_ASSERT_TRUE(cJSON_IsNull(gpu) || cJSON_IsNumber(gpu));
    cJSON_Delete(root);
}

static void test_perf_snapshot_user_counters(void) {
    nt_debug_overlay_count("enemies", 7U);
    nt_debug_overlay_count_f("frame_avg", 16.5);

    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"perf.snapshot\"}"));
    cJSON *uc = cJSON_GetObjectItemCaseSensitive(result_of(root), "user_counters");
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(uc, "enemies")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(uc, "frame_avg")));
    cJSON_Delete(root);
}

/* ---- perf.stats ---- */

/* test_perf_stats_channel_shape + test_perf_reset_ok feed a known sample set via nt_metrics_test_push,
   which only exists when the real collector is compiled in. The OFF mirror (NT_METRICS_ENABLED=0) has
   no-op metrics + no test hook, so guard those two; perf.stats empty-window/bad_params + perf.snapshot
   stay live in both configs. */
#if NT_METRICS_ENABLED
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_perf_stats_channel_shape(void) {
    /* Feed a known set into the frame_ms channel so the aggregates are populated. */
    for (int i = 1; i <= 10; i++) {
        nt_metrics_test_push(NT_METRICS_FRAME_MS, (double)i);
    }
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"perf.stats\",\"params\":{\"channels\":[\"frame_ms\"]}}"));
    cJSON *r = result_of(root);
    cJSON *channels = cJSON_GetObjectItemCaseSensitive(r, "channels");
    cJSON *fm = cJSON_GetObjectItemCaseSensitive(channels, "frame_ms");
    TEST_ASSERT_NOT_NULL(fm);
    TEST_ASSERT_EQUAL_INT(10, cJSON_GetObjectItemCaseSensitive(fm, "samples")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(fm, "avg")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(fm, "median")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(fm, "p95")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(fm, "p99")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(fm, "p99_9")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(r, "fps_low_1pct")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(r, "over_budget_pct")));
    cJSON_Delete(root);
}

/* After real per-frame sampling on a host with no GPU timer (overlay gpu_ms == -1.0F sentinel),
   perf.stats gpu_ms must be samples:0 + null aggregates — matching perf.snapshot's gpu_ms:null, not a
   confident avg:-1. setUp() inits the overlay (last_gpu_ms defaults to the sentinel). */
static void test_perf_stats_gpu_sentinel_null(void) {
    for (int i = 0; i < 5; i++) {
        nt_metrics_sample();
    }
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"perf.stats\",\"params\":{\"channels\":[\"gpu_ms\",\"cpu_ms\"]}}"));
    cJSON *channels = cJSON_GetObjectItemCaseSensitive(result_of(root), "channels");
    cJSON *gpu = cJSON_GetObjectItemCaseSensitive(channels, "gpu_ms");
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetObjectItemCaseSensitive(gpu, "samples")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(gpu, "avg")));
    /* cpu_ms DID sample, proving the frames ran (gpu was filtered, not the whole loop). */
    cJSON *cpu = cJSON_GetObjectItemCaseSensitive(channels, "cpu_ms");
    TEST_ASSERT_EQUAL_INT(5, cJSON_GetObjectItemCaseSensitive(cpu, "samples")->valueint);
    cJSON_Delete(root);
}
#endif /* NT_METRICS_ENABLED */

static void test_perf_stats_empty_window_null_aggregates(void) {
    /* No samples pushed -> samples:0 + null aggregates. */
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"perf.stats\",\"params\":{\"channels\":[\"cpu_ms\"]}}"));
    cJSON *cm = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(result_of(root), "channels"), "cpu_ms");
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetObjectItemCaseSensitive(cm, "samples")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(cm, "avg")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(cm, "p99")));
    cJSON_Delete(root);
}

static void test_perf_stats_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"perf.stats\",\"params\":{\"budget_ms\":-1}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"perf.stats\",\"params\":{\"channels\":[\"nope\"]}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"perf.stats\",\"params\":{\"channels\":\"frame_ms\"}}"));
}

#if NT_METRICS_ENABLED
static void test_perf_reset_ok(void) {
    nt_metrics_test_push(NT_METRICS_FRAME_MS, 5.0);
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"perf.reset\"}"));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result_of(root), "reset")));
    cJSON_Delete(root);

    /* the window is now empty -> samples:0. */
    cJSON *r2 = parse_ok(nt_devapi_submit("{\"method\":\"perf.stats\",\"params\":{\"channels\":[\"frame_ms\"]}}"));
    cJSON *fm = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(result_of(r2), "channels"), "frame_ms");
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetObjectItemCaseSensitive(fm, "samples")->valueint);
    cJSON_Delete(r2);
}
#endif /* NT_METRICS_ENABLED */

/* ---- entity.list ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_entity_list_total_and_fields(void) {
    nt_entity_t a = nt_entity_create();
    nt_entity_t b = nt_entity_create();
    TEST_ASSERT_TRUE(nt_transform_comp_add(a));
    float *pa = nt_transform_comp_position(a);
    pa[0] = 1.0F;
    pa[1] = 2.0F;
    pa[2] = 3.0F;
    TEST_ASSERT_TRUE(nt_drawable_comp_add(b));

    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"entity.list\"}"));
    cJSON *r = result_of(root);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(r, "total")->valueint);
    /* entity.list is fully paginated against the honest total. */
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(r, "truncated"));
    cJSON *entities = cJSON_GetObjectItemCaseSensitive(r, "entities");
    TEST_ASSERT_TRUE(cJSON_IsArray(entities));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(entities));
    cJSON *e0 = cJSON_GetArrayItem(entities, 0);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(e0, "id")));
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(e0, "generation")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(e0, "alive")));
    cJSON_Delete(root);
}

static void test_entity_list_only_drawable_filter(void) {
    nt_entity_t a = nt_entity_create();
    nt_entity_t b = nt_entity_create();
    (void)a;
    TEST_ASSERT_TRUE(nt_drawable_comp_add(b));

    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"entity.list\",\"params\":{\"only_drawable\":true}}"));
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItemCaseSensitive(result_of(root), "total")->valueint);
    cJSON_Delete(root);
}

static void test_entity_list_pagination_and_bad_params(void) {
    for (int i = 0; i < 5; i++) {
        (void)nt_entity_create();
    }
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"entity.list\",\"params\":{\"offset\":1,\"limit\":2}}"));
    cJSON *r = result_of(root);
    TEST_ASSERT_EQUAL_INT(5, cJSON_GetObjectItemCaseSensitive(r, "total")->valueint);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(r, "entities")));
    cJSON_Delete(root);

    assert_bad_params(nt_devapi_submit("{\"method\":\"entity.list\",\"params\":{\"offset\":-1}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"entity.list\",\"params\":{\"limit\":-3}}"));
    /* strict numeric parse: fractional offset/limit rejected, not truncated. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"entity.list\",\"params\":{\"offset\":1.5}}"));
}

/* ---- resource.list ---- */

static void test_resource_list_packs(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"resource.list\"}"));
    cJSON *r = result_of(root);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(r, "total")));
    TEST_ASSERT_TRUE(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(r, "packs")));
    /* without include_assets there is no assets array. */
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(r, "assets"));
    cJSON_Delete(root);
}

static void test_resource_list_include_assets_flat(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"resource.list\",\"params\":{\"include_assets\":true}}"));
    cJSON *r = result_of(root);
    cJSON *assets = cJSON_GetObjectItemCaseSensitive(r, "assets");
    TEST_ASSERT_NOT_NULL(assets);
    TEST_ASSERT_TRUE(cJSON_IsArray(assets));
    /* The flat assets[] is bounded (DoS cap); asset_total + assets_truncated report the cap. */
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(r, "asset_total")));
    TEST_ASSERT_TRUE(cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(r, "assets_truncated")));
    cJSON_Delete(root);
}

static void test_resource_list_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"resource.list\",\"params\":{\"pack_id\":-1}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"resource.list\",\"params\":{\"offset\":-1}}"));
    assert_bad_params(nt_devapi_submit("{\"method\":\"resource.list\",\"params\":{\"include_assets\":\"yes\"}}"));
    /* strict numeric parse: fractional pack_id rejected, not truncated. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"resource.list\",\"params\":{\"pack_id\":1.5}}"));
}

/* resource_id is a 64-bit hash; a JSON double drops the low bits above 2^53, so it can't
   round-trip. It must serialize as a 0x-hex string. Register an asset whose id sets bits above 2^53
   and that would alias a neighbouring value when squashed through a double, then assert the exact hex. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_resource_list_resource_id_hex_string(void) {
    nt_hash32_t pid = nt_hash32_str("ser2_pack");
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_resource_create_pack(pid, 0));
    /* 0xFEEDFACECAFEBEEF: > 2^53, low bits would be lost as a JSON double. */
    nt_hash64_t rid = {0xFEEDFACECAFEBEEFULL};
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_resource_register(pid, rid, 0U, 1U));

    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"resource.list\",\"params\":{\"include_assets\":true}}"));
    cJSON *assets = cJSON_GetObjectItemCaseSensitive(result_of(root), "assets");
    TEST_ASSERT_TRUE(cJSON_IsArray(assets));
    bool found = false;
    cJSON *a = NULL;
    cJSON_ArrayForEach(a, assets) {
        cJSON *rid_j = cJSON_GetObjectItemCaseSensitive(a, "resource_id");
        TEST_ASSERT_TRUE(cJSON_IsString(rid_j)); /* string, never a number */
        if (strcmp(rid_j->valuestring, "0xfeedfacecafebeef") == 0) {
            found = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "resource_id not serialized as the exact 0x-hex string (low bits lost?)");
    cJSON_Delete(root);
}

/* The flat assets[] is DoS-capped at NT_DEVAPI_OBS_LIMIT_MAX. Register more than the
   cap, then assert the returned array length == cap, assets_truncated == true, and asset_total is the
   HONEST real count (not clamped). The entity-cap true-branch needs a 4096-entity working set or a
   per-lib -D override (cap is PRIVATE to nt_devapi), so it is left to the override path; the assets
   cap is exercised here directly since NT_RESOURCE_MAX_ASSETS (2048) > the 512 cap. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_resource_list_assets_cap_trips(void) {
    enum { OBS_LIMIT_MAX = 512 }; /* mirrors nt_devapi_obs.c NT_DEVAPI_OBS_LIMIT_MAX default */
    const int n = OBS_LIMIT_MAX + 1;
    nt_hash32_t pid = nt_hash32_str("cap_pack");
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_resource_create_pack(pid, 0));
    for (int i = 0; i < n; i++) {
        nt_hash64_t rid = {0x1000000000000000ULL + (uint64_t)i}; /* distinct, non-zero, hi-bit */
        TEST_ASSERT_EQUAL_INT(NT_OK, nt_resource_register(pid, rid, 0U, (uint32_t)(i + 1)));
    }

    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"resource.list\",\"params\":{\"include_assets\":true}}"));
    cJSON *r = result_of(root);
    cJSON *assets = cJSON_GetObjectItemCaseSensitive(r, "assets");
    TEST_ASSERT_TRUE(cJSON_IsArray(assets));
    TEST_ASSERT_EQUAL_INT(OBS_LIMIT_MAX, cJSON_GetArraySize(assets)); /* emitted prefix == cap */
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "assets_truncated")));
    TEST_ASSERT_EQUAL_INT(n, cJSON_GetObjectItemCaseSensitive(r, "asset_total")->valueint); /* honest total */
    cJSON_Delete(root);
}

/* resource.list{pack_id} must filter the flat assets[] to that pack, not emit assets for ALL
   packs. Mount two virtual packs with distinct asset counts, then assert a pack_id-filtered
   include_assets request returns only pack A's assets and asset_total == pack A's count. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_resource_list_pack_id_filters_assets(void) {
    nt_hash32_t pid_a = nt_hash32_str("filter_pack_a");
    nt_hash32_t pid_b = nt_hash32_str("filter_pack_b");
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_resource_create_pack(pid_a, 0));
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_resource_create_pack(pid_b, 0));

    const int count_a = 3;
    const int count_b = 5;
    for (int i = 0; i < count_a; i++) {
        nt_hash64_t rid = {0xA000000000000000ULL + (uint64_t)i};
        TEST_ASSERT_EQUAL_INT(NT_OK, nt_resource_register(pid_a, rid, 0U, (uint32_t)(i + 1)));
    }
    for (int i = 0; i < count_b; i++) {
        nt_hash64_t rid = {0xB000000000000000ULL + (uint64_t)i};
        TEST_ASSERT_EQUAL_INT(NT_OK, nt_resource_register(pid_b, rid, 0U, (uint32_t)(100 + i)));
    }

    /* Resolve pack A's raw slot index so we can assert each returned asset's "pack" field matches it. */
    uint16_t slot_a = UINT16_MAX;
    uint16_t pack_count = nt_resource_pack_count();
    for (uint16_t i = 0; i < pack_count; i++) {
        nt_resource_pack_info_t info;
        if (nt_resource_pack_info(i, &info) && info.id == pid_a.value) {
            slot_a = info.pack_index;
        }
    }
    TEST_ASSERT_NOT_EQUAL(UINT16_MAX, slot_a);

    char req[128];
    (void)snprintf(req, sizeof(req), "{\"method\":\"resource.list\",\"params\":{\"pack_id\":%u,\"include_assets\":true}}", pid_a.value);
    cJSON *root = parse_ok(nt_devapi_submit(req));
    cJSON *r = result_of(root);
    cJSON *assets = cJSON_GetObjectItemCaseSensitive(r, "assets");
    TEST_ASSERT_TRUE(cJSON_IsArray(assets));
    TEST_ASSERT_EQUAL_INT(count_a, cJSON_GetArraySize(assets));
    TEST_ASSERT_EQUAL_INT(count_a, cJSON_GetObjectItemCaseSensitive(r, "asset_total")->valueint);
    cJSON *a = NULL;
    cJSON_ArrayForEach(a, assets) { TEST_ASSERT_EQUAL_INT(slot_a, cJSON_GetObjectItemCaseSensitive(a, "pack")->valueint); }
    cJSON_Delete(root);
}

/* nt_log_add_sink is idempotent and nt_log_remove_sink unregisters. Double-add yields one live
   sink (one fan-out per write); after remove the sink stops receiving. Uses the log ring as the
   observable sink so a single nt_log_write that lands once == one ring entry. */
#if NT_LOG_RING_ENABLED
static void test_log_sink_idempotent_add_and_remove(void) {
    /* setUp already attached nt_log_ring_sink once; a redundant add must NOT create a second slot. */
    nt_log_add_sink(nt_log_ring_sink, NULL);
    nt_log_ring_clear();
    nt_log_write(NT_LOG_LEVEL_INFO, "obs", "dedup-line");
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"log.tail\",\"params\":{\"n\":16}}"));
    cJSON *entries = cJSON_GetObjectItemCaseSensitive(result_of(root), "entries");
    /* one sink -> exactly one ring entry for the single write (a duplicate slot would double it). */
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(entries));
    cJSON_Delete(root);

    /* remove -> the sink stops capturing; re-add for the next test (tearDown doesn't touch sinks). */
    nt_log_remove_sink(nt_log_ring_sink, NULL);
    nt_log_ring_clear();
    nt_log_write(NT_LOG_LEVEL_INFO, "obs", "after-remove");
    cJSON *root2 = parse_ok(nt_devapi_submit("{\"method\":\"log.tail\",\"params\":{\"n\":16}}"));
    cJSON *entries2 = cJSON_GetObjectItemCaseSensitive(result_of(root2), "entries");
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(entries2));
    cJSON_Delete(root2);
    nt_log_add_sink(nt_log_ring_sink, NULL);
}
#endif /* NT_LOG_RING_ENABLED */

/* perf.snapshot frame_ms is wall-clock (1000/fps), distinct from cpu_ms. Inject known
   frames via the overlay test hook so a re-introduced frame_ms==cpu_ms alias is caught. The hook sets
   cpu_ms = last dt*1000 and pushes dt into the fps ring; two unequal dts make the fps-average wall time
   differ from the last cpu_ms. setUp() already inited the overlay (no re-init: init asserts !inited). */
static void test_perf_snapshot_frame_ms_distinct_from_cpu(void) {
    nt_debug_overlay_test_inject_frame(0.005F); /* 5ms  */
    nt_debug_overlay_test_inject_frame(0.020F); /* 20ms -> cpu_ms == 20ms; fps averages both samples */

    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"perf.snapshot\"}"));
    cJSON *r = result_of(root);
    double fps = cJSON_GetObjectItemCaseSensitive(r, "fps")->valuedouble;
    double frame_ms = cJSON_GetObjectItemCaseSensitive(r, "frame_ms")->valuedouble;
    double cpu_ms = cJSON_GetObjectItemCaseSensitive(r, "cpu_ms")->valuedouble;
    TEST_ASSERT_TRUE(fps > 0.0);
    /* frame_ms == 1000/fps (the wall-clock contract), within fp tolerance. */
    TEST_ASSERT_TRUE(frame_ms > 0.0 && (frame_ms - 1000.0 / fps) < 1e-6 && (1000.0 / fps - frame_ms) < 1e-6);
    /* fps-average wall time (~8ms) != last cpu_ms (20ms): proves frame_ms is not an alias of cpu_ms. */
    TEST_ASSERT_TRUE((frame_ms - cpu_ms) > 1e-6 || (cpu_ms - frame_ms) > 1e-6);
    cJSON_Delete(root);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_discovery_lists_obs_commands);
#if NT_LOG_RING_ENABLED
    RUN_TEST(test_log_tail_shape_and_n_cap);
    RUN_TEST(test_log_tail_level_filter);
#endif
    RUN_TEST(test_log_tail_bad_params);
    RUN_TEST(test_perf_snapshot_keys_and_gpu_null);
    RUN_TEST(test_perf_snapshot_user_counters);
#if NT_METRICS_ENABLED
    RUN_TEST(test_perf_stats_channel_shape);
    RUN_TEST(test_perf_stats_gpu_sentinel_null);
#endif
    RUN_TEST(test_perf_stats_empty_window_null_aggregates);
    RUN_TEST(test_perf_stats_bad_params);
#if NT_METRICS_ENABLED
    RUN_TEST(test_perf_reset_ok);
#endif
    RUN_TEST(test_entity_list_total_and_fields);
    RUN_TEST(test_entity_list_only_drawable_filter);
    RUN_TEST(test_entity_list_pagination_and_bad_params);
    RUN_TEST(test_resource_list_packs);
    RUN_TEST(test_resource_list_include_assets_flat);
    RUN_TEST(test_resource_list_bad_params);
    RUN_TEST(test_resource_list_resource_id_hex_string);
    RUN_TEST(test_resource_list_assets_cap_trips);
    RUN_TEST(test_resource_list_pack_id_filters_assets);
#if NT_LOG_RING_ENABLED
    RUN_TEST(test_log_sink_idempotent_add_and_remove);
#endif
    RUN_TEST(test_perf_snapshot_frame_ms_distinct_from_cpu);
    return UNITY_END();
}
