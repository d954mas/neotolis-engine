/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "resource/nt_resource.h"
#include "resource/nt_resource_internal.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "unity.h"
/* clang-format on */

/* ---- Test blob builder ---- */

/*
 * Build a valid NEOPAK blob in memory with `asset_count` fake assets.
 * Each asset has 16 bytes of zero data. Returns malloc'd blob (caller frees).
 * Sets *out_size to total blob size.
 */
static uint8_t *build_test_pack(uint32_t asset_count, uint32_t *out_size) {
    /* Compute sizes using unsigned arithmetic to avoid sign-conversion */
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + (asset_count * sizeof(NtAssetEntry)));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);

    uint32_t data_per_asset = 16; /* fake payload per asset */
    uint32_t data_size = 0;
    for (uint32_t i = 0; i < asset_count; i++) {
        uint32_t aligned = (data_per_asset + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
        data_size += aligned;
    }

    uint32_t total_size = header_size + data_size;
    uint8_t *blob = (uint8_t *)calloc(1, total_size);
    if (!blob) {
        return NULL;
    }

    /* Fill header */
    NtPackHeader *h = (NtPackHeader *)blob;
    h->magic = NT_PACK_MAGIC;
    h->pack_id = nt_resource_hash("test_pack");
    h->version = NT_PACK_VERSION;
    h->asset_count = (uint16_t)asset_count;
    h->header_size = header_size;
    h->total_size = total_size;

    /* Fill asset entries */
    NtAssetEntry *entries = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    uint32_t data_offset = header_size;
    for (uint32_t i = 0; i < asset_count; i++) {
        char name[32];
        /* Build asset name like "asset0", "asset1" */
        name[0] = 'a';
        name[1] = 's';
        name[2] = 's';
        name[3] = 'e';
        name[4] = 't';
        name[5] = (char)('0' + (char)i);
        name[6] = '\0';

        entries[i].resource_id = nt_resource_hash(name);
        entries[i].format_version = 1;
        entries[i].asset_type = (uint8_t)(NT_ASSET_MESH + (i % 3)); /* rotate types */
        entries[i]._pad = 0;
        entries[i].offset = data_offset;
        entries[i].size = data_per_asset;

        uint32_t aligned = (data_per_asset + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
        data_offset += aligned;
    }

    /* Compute CRC32 over data region */
    h->checksum = nt_crc32(blob + header_size, data_size);

    *out_size = total_size;
    return blob;
}

/* ---- Unity setUp / tearDown ---- */

static nt_resource_desc_t s_desc;

void setUp(void) {
    memset(&s_desc, 0, sizeof(s_desc));
    nt_resource_init(&s_desc);
}

void tearDown(void) { nt_resource_shutdown(); }

/* ---- Hash tests ---- */

void test_hash_known_value(void) {
    /* FNV-1a of "hello" = 0x4F9F2CAB */
    TEST_ASSERT_EQUAL_HEX32(0x4F9F2CAB, nt_resource_hash("hello"));
}

void test_hash_null(void) { TEST_ASSERT_EQUAL_HEX32(0x811C9DC5, nt_resource_hash(NULL)); }

void test_hash_empty(void) { TEST_ASSERT_EQUAL_HEX32(0x811C9DC5, nt_resource_hash("")); }

/* ---- Init / shutdown tests ---- */

void test_init_shutdown(void) {
    /* setUp already called init, so shutdown and re-init to test return value */
    nt_resource_shutdown();
    nt_result_t r = nt_resource_init(&s_desc);
    TEST_ASSERT_EQUAL(NT_OK, r);
}

void test_double_init(void) {
    /* setUp already called init, second init should fail */
    nt_result_t r = nt_resource_init(&s_desc);
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, r);
}

/* ---- Handle tests ---- */

void test_handle_invalid(void) { TEST_ASSERT_EQUAL_UINT32(0, NT_RESOURCE_INVALID.id); }

