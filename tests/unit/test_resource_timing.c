#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fs/nt_fs.h"
#include "http/nt_http.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"
#include "resource/nt_resource_internal.h"
#include "time/nt_time.h"
#include "unity.h"

static double s_seconds;
static double s_tick;
static uint32_t s_reads;
static NtPackHeader s_pack;
static const nt_hash32_t s_id = {1};
static uint8_t s_blob[160];
static uint32_t s_activations;
static double s_activation_seconds;
static const char *const s_path_a = "test_resource_timing_a.ntpack";
static const char *const s_path_b = "test_resource_timing_b.ntpack";

static void make_blob(void) {
    memset(s_blob, 0, sizeof s_blob);
    NtPackHeader *h = (NtPackHeader *)s_blob;
    *h = (NtPackHeader){.magic = NT_PACK_MAGIC, .version = NT_PACK_VERSION, .asset_count = 3, .header_size = 104, .total_size = sizeof s_blob, .meta_count = 1, .meta_offset = 136};
    NtAssetEntry *entries = (NtAssetEntry *)(s_blob + sizeof *h);
    for (uint32_t i = 0; i < 3; ++i) {
        entries[i] = (NtAssetEntry){.resource_id = 101U + i, .offset = i == 2 ? 120U : 104U, .size = 16, .asset_type = NT_ASSET_MESH};
    }
    entries[0].meta_offset = h->meta_offset;
    s_blob[104] = 1;
    s_blob[120] = 2;
    const NtMetaEntryHeader meta = {.resource_id = 101, .kind = 7, .size = 4};
    memcpy(s_blob + h->meta_offset, &meta, sizeof meta);
    s_blob[156] = 42;
    h->checksum = nt_crc32(s_blob + h->header_size, h->total_size - h->header_size);
}

static void write_pack(const char *path, const void *data, uint32_t size) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    size_t written = fwrite(data, 1, size, file);
    int closed = fclose(file);
    TEST_ASSERT_EQUAL_UINT32(size, (uint32_t)written);
    TEST_ASSERT_EQUAL_INT(0, closed);
}

static uint32_t activate(const uint8_t *data, uint32_t size) {
    TEST_ASSERT_EQUAL_UINT32(16, size);
    ++s_activations;
    s_seconds += s_activation_seconds;
    return 100U + data[0];
}

static void resolve(const uint8_t *data, uint32_t size, uint32_t handle, void **user_data) {
    (void)data;
    (void)size;
    (void)handle;
    (void)user_data;
    s_seconds += 0.5;
}

static void cleanup(void *user_data) { (void)user_data; }

double nt_time_now(void) {
    ++s_reads;
    s_seconds += s_tick;
    return s_seconds;
}

void setUp(void) {
    s_seconds = 0.0;
    s_tick = 0.125;
    s_reads = 0;
    s_activations = 0;
    s_activation_seconds = 0.0;
    (void)remove(s_path_a);
    (void)remove(s_path_b);
    nt_http_init();
    nt_fs_init();
    make_blob();
    const nt_resource_desc_t desc = {0};
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_init(&desc));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(s_id, 0));
    s_pack = (NtPackHeader){.magic = NT_PACK_MAGIC, .version = NT_PACK_VERSION, .header_size = sizeof s_pack, .total_size = sizeof s_pack};
    s_pack.checksum = nt_crc32((const uint8_t *)&s_pack + sizeof s_pack, 0);
}

void tearDown(void) {
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    (void)remove(s_path_a);
    (void)remove(s_path_b);
}

static void assert_parse(int32_t parse_ms, int32_t crc_ms) {
#if !NT_RESOURCE_TIMING_ENABLED
    parse_ms = 0;
    crc_ms = 0;
#endif
    TEST_ASSERT_TRUE(nt_resource_get_last_parse_ms() == (float)parse_ms);
    TEST_ASSERT_TRUE(nt_resource_get_last_crc_ms() == (float)crc_ms);
}

static void test_direct_parse_and_rejection_replace_last_result(void) {
    assert_parse(0, 0);
    TEST_ASSERT_TRUE(nt_resource_get_last_step_ms() == 0.0F);
    TEST_ASSERT_TRUE(nt_resource_get_last_activate_ms() == 0.0F);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(s_id, (const uint8_t *)&s_pack, sizeof s_pack));
    assert_parse(375, 125);
    s_tick = 0.25;
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_parse_pack(s_id, (const uint8_t *)&s_pack, sizeof s_pack));
    assert_parse(250, 0);
#if NT_RESOURCE_TIMING_ENABLED
    TEST_ASSERT_EQUAL_UINT32(6, s_reads);
#else
    TEST_ASSERT_EQUAL_UINT32(0, s_reads);
#endif
}

static void assert_step(int32_t activate_ms, int32_t step_ms) {
#if !NT_RESOURCE_TIMING_ENABLED
    activate_ms = 0;
    step_ms = 0;
#endif
    TEST_ASSERT_TRUE(nt_resource_get_last_activate_ms() == (float)activate_ms);
    TEST_ASSERT_TRUE(nt_resource_get_last_step_ms() == (float)step_ms);
}

static void assert_bytes(uint64_t blob, uint64_t meta) {
    uint32_t reads = s_reads;
    nt_resource_resident_bytes_t bytes = nt_resource_get_resident_bytes();
    TEST_ASSERT_EQUAL_UINT64(blob, bytes.blob_bytes);
    TEST_ASSERT_EQUAL_UINT64(meta, bytes.metadata_bytes);
    TEST_ASSERT_EQUAL_UINT32(reads, s_reads);
}

static void test_crc_and_entry_failures_publish_completed_parse(void) {
    NtPackHeader *h = (NtPackHeader *)s_blob;
    h->checksum ^= 1U;
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_parse_pack(s_id, s_blob, sizeof s_blob));
    assert_parse(375, 125);
    make_blob();
    s_tick = 0.25;
    NtAssetEntry *entries = (NtAssetEntry *)(s_blob + sizeof *h);
    entries[0].offset = 0;
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_parse_pack(s_id, s_blob, sizeof s_blob));
    assert_parse(750, 250);
    TEST_ASSERT_EQUAL_UINT16(0, nt_resource_asset_count());
    s_blob[0] = 0;
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_parse_pack(s_id, s_blob, sizeof s_blob));
    assert_parse(250, 0);
    assert_bytes(0, 0);
}

static void test_idle_steps_refresh_step_and_preserve_parse(void) {
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(s_id, s_blob, sizeof s_blob));
    s_reads = 0;
    nt_resource_step();
    assert_parse(375, 125);
    assert_step(125, 500);
#if NT_RESOURCE_TIMING_ENABLED
    TEST_ASSERT_EQUAL_UINT32(5, s_reads);
#else
    TEST_ASSERT_EQUAL_UINT32(2, s_reads);
#endif
    s_tick = 0.25;
    nt_resource_step();
    assert_parse(375, 125);
    assert_step(250, 1000);
    nt_resource_shutdown();
    assert_parse(0, 0);
    assert_step(0, 0);
    assert_bytes(0, 0);
    s_reads = 0;
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(0, s_reads);
}

static void test_budget_dedup_and_resolve_keep_their_phase_boundaries(void) {
    s_tick = 0.0;
    s_activation_seconds = 0.25;
    nt_resource_set_activate_time_budget(125.0F);
    nt_resource_set_activator(NT_ASSET_MESH, activate, NULL);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, resolve, cleanup);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(s_id, s_blob, sizeof s_blob));
    nt_resource_t first = nt_resource_request((nt_hash64_t){101}, NT_ASSET_MESH);
    nt_resource_t last = nt_resource_request((nt_hash64_t){103}, NT_ASSET_MESH);
    s_reads = 0;
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(1, s_activations);
    TEST_ASSERT_EQUAL_UINT32(101, nt_resource_get(first));
    TEST_ASSERT_FALSE(nt_resource_is_ready(last));
    assert_step(250, 750);
#if NT_RESOURCE_TIMING_ENABLED
    TEST_ASSERT_EQUAL_UINT32(7, s_reads);
#else
    TEST_ASSERT_EQUAL_UINT32(4, s_reads);