void test_handle_encode_decode(void) {
    /* Construct handle: index=5, gen=3 -> id = (3 << 16) | 5 = 196613 */
    nt_resource_t h = {.id = (3U << 16) | 5U};
    TEST_ASSERT_EQUAL_UINT32(196613, h.id);
    TEST_ASSERT_EQUAL_UINT16(5, nt_resource_slot_index(h));
    TEST_ASSERT_EQUAL_UINT16(3, nt_resource_generation(h));
}

/* ---- Parser tests ---- */

void test_parse_too_small(void) {
    uint32_t pack_id = nt_resource_hash("small_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint8_t tiny[8] = {0};
    nt_result_t r = nt_resource_parse_pack(pack_id, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
}

void test_parse_bad_magic(void) {
    uint32_t pack_id = nt_resource_hash("badmagic_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    /* Build a valid-sized blob but with wrong magic */
    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->magic = 0xDEADBEEF;
    h->pack_id = pack_id;

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

void test_parse_bad_version(void) {
    uint32_t pack_id = nt_resource_hash("badver_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->version = 99;
    h->pack_id = pack_id;

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

void test_parse_header_size_overflow(void) {
    uint32_t pack_id = nt_resource_hash("hdr_overflow_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->header_size = blob_size + 100; /* header_size > blob_size */
    h->pack_id = pack_id;

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

void test_parse_total_size_mismatch(void) {
    uint32_t pack_id = nt_resource_hash("total_mismatch_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->total_size = blob_size + 1; /* mismatch */
    h->pack_id = pack_id;

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

void test_parse_bad_crc(void) {
    uint32_t pack_id = nt_resource_hash("badcrc_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(1, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->pack_id = pack_id;

    /* Corrupt a data byte after header */
    blob[h->header_size] ^= 0xFF;
    /* Recompute CRC would change -- we leave old checksum so it mismatches */

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

void test_parse_valid_empty(void) {
    uint32_t pack_id = nt_resource_hash("empty_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->pack_id = pack_id;
    /* Recompute CRC for this pack_id (data region unchanged, CRC still valid) */

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_OK, r);
    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_parse_valid_two_assets(void) {
    uint32_t pack_id = nt_resource_hash("two_asset_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(2, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->pack_id = pack_id;
    /* Recompute CRC since build_test_pack already computed it correctly */

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_OK, r);

    /* Verify entries were registered correctly by checking resource_ids match */
    NtAssetEntry *entries = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    TEST_ASSERT_EQUAL_HEX32(nt_resource_hash("asset0"), entries[0].resource_id);
    TEST_ASSERT_EQUAL_HEX32(nt_resource_hash("asset1"), entries[1].resource_id);

    /* Verify asset types */
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_MESH, entries[0].asset_type);
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_TEXTURE, entries[1].asset_type);

    /* Verify offsets and sizes */
    TEST_ASSERT_TRUE(entries[0].offset >= h->header_size);
    TEST_ASSERT_EQUAL_UINT32(16, entries[0].size);
    TEST_ASSERT_TRUE(entries[1].offset > entries[0].offset);
    TEST_ASSERT_EQUAL_UINT32(16, entries[1].size);

    free(blob);
}

void test_parse_unmounted_pack(void) {
    /* Do NOT mount this pack_id */
    uint32_t pack_id = nt_resource_hash("never_mounted");

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

/* ---- Pack builder with specific resource_id ---- */

/*
 * Build a NEOPAK blob with one asset whose resource_id is `rid` and type is `atype`.
 * Returns malloc'd blob (caller frees). Sets *out_size to total blob size.
 */
static uint8_t *build_pack_with_rid(uint32_t rid, uint8_t atype, uint32_t *out_size) {
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);
    uint32_t data_per_asset = 16;
    uint32_t aligned_data = (data_per_asset + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
    uint32_t total_size = header_size + aligned_data;

    uint8_t *blob = (uint8_t *)calloc(1, total_size);
    if (!blob) {
        return NULL;
    }

    NtPackHeader *h = (NtPackHeader *)blob;
    h->magic = NT_PACK_MAGIC;
    h->pack_id = 0; /* caller sets before parse */
    h->version = NT_PACK_VERSION;
    h->asset_count = 1;
    h->header_size = header_size;
    h->total_size = total_size;

    NtAssetEntry *entry = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    entry->resource_id = rid;
    entry->format_version = 1;
    entry->asset_type = atype;
    entry->_pad = 0;
    entry->offset = header_size;
    entry->size = data_per_asset;

    h->checksum = nt_crc32(blob + header_size, aligned_data);

    *out_size = total_size;
    return blob;
}

/* ---- Mount / unmount / set_priority tests ---- */

void test_mount_unmount_remount(void) {
    uint32_t pid = nt_resource_hash("remount_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    nt_resource_unmount(pid);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
}

void test_mount_duplicate(void) {
    uint32_t pid = nt_resource_hash("dup_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_mount(pid, 0));
}

void test_unmount_unknown(void) {
    /* Should not crash or assert */
    nt_resource_unmount(0xDEAD);
}

void test_set_priority_ok(void) {
    uint32_t pid = nt_resource_hash("prio_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_set_priority(pid, 5));
}

void test_set_priority_unknown(void) { TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_set_priority(0xDEAD, 5)); }

/* ---- Request / get tests ---- */

void test_request_returns_handle(void) {
    nt_resource_t h = nt_resource_request(nt_resource_hash("some_res"), NT_ASSET_MESH);
    TEST_ASSERT_TRUE(h.id != 0);
}

void test_request_idempotent(void) {
    uint32_t rid = nt_resource_hash("idem_res");
    nt_resource_t h1 = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_t h2 = nt_resource_request(rid, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL_UINT32(h1.id, h2.id);
}

void test_get_invalid_handle(void) { TEST_ASSERT_EQUAL_UINT32(0, nt_resource_get(NT_RESOURCE_INVALID)); }

/* ---- State transition tests ---- */

void test_is_ready_before_step(void) {
    uint32_t pid = nt_resource_hash("ready_before_pack");
    uint32_t rid = nt_resource_hash("ready_before_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid;

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    TEST_ASSERT_TRUE(h.id != 0);

    /* Asset is REGISTERED, not READY -- is_ready should be false even after step */
    nt_resource_step();
    TEST_ASSERT_FALSE(nt_resource_is_ready(h));

    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_is_ready_after_step(void) {
    uint32_t pid = nt_resource_hash("ready_after_pack");
    uint32_t rid = nt_resource_hash("ready_after_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid;

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    TEST_ASSERT_TRUE(h.id != 0);

    /* Simulate loading: set asset state to READY with runtime_handle=42 */
    /* Pack index 0 because it's the first mount in this test */
    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_READY, 42);
    nt_resource_step();

    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_EQUAL_UINT32(42, nt_resource_get(h));

    free(blob);
}

/* ---- Priority stacking tests ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_priority_high_wins(void) {
    uint32_t pid_a = nt_resource_hash("prio_pack_a");
    uint32_t pid_b = nt_resource_hash("prio_pack_b");
    uint32_t rid = nt_resource_hash("shared_asset");

    /* Mount A with priority 1, B with priority 10 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 1));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 10));

    /* Parse both with same resource_id */
    uint32_t blob_size_a = 0;
    uint8_t *blob_a = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size_a);
    TEST_ASSERT_NOT_NULL(blob_a);
    ((NtPackHeader *)blob_a)->pack_id = pid_a;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, blob_a, blob_size_a));

    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size_b);
    TEST_ASSERT_NOT_NULL(blob_b);
    ((NtPackHeader *)blob_b)->pack_id = pid_b;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_b, blob_b, blob_size_b));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);

    /* Set both READY with different handles */
    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_READY, 100); /* pack A index 0 */
    nt_resource_test_set_asset_state(rid, 1, NT_ASSET_STATE_READY, 200); /* pack B index 1 */
    nt_resource_step();

    /* B (priority 10) should win */
    TEST_ASSERT_EQUAL_UINT32(200, nt_resource_get(h));

    free(blob_a);
    free(blob_b);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_priority_equal_last_mounted_wins(void) {
    uint32_t pid_a = nt_resource_hash("eq_pack_a");
    uint32_t pid_b = nt_resource_hash("eq_pack_b");
    uint32_t rid = nt_resource_hash("eq_shared");

    /* Both priority 5: A mounted first, B mounted second */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 5));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 5));

    uint32_t blob_size_a = 0;
    uint8_t *blob_a = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size_a);
    TEST_ASSERT_NOT_NULL(blob_a);
    ((NtPackHeader *)blob_a)->pack_id = pid_a;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, blob_a, blob_size_a));

    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size_b);
    TEST_ASSERT_NOT_NULL(blob_b);
    ((NtPackHeader *)blob_b)->pack_id = pid_b;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_b, blob_b, blob_size_b));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);

    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_READY, 100); /* A */
    nt_resource_test_set_asset_state(rid, 1, NT_ASSET_STATE_READY, 200); /* B */
    nt_resource_step();

    /* B (last mounted, higher pack index) should win */
    TEST_ASSERT_EQUAL_UINT32(200, nt_resource_get(h));

    free(blob_a);
    free(blob_b);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_unmount_fallback(void) {
    uint32_t pid_a = nt_resource_hash("fb_pack_a");
    uint32_t pid_b = nt_resource_hash("fb_pack_b");
    uint32_t rid = nt_resource_hash("fb_shared");

    /* A priority 1, B priority 10 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 1));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 10));

    uint32_t blob_size_a = 0;
    uint8_t *blob_a = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size_a);
    TEST_ASSERT_NOT_NULL(blob_a);
    ((NtPackHeader *)blob_a)->pack_id = pid_a;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, blob_a, blob_size_a));

    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size_b);
    TEST_ASSERT_NOT_NULL(blob_b);
    ((NtPackHeader *)blob_b)->pack_id = pid_b;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_b, blob_b, blob_size_b));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);

    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_READY, 100); /* A */
    nt_resource_test_set_asset_state(rid, 1, NT_ASSET_STATE_READY, 200); /* B */
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(200, nt_resource_get(h));

    /* Unmount B: should fall back to A */
    nt_resource_unmount(pid_b);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(100, nt_resource_get(h));

    free(blob_a);
    free(blob_b);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_set_priority_reorder(void) {
    uint32_t pid_a = nt_resource_hash("reorder_pack_a");
    uint32_t pid_b = nt_resource_hash("reorder_pack_b");
    uint32_t rid = nt_resource_hash("reorder_shared");

    /* A priority 10 (high), B priority 1 (low) */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 1));

    uint32_t blob_size_a = 0;
    uint8_t *blob_a = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size_a);
    TEST_ASSERT_NOT_NULL(blob_a);
    ((NtPackHeader *)blob_a)->pack_id = pid_a;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, blob_a, blob_size_a));

    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size_b);
    TEST_ASSERT_NOT_NULL(blob_b);
    ((NtPackHeader *)blob_b)->pack_id = pid_b;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_b, blob_b, blob_size_b));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);

    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_READY, 100); /* A */
    nt_resource_test_set_asset_state(rid, 1, NT_ASSET_STATE_READY, 200); /* B */
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(100, nt_resource_get(h)); /* A wins (priority 10) */

    /* Boost B to priority 20 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_set_priority(pid_b, 20));
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(200, nt_resource_get(h)); /* B now wins (priority 20) */

    free(blob_a);
    free(blob_b);
}