#endif
    nt_resource_set_activate_time_budget(0.0F);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(2, s_activations);
    TEST_ASSERT_EQUAL_UINT32(102, nt_resource_get(last));
    assert_step(250, 750);
    nt_resource_t duplicate = nt_resource_request((nt_hash64_t){102}, NT_ASSET_MESH);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(nt_resource_get(first), nt_resource_get(duplicate));
    TEST_ASSERT_EQUAL_UINT32(2, s_activations);
    assert_step(0, 500);
}

static void test_multiple_parses_keep_last_result_not_sum(void) {
    const nt_hash32_t other = {2};
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(other, 0));
    write_pack(s_path_a, s_blob, sizeof s_blob);
    s_pack.magic = 0;
    write_pack(s_path_b, &s_pack, sizeof s_pack);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(s_id, s_path_a));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(other, s_path_b));
    nt_resource_step();
    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(s_id));
    TEST_ASSERT_EQUAL(NT_PACK_STATE_FAILED, nt_resource_pack_state(other));
    TEST_ASSERT_EQUAL_UINT16(3, nt_resource_asset_count());
    assert_parse(125, 0);
    nt_resource_step();
    assert_parse(125, 0);
}

static void test_resident_bytes_eviction_reload_and_unmount(void) {
    const nt_hash32_t other = {2};
    assert_bytes(0, 0);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(other, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(other, (const uint8_t *)&s_pack, sizeof s_pack));
    write_pack(s_path_a, s_blob, sizeof s_blob);
    nt_resource_set_activator(NT_ASSET_MESH, activate, NULL);
    nt_resource_set_activate_time_budget(0.0F);
    nt_resource_t first = nt_resource_request((nt_hash64_t){101}, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(s_id, s_path_a));
    nt_resource_step();
    assert_bytes(sizeof s_blob + sizeof s_pack, 24);
    assert_parse(375, 125);
    TEST_ASSERT_EQUAL_UINT32(2, s_activations);
    nt_resource_set_blob_policy(s_id, NT_BLOB_AUTO, 1000);
    s_tick = 0.0;
    s_seconds += 0.5;
    nt_resource_step();
    assert_bytes(sizeof s_blob + sizeof s_pack, 24);
    s_seconds += 1.0;
    nt_resource_step();
    assert_bytes(sizeof s_pack, 24);
    uint32_t size = 0;
    const uint8_t *meta = nt_resource_get_meta(first, (nt_hash64_t){7}, &size);
    TEST_ASSERT_NOT_NULL(meta);
    TEST_ASSERT_EQUAL_UINT32(4, size);
    TEST_ASSERT_EQUAL_UINT8(42, meta[0]);
    nt_resource_invalidate(NT_ASSET_MESH);
    nt_resource_step();
    assert_bytes(sizeof s_blob + sizeof s_pack, 24);
    assert_parse(375, 125);
    TEST_ASSERT_EQUAL_UINT32(4, s_activations);
    TEST_ASSERT_TRUE(nt_resource_is_ready(first));
    nt_resource_unmount(s_id);
    assert_bytes(sizeof s_pack, 0);
    nt_resource_unmount(other);
    assert_bytes(0, 0);
}

static void test_retry_waits_until_deadline(void) {
    s_tick = 0.0;
    s_seconds = 1.0;
    nt_resource_set_retry_policy(3, 1000, 1000);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(s_id, s_path_a));
    nt_resource_step();
    TEST_ASSERT_EQUAL(NT_PACK_STATE_NONE, nt_resource_pack_state(s_id));
    write_pack(s_path_a, &s_pack, sizeof s_pack);
    s_seconds = 1.5;
    nt_resource_step();
    TEST_ASSERT_EQUAL(NT_PACK_STATE_NONE, nt_resource_pack_state(s_id));
    s_seconds = 2.0;
    nt_resource_step();
    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(s_id));
    assert_bytes(sizeof s_pack, 0);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_direct_parse_and_rejection_replace_last_result);
    RUN_TEST(test_crc_and_entry_failures_publish_completed_parse);
    RUN_TEST(test_idle_steps_refresh_step_and_preserve_parse);
    RUN_TEST(test_budget_dedup_and_resolve_keep_their_phase_boundaries);
    RUN_TEST(test_multiple_parses_keep_last_result_not_sum);
    RUN_TEST(test_resident_bytes_eviction_reload_and_unmount);
    RUN_TEST(test_retry_waits_until_deadline);
    return UNITY_END();
}