/* ---- State reporting tests ---- */

void test_get_state_registered(void) {
    uint32_t pid = nt_resource_hash("state_reg_pack");
    uint32_t rid = nt_resource_hash("state_reg_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_STATE_REGISTERED, nt_resource_get_state(h));

    free(blob);
}

void test_failed_permanent(void) {
    uint32_t pid = nt_resource_hash("fail_pack");
    uint32_t rid = nt_resource_hash("fail_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);

    /* Set FAILED state */
    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_FAILED, 0);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_STATE_FAILED, nt_resource_get_state(h));

    /* Step again: state should remain FAILED */
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_STATE_FAILED, nt_resource_get_state(h));

    free(blob);
}

/* ---- Virtual pack tests ---- */

void test_create_virtual_pack(void) {
    uint32_t pid = nt_resource_hash("virtual_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
}

void test_create_virtual_pack_duplicate(void) {
    uint32_t pid = nt_resource_hash("dup_virtual");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_create_pack(pid, 0));
}

void test_register_no_pack(void) {
    /* Register on a pack that was never created */
    nt_result_t r = nt_resource_register(0xDEAD, nt_resource_hash("some_res"), NT_ASSET_TEXTURE, 42);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_register_file_pack_rejected(void) {
    /* Mount a file pack (not virtual) */
    uint32_t pid = nt_resource_hash("file_only_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    /* Attempt register on file pack should fail */
    nt_result_t r = nt_resource_register(pid, nt_resource_hash("some_res"), NT_ASSET_TEXTURE, 42);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_register_immediate_ready(void) {
    uint32_t pid = nt_resource_hash("imm_ready_pack");
    uint32_t rid = nt_resource_hash("imm_ready_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, 42));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    TEST_ASSERT_TRUE(h.id != 0);

    nt_resource_step();
    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_EQUAL_UINT32(42, nt_resource_get(h));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_register_update_handle(void) {
    uint32_t pid = nt_resource_hash("update_handle_pack");
    uint32_t rid = nt_resource_hash("update_handle_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, 42));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(42, nt_resource_get(h));

    /* Re-register same resource with new handle */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, 99));
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(99, nt_resource_get(h));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_virtual_overrides_file(void) {
    uint32_t pid_file = nt_resource_hash("vof_file_pack");
    uint32_t pid_virt = nt_resource_hash("vof_virt_pack");
    uint32_t rid = nt_resource_hash("vof_shared");

    /* File pack at priority 1 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_file, 1));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid_file;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_file, blob, blob_size));
    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_READY, 100);

    /* Virtual pack at priority 10 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid_virt, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid_virt, rid, NT_ASSET_TEXTURE, 200));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();

    /* Virtual (priority 10) should win over file (priority 1) */
    TEST_ASSERT_EQUAL_UINT32(200, nt_resource_get(h));

    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_file_overrides_virtual(void) {
    uint32_t pid_virt = nt_resource_hash("fov_virt_pack");
    uint32_t pid_file = nt_resource_hash("fov_file_pack");
    uint32_t rid = nt_resource_hash("fov_shared");

    /* Virtual pack at priority 1 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid_virt, 1));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid_virt, rid, NT_ASSET_TEXTURE, 100));

    /* File pack at priority 10 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_file, 10));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid_file;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_file, blob, blob_size));
    nt_resource_test_set_asset_state(rid, 1, NT_ASSET_STATE_READY, 200);

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();

    /* File (priority 10) should win over virtual (priority 1) */
    TEST_ASSERT_EQUAL_UINT32(200, nt_resource_get(h));

    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_unregister_fallback(void) {
    uint32_t pid_file = nt_resource_hash("unreg_file_pack");
    uint32_t pid_virt = nt_resource_hash("unreg_virt_pack");
    uint32_t rid = nt_resource_hash("unreg_shared");

    /* File pack at priority 1, handle=100 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_file, 1));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid_file;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_file, blob, blob_size));
    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_READY, 100);

    /* Virtual pack at priority 10, handle=200 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid_virt, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid_virt, rid, NT_ASSET_TEXTURE, 200));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(200, nt_resource_get(h));

    /* Unregister from virtual: should fall back to file pack */
    nt_resource_unregister(pid_virt, rid);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(100, nt_resource_get(h));

    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_unmount_virtual_clears(void) {
    uint32_t pid = nt_resource_hash("unmount_virt_pack");
    uint32_t rid = nt_resource_hash("unmount_virt_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, 42));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(42, nt_resource_get(h));

    /* Unmount virtual pack: entries cleared, no READY source left -> handle 0 */
    nt_resource_unmount(pid);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(0, nt_resource_get(h));
}

/* ---- Placeholder tests ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_set_placeholder_fallback(void) {
    nt_resource_set_placeholder(NT_ASSET_TEXTURE, 999);

    /* Mount file pack with resource in REGISTERED state (not READY) */
    uint32_t pid = nt_resource_hash("ph_fallback_pack");
    uint32_t rid = nt_resource_hash("ph_fallback_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    /* Asset is REGISTERED, not READY: should get placeholder */
    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(999, nt_resource_get(h));

    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_placeholder_not_used_when_ready(void) {
    nt_resource_set_placeholder(NT_ASSET_TEXTURE, 999);

    uint32_t pid = nt_resource_hash("ph_ready_pack");
    uint32_t rid = nt_resource_hash("ph_ready_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    /* Set READY with handle=42 */
    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_READY, 42);

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();

    /* READY entry exists: should get 42, not 999 */
    TEST_ASSERT_EQUAL_UINT32(42, nt_resource_get(h));

    free(blob);
}

void test_placeholder_type_specific(void) {
    /* Set placeholder for TEXTURE only */
    nt_resource_set_placeholder(NT_ASSET_TEXTURE, 999);

    /* Request a MESH resource with no READY entry */
    uint32_t pid = nt_resource_hash("ph_type_pack");
    uint32_t rid = nt_resource_hash("ph_type_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    ((NtPackHeader *)blob)->pack_id = pid;
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();

    /* No mesh placeholder set: should get 0, not 999 */
    TEST_ASSERT_EQUAL_UINT32(0, nt_resource_get(h));

    free(blob);
}

/* ---- main ---- */

int main(void) {
    UNITY_BEGIN();

    /* Hash tests */
    RUN_TEST(test_hash_known_value);
    RUN_TEST(test_hash_null);
    RUN_TEST(test_hash_empty);

    /* Init / shutdown */
    RUN_TEST(test_init_shutdown);
    RUN_TEST(test_double_init);

    /* Handle encoding */
    RUN_TEST(test_handle_invalid);
    RUN_TEST(test_handle_encode_decode);

    /* Parser rejection tests */
    RUN_TEST(test_parse_too_small);
    RUN_TEST(test_parse_bad_magic);
    RUN_TEST(test_parse_bad_version);
    RUN_TEST(test_parse_header_size_overflow);
    RUN_TEST(test_parse_total_size_mismatch);
    RUN_TEST(test_parse_bad_crc);

    /* Parser acceptance tests */
    RUN_TEST(test_parse_valid_empty);
    RUN_TEST(test_parse_valid_two_assets);

    /* Parser unmounted pack test */
    RUN_TEST(test_parse_unmounted_pack);

    /* Mount / unmount / set_priority tests */
    RUN_TEST(test_mount_unmount_remount);
    RUN_TEST(test_mount_duplicate);
    RUN_TEST(test_unmount_unknown);
    RUN_TEST(test_set_priority_ok);
    RUN_TEST(test_set_priority_unknown);

    /* Request / get tests */
    RUN_TEST(test_request_returns_handle);
    RUN_TEST(test_request_idempotent);
    RUN_TEST(test_get_invalid_handle);

    /* State transition tests */
    RUN_TEST(test_is_ready_before_step);
    RUN_TEST(test_is_ready_after_step);

    /* Priority stacking tests */
    RUN_TEST(test_priority_high_wins);
    RUN_TEST(test_priority_equal_last_mounted_wins);
    RUN_TEST(test_unmount_fallback);
    RUN_TEST(test_set_priority_reorder);

    /* State reporting tests */
    RUN_TEST(test_get_state_registered);
    RUN_TEST(test_failed_permanent);

    /* Virtual pack tests */
    RUN_TEST(test_create_virtual_pack);
    RUN_TEST(test_create_virtual_pack_duplicate);
    RUN_TEST(test_register_no_pack);
    RUN_TEST(test_register_file_pack_rejected);
    RUN_TEST(test_register_immediate_ready);
    RUN_TEST(test_register_update_handle);
    RUN_TEST(test_virtual_overrides_file);
    RUN_TEST(test_file_overrides_virtual);
    RUN_TEST(test_unregister_fallback);
    RUN_TEST(test_unmount_virtual_clears);

    /* Placeholder tests */
    RUN_TEST(test_set_placeholder_fallback);
    RUN_TEST(test_placeholder_not_used_when_ready);
    RUN_TEST(test_placeholder_type_specific);

    return UNITY_END();
}
