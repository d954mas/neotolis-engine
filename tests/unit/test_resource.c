/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "resource/nt_resource.h"
#include "resource/nt_resource_internal.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "fs/nt_fs.h"
#include "time/nt_time.h"
#include "nt_blob_format.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "core/nt_assert.h"
#include "unity.h"
/* clang-format on */

#include <setjmp.h>

/* ---- Assert-catching helper (setjmp/longjmp via hookable nt_assert_handler) ---- */

static jmp_buf s_assert_jmp;

static void test_assert_trap(const char *expr, const char *file, int line) {
    (void)expr;
    (void)file;
    (void)line;
    longjmp(s_assert_jmp, 1);
}

#define EXPECT_ASSERT(code)                                                                                                                                                                            \
    do {                                                                                                                                                                                               \
        nt_assert_handler = test_assert_trap;                                                                                                                                                          \
        if (setjmp(s_assert_jmp) == 0) {                                                                                                                                                               \
            code;                                                                                                                                                                                      \
            nt_assert_handler = NULL;                                                                                                                                                                  \
            TEST_FAIL_MESSAGE("Expected NT_ASSERT to fire");                                                                                                                                           \
        }                                                                                                                                                                                              \
        nt_assert_handler = NULL;                                                                                                                                                                      \
    } while (0)

/* ---- Test blob builder ---- */

/*
 * Build a valid NTPACK blob in memory with `asset_count` fake assets.
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
    h->version = NT_PACK_VERSION;
    h->asset_count = (uint16_t)asset_count;
    h->header_size = header_size;
    h->total_size = total_size;

    /* Fill asset entries */
    NtAssetEntry *entries = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    uint32_t data_offset = header_size;
    for (uint32_t i = 0; i < asset_count; i++) {
        char name[32];
        (void)snprintf(name, sizeof(name), "asset%u", i);

        entries[i].resource_id = nt_hash64_str(name).value;
        entries[i].format_version = 1;
        entries[i].asset_type = (uint8_t)(NT_ASSET_MESH + (i % 3)); /* rotate types */
        entries[i]._pad = 0;
        entries[i].meta_offset = 0;
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
    nt_http_init();
    nt_fs_init();
    memset(&s_desc, 0, sizeof(s_desc));
    nt_resource_init(&s_desc);
}

void tearDown(void) {
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
}

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
    nt_hash32_t pack_id = nt_hash32_str("small_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint8_t tiny[8] = {0};
    nt_result_t r = nt_resource_parse_pack(pack_id, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
}

void test_parse_bad_magic(void) {
    nt_hash32_t pack_id = nt_hash32_str("badmagic_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    /* Build a valid-sized blob but with wrong magic */
    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->magic = 0xDEADBEEF;

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

void test_parse_bad_version(void) {
    nt_hash32_t pack_id = nt_hash32_str("badver_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->version = 99;
    /* Recompute CRC after changing version */
    h->checksum = nt_crc32(blob + h->header_size, blob_size - h->header_size);

    EXPECT_ASSERT(nt_resource_parse_pack(pack_id, blob, blob_size));
    free(blob);
}

void test_parse_header_size_overflow(void) {
    nt_hash32_t pack_id = nt_hash32_str("hdr_overflow_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->header_size = blob_size + 100; /* header_size > blob_size */

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

void test_parse_total_size_mismatch(void) {
    nt_hash32_t pack_id = nt_hash32_str("total_mismatch_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->total_size = blob_size + 1; /* mismatch */

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

void test_parse_bad_crc(void) {
    nt_hash32_t pack_id = nt_hash32_str("badcrc_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(1, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;

    /* Corrupt a data byte after header */
    blob[h->header_size] ^= 0xFF;
    /* Recompute CRC would change -- we leave old checksum so it mismatches */

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

void test_parse_valid_empty(void) {
    nt_hash32_t pack_id = nt_hash32_str("empty_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_OK, r);
    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_parse_valid_two_assets(void) {
    nt_hash32_t pack_id = nt_hash32_str("two_asset_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(2, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    /* Recompute CRC since build_test_pack already computed it correctly */

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_OK, r);

    /* Verify entries were registered correctly by checking resource_ids match */
    NtAssetEntry *entries = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    TEST_ASSERT_TRUE(entries[0].resource_id == nt_hash64_str("asset0").value);
    TEST_ASSERT_TRUE(entries[1].resource_id == nt_hash64_str("asset1").value);

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
    nt_hash32_t pack_id = nt_hash32_str("never_mounted");

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

/* ---- Pack builder with specific resource_id ---- */

/*
 * Build a NTPACK blob with one asset whose resource_id is `rid` and type is `atype`.
 * Returns malloc'd blob (caller frees). Sets *out_size to total blob size.
 */
static uint8_t *build_pack_with_rid(uint64_t rid, uint8_t atype, uint32_t *out_size) {
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
    h->version = NT_PACK_VERSION;
    h->asset_count = 1;
    h->header_size = header_size;
    h->total_size = total_size;

    NtAssetEntry *entry = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    entry->resource_id = rid;
    entry->format_version = 1;
    entry->asset_type = atype;
    entry->_pad = 0;
    entry->meta_offset = 0;
    entry->offset = header_size;
    entry->size = data_per_asset;

    h->checksum = nt_crc32(blob + header_size, aligned_data);

    *out_size = total_size;
    return blob;
}

/* ---- Mount / unmount / set_priority tests ---- */

void test_mount_unmount_remount(void) {
    nt_hash32_t pid = nt_hash32_str("remount_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    nt_resource_unmount(pid);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
}

void test_mount_duplicate(void) {
    nt_hash32_t pid = nt_hash32_str("dup_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_mount(pid, 0));
}

void test_unmount_unknown(void) {
    /* Should not crash or assert */
    nt_resource_unmount((nt_hash32_t){0xDEAD});
}

void test_set_priority_ok(void) {
    nt_hash32_t pid = nt_hash32_str("prio_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_set_priority(pid, 5));
}

void test_set_priority_unknown(void) { TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_set_priority((nt_hash32_t){0xDEAD}, 5)); }

/* ---- Request / get tests ---- */

void test_request_returns_handle(void) {
    nt_resource_t h = nt_resource_request(nt_hash64_str("some_res"), NT_ASSET_MESH);
    TEST_ASSERT_TRUE(h.id != 0);
}

void test_request_idempotent(void) {
    nt_hash64_t rid = nt_hash64_str("idem_res");
    nt_resource_t h1 = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_t h2 = nt_resource_request(rid, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL_UINT32(h1.id, h2.id);
}

void test_get_invalid_handle(void) { TEST_ASSERT_EQUAL_UINT32(0, nt_resource_get(NT_RESOURCE_INVALID)); }

/* ---- State transition tests ---- */

void test_is_ready_before_step(void) {
    nt_hash32_t pid = nt_hash32_str("ready_before_pack");
    nt_hash64_t rid = nt_hash64_str("ready_before_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

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
    nt_hash32_t pid = nt_hash32_str("ready_after_pack");
    nt_hash64_t rid = nt_hash64_str("ready_after_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

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
    nt_hash32_t pid_a = nt_hash32_str("prio_pack_a");
    nt_hash32_t pid_b = nt_hash32_str("prio_pack_b");
    nt_hash64_t rid = nt_hash64_str("shared_asset");

    /* Mount A with priority 1, B with priority 10 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 1));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 10));

    /* Parse both with same resource_id */
    uint32_t blob_size_a = 0;
    uint8_t *blob_a = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size_a);
    TEST_ASSERT_NOT_NULL(blob_a);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, blob_a, blob_size_a));

    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size_b);
    TEST_ASSERT_NOT_NULL(blob_b);
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
    nt_hash32_t pid_a = nt_hash32_str("eq_pack_a");
    nt_hash32_t pid_b = nt_hash32_str("eq_pack_b");
    nt_hash64_t rid = nt_hash64_str("eq_shared");

    /* Both priority 5: A mounted first, B mounted second */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 5));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 5));

    uint32_t blob_size_a = 0;
    uint8_t *blob_a = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size_a);
    TEST_ASSERT_NOT_NULL(blob_a);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, blob_a, blob_size_a));

    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size_b);
    TEST_ASSERT_NOT_NULL(blob_b);
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
    nt_hash32_t pid_a = nt_hash32_str("fb_pack_a");
    nt_hash32_t pid_b = nt_hash32_str("fb_pack_b");
    nt_hash64_t rid = nt_hash64_str("fb_shared");

    /* A priority 1, B priority 10 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 1));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 10));

    uint32_t blob_size_a = 0;
    uint8_t *blob_a = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size_a);
    TEST_ASSERT_NOT_NULL(blob_a);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, blob_a, blob_size_a));

    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size_b);
    TEST_ASSERT_NOT_NULL(blob_b);
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
    nt_hash32_t pid_a = nt_hash32_str("reorder_pack_a");
    nt_hash32_t pid_b = nt_hash32_str("reorder_pack_b");
    nt_hash64_t rid = nt_hash64_str("reorder_shared");

    /* A priority 10 (high), B priority 1 (low) */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 1));

    uint32_t blob_size_a = 0;
    uint8_t *blob_a = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size_a);
    TEST_ASSERT_NOT_NULL(blob_a);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, blob_a, blob_size_a));

    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size_b);
    TEST_ASSERT_NOT_NULL(blob_b);
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
    nt_hash32_t pid = nt_hash32_str("state_reg_pack");
    nt_hash64_t rid = nt_hash64_str("state_reg_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_STATE_REGISTERED, nt_resource_get_state(h));

    free(blob);
}

void test_failed_permanent(void) {
    nt_hash32_t pid = nt_hash32_str("fail_pack");
    nt_hash64_t rid = nt_hash64_str("fail_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
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
    nt_hash32_t pid = nt_hash32_str("virtual_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
}

void test_create_virtual_pack_duplicate(void) {
    nt_hash32_t pid = nt_hash32_str("dup_virtual");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_resource_create_pack(pid, 0));
}

void test_register_no_pack(void) {
    /* Register on a pack that was never created */
    nt_result_t r = nt_resource_register((nt_hash32_t){0xDEAD}, nt_hash64_str("some_res"), NT_ASSET_TEXTURE, 42);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_register_file_pack_rejected(void) {
    /* Mount a file pack (not virtual) */
    nt_hash32_t pid = nt_hash32_str("file_only_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    /* Attempt register on file pack should fail */
    nt_result_t r = nt_resource_register(pid, nt_hash64_str("some_res"), NT_ASSET_TEXTURE, 42);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_register_immediate_ready(void) {
    nt_hash32_t pid = nt_hash32_str("imm_ready_pack");
    nt_hash64_t rid = nt_hash64_str("imm_ready_res");

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
    nt_hash32_t pid = nt_hash32_str("update_handle_pack");
    nt_hash64_t rid = nt_hash64_str("update_handle_res");

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

void test_publication_epoch_stable_when_no_slot_publication_changes(void) {
    nt_hash32_t pid = nt_hash32_str("epoch_stable_pack");
    nt_hash64_t rid = nt_hash64_str("epoch_stable_res");

    TEST_ASSERT_EQUAL_UINT32(0, nt_resource_publication_epoch());
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, 42));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    TEST_ASSERT_TRUE(h.id != 0);

    nt_resource_step();
    uint32_t epoch_after_publish = nt_resource_publication_epoch();
    TEST_ASSERT_TRUE(epoch_after_publish > 0);

    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(epoch_after_publish, nt_resource_publication_epoch());
}

void test_publication_epoch_increments_on_winner_change(void) {
    nt_hash32_t pid = nt_hash32_str("epoch_change_pack");
    nt_hash64_t rid = nt_hash64_str("epoch_change_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, 42));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();
    uint32_t epoch_after_first_publish = nt_resource_publication_epoch();

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, 99));
    nt_resource_step();

    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_EQUAL_UINT32(99, nt_resource_get(h));
    TEST_ASSERT_TRUE(nt_resource_publication_epoch() > epoch_after_first_publish);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_virtual_overrides_file(void) {
    nt_hash32_t pid_file = nt_hash32_str("vof_file_pack");
    nt_hash32_t pid_virt = nt_hash32_str("vof_virt_pack");
    nt_hash64_t rid = nt_hash64_str("vof_shared");

    /* File pack at priority 1 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_file, 1));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
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
    nt_hash32_t pid_virt = nt_hash32_str("fov_virt_pack");
    nt_hash32_t pid_file = nt_hash32_str("fov_file_pack");
    nt_hash64_t rid = nt_hash64_str("fov_shared");

    /* Virtual pack at priority 1 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid_virt, 1));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid_virt, rid, NT_ASSET_TEXTURE, 100));

    /* File pack at priority 10 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_file, 10));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
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
    nt_hash32_t pid_file = nt_hash32_str("unreg_file_pack");
    nt_hash32_t pid_virt = nt_hash32_str("unreg_virt_pack");
    nt_hash64_t rid = nt_hash64_str("unreg_shared");

    /* File pack at priority 1, handle=100 */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_file, 1));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
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
    nt_hash32_t pid = nt_hash32_str("unmount_virt_pack");
    nt_hash64_t rid = nt_hash64_str("unmount_virt_res");

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
    /* Register placeholder texture via virtual pack */
    nt_hash32_t ph_pid = nt_hash32_str("ph_vpack");
    nt_hash64_t ph_rid = nt_hash64_str("placeholder_tex");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(ph_pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(ph_pid, ph_rid, NT_ASSET_TEXTURE, 999));
    nt_resource_set_placeholder_texture(ph_rid);

    /* Mount file pack with resource in REGISTERED state (not READY) */
    nt_hash32_t pid = nt_hash32_str("ph_fallback_pack");
    nt_hash64_t rid = nt_hash64_str("ph_fallback_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    /* Asset is REGISTERED, not READY: should get placeholder handle */
    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(999, nt_resource_get(h));

    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_placeholder_not_used_when_ready(void) {
    /* Register placeholder texture via virtual pack */
    nt_hash32_t ph_pid = nt_hash32_str("ph_ready_vpack");
    nt_hash64_t ph_rid = nt_hash64_str("placeholder_tex2");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(ph_pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(ph_pid, ph_rid, NT_ASSET_TEXTURE, 999));
    nt_resource_set_placeholder_texture(ph_rid);

    nt_hash32_t pid = nt_hash32_str("ph_ready_pack");
    nt_hash64_t rid = nt_hash64_str("ph_ready_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_TEXTURE, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    /* Set READY with handle=42 (pack_index=1, file pack after virtual pack at 0) */
    nt_resource_test_set_asset_state(rid, 1, NT_ASSET_STATE_READY, 42);

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();

    /* READY entry exists: should get 42, not placeholder 999 */
    TEST_ASSERT_EQUAL_UINT32(42, nt_resource_get(h));

    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_placeholder_type_specific(void) {
    /* Register placeholder texture via virtual pack */
    nt_hash32_t ph_pid = nt_hash32_str("ph_type_vpack");
    nt_hash64_t ph_rid = nt_hash64_str("placeholder_tex3");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(ph_pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(ph_pid, ph_rid, NT_ASSET_TEXTURE, 999));
    nt_resource_set_placeholder_texture(ph_rid);

    /* Request a MESH resource with no READY entry */
    nt_hash32_t pid = nt_hash32_str("ph_type_pack");
    nt_hash64_t rid = nt_hash64_str("ph_type_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();

    /* No mesh placeholder set: should get 0, not 999 */
    TEST_ASSERT_EQUAL_UINT32(0, nt_resource_get(h));

    free(blob);
}

/* ---- Entries bounds check test ---- */

void test_parse_entries_overflow(void) {
    nt_hash32_t pack_id = nt_hash32_str("entries_overflow_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack_id, 0));

    /* Build a valid blob with 0 assets, then lie about asset_count */
    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(0, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->asset_count = 9999; /* header region can't hold 9999 entries */

    nt_result_t r = nt_resource_parse_pack(pack_id, blob, blob_size);
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, r);
    free(blob);
}

/* ---- Asset slot reuse test ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_asset_slot_reuse(void) {
    /* Mount pack A, parse 2 assets, unmount -> creates holes */
    nt_hash32_t pid_a = nt_hash32_str("reuse_pack_a");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 0));

    uint32_t blob_size_a = 0;
    uint8_t *blob_a = build_test_pack(2, &blob_size_a);
    TEST_ASSERT_NOT_NULL(blob_a);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, blob_a, blob_size_a));

    nt_resource_unmount(pid_a);

    /* Mount pack B, parse 2 assets -> should reuse holes from A */
    nt_hash32_t pid_b = nt_hash32_str("reuse_pack_b");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 0));

    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_test_pack(2, &blob_size_b);
    TEST_ASSERT_NOT_NULL(blob_b);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_b, blob_b, blob_size_b));

    /* Request and verify assets from pack B resolve correctly */
    nt_hash64_t rid = nt_hash64_str("asset0");
    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    TEST_ASSERT_TRUE(h.id != 0);

    nt_resource_test_set_asset_state(rid, 0, NT_ASSET_STATE_READY, 77);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(77, nt_resource_get(h));

    free(blob_a);
    free(blob_b);
}

/* ---- Fake activator for testing ---- */

static uint32_t s_activate_call_count;
static uint32_t s_deactivate_call_count;
static uint32_t s_last_deactivated_handle;

static uint32_t fake_activate(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    s_activate_call_count++;
    return 0xBEEF;
}

static void fake_deactivate(uint32_t runtime_handle) {
    s_deactivate_call_count++;
    s_last_deactivated_handle = runtime_handle;
}

static uint32_t fake_activate_fail(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    s_activate_call_count++;
    return 0; /* failure */
}

static uint32_t s_next_handle = 0xBEEF;

static uint32_t fake_activate_seq(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    s_activate_call_count++;
    return s_next_handle++;
}

/* ---- Mock on_resolve / on_cleanup for user_data tests ---- */

static uint32_t s_resolve_call_count;
static uint32_t s_cleanup_call_count;
static uint32_t s_post_resolve_call_count;
static void *s_last_resolve_user_data;
static uint32_t s_last_resolve_handle;
static uint32_t s_last_resolve_size;
static nt_resource_t s_last_post_resolve_requested;
static uint64_t s_post_resolve_dependency_rid;

static void reset_resolve_state(void) {
    s_resolve_call_count = 0;
    s_cleanup_call_count = 0;
    s_post_resolve_call_count = 0;
    s_last_resolve_user_data = NULL;
    s_last_resolve_handle = 0;
    s_last_resolve_size = 0;
    s_last_post_resolve_requested = NT_RESOURCE_INVALID;
    s_post_resolve_dependency_rid = 0;
}

static void mock_on_resolve(const uint8_t *data, uint32_t size, uint32_t runtime_handle, void **user_data) {
    (void)data;
    s_resolve_call_count++;
    s_last_resolve_handle = runtime_handle;
    s_last_resolve_size = size;
    if (*user_data == NULL) {
        /* First activation -- allocate */
        *user_data = malloc(sizeof(uint32_t));
        *(uint32_t *)*user_data = 42;
    } else {
        /* Re-activation (merge) -- increment */
        *(uint32_t *)*user_data += 1;
    }
    s_last_resolve_user_data = *user_data;
}

static void mock_on_cleanup(void *user_data) {
    s_cleanup_call_count++;
    free(user_data);
}

static void mock_on_post_resolve(const uint8_t *data, uint32_t size, nt_resource_t handle, uint32_t runtime_handle, void *user_data) {
    (void)data;
    (void)size;
    (void)handle;
    (void)runtime_handle;
    (void)user_data;
    s_post_resolve_call_count++;
    if (s_post_resolve_dependency_rid != 0) {
        s_last_post_resolve_requested = nt_resource_request((nt_hash64_t){s_post_resolve_dependency_rid}, NT_ASSET_TEXTURE);
    }
}

/* ---- Test pack file writer ---- */

static void write_test_pack_file(const char *path, uint64_t rid, uint8_t atype) {
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid, atype, &blob_size);
    if (!blob) {
        return;
    }

    FILE *f = fopen(path, "wb");
    if (f) {
        (void)fwrite(blob, 1, blob_size, f);
        (void)fclose(f);
    }
    free(blob);
}

/* ---- Pack loading tests ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_load_file_transitions_state(void) {
    nt_hash32_t pid = nt_hash32_str("load_file_pack");
    nt_hash64_t rid = nt_hash64_str("load_file_res");

    write_test_pack_file("build/test_pack_load.ntpack", rid.value, NT_ASSET_MESH);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_PACK_STATE_NONE, nt_resource_pack_state(pid));

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_pack_load.ntpack"));
    /* On native, nt_fs reads synchronously, so state should be REQUESTED
     * (nt_fs returns immediately with DONE state) */
    nt_pack_state_t st = nt_resource_pack_state(pid);
    TEST_ASSERT_TRUE(st == NT_PACK_STATE_REQUESTED || st == NT_PACK_STATE_LOADED);

    /* Step to poll I/O and parse */
    nt_resource_step();
    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(pid));

    (void)remove("build/test_pack_load.ntpack");
}

void test_load_file_nonexistent(void) {
    nt_hash32_t pid = nt_hash32_str("load_nofile_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    /* Set retry to 1 so it fails permanently */
    nt_resource_set_retry_policy(1, 100, 1000);

    nt_result_t r = nt_resource_load_file(pid, "nonexistent_file_that_does_not_exist.ntpack");
    /* On native nt_fs, reading a non-existent file returns a valid request
     * but with FAILED state. The load_file still returns OK because the
     * request ID is non-zero; the failure is detected in resource_step. */
    if (r == NT_OK) {
        nt_resource_step();
        TEST_ASSERT_EQUAL(NT_PACK_STATE_FAILED, nt_resource_pack_state(pid));
    } else {
        /* If nt_fs_read_file returned INVALID (req.id == 0), load_file returns error */
        TEST_ASSERT_EQUAL(NT_PACK_STATE_FAILED, nt_resource_pack_state(pid));
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_pack_state_api(void) {
    nt_hash32_t pid = nt_hash32_str("state_api_pack");
    nt_hash64_t rid = nt_hash64_str("state_api_res");

    /* Not mounted: NONE */
    TEST_ASSERT_EQUAL(NT_PACK_STATE_NONE, nt_resource_pack_state(pid));

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_PACK_STATE_NONE, nt_resource_pack_state(pid));

    /* Load via direct parse (manual): NONE -> directly to READY via parse_pack + set_asset_state */
    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    /* After parse_pack, pack_state is READY (assets registered, blob available) */
    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(pid));

    free(blob);
}

void test_pack_progress(void) {
    nt_hash32_t pid = nt_hash32_str("progress_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t received = 99;
    uint32_t total = 99;
    nt_resource_pack_progress(pid, &received, &total);
    /* On native, no progress tracking -- both 0 */
    TEST_ASSERT_EQUAL_UINT32(0, received);
    TEST_ASSERT_EQUAL_UINT32(0, total);
}

/* ---- Activator system tests ---- */

void test_set_activator(void) {
    /* Just verify it doesn't crash */
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_activation_called_on_step(void) {
    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);

    nt_hash32_t pid = nt_hash32_str("act_call_pack");
    nt_hash64_t rid = nt_hash64_str("act_call_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    /* Pack needs to be in READY state for activation to run */
    /* Manually set pack_state to READY (since we used parse_pack directly) */
    nt_resource_step(); /* should activate the registered asset */

    /* parse_pack sets blob pointer, pack_state is NONE but activation checks pack_state == READY.
     * Since we used parse_pack directly (not load_file), pack_state is NONE. Need to set it. */
    /* Actually we need pack_state == READY for activation. Let me use test_set_asset_state approach
     * or set pack_state manually. Since we don't have a public API for that, let's use load_file. */

    /* Reset and use load_file approach */
    nt_resource_shutdown();
    nt_http_shutdown();
    nt_fs_shutdown();
    nt_http_init();
    nt_fs_init();
    nt_resource_init(&s_desc);

    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);

    nt_hash32_t pid2 = nt_hash32_str("act_call_pack2");
    nt_hash64_t rid2 = nt_hash64_str("act_call_res2");

    write_test_pack_file("build/test_act_call.ntpack", rid2.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid2, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid2, "build/test_act_call.ntpack"));
    nt_resource_step(); /* polls I/O, parses, activates */

    TEST_ASSERT_EQUAL_UINT32(1, s_activate_call_count);

    (void)remove("build/test_act_call.ntpack");
    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_activation_sets_runtime_handle(void) {
    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);

    nt_hash32_t pid = nt_hash32_str("act_handle_pack");
    nt_hash64_t rid = nt_hash64_str("act_handle_res");

    write_test_pack_file("build/test_act_handle.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_act_handle.ntpack"));
    nt_resource_step();

    /* Request handle after activation */
    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step(); /* resolve */
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, nt_resource_get(h));

    (void)remove("build/test_act_handle.ntpack");
}

/* Build a pack with N assets all of the same type */
static void write_test_pack_multi(const char *path, uint8_t atype, uint32_t count) {
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + (count * sizeof(NtAssetEntry)));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);
    uint32_t data_per_asset = 16;
    uint32_t data_size = 0;
    for (uint32_t i = 0; i < count; i++) {
        data_size += (data_per_asset + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
    }
    uint32_t total_size = header_size + data_size;
    uint8_t *blob = (uint8_t *)calloc(1, total_size);
    if (!blob) {
        return;
    }
    NtPackHeader *h = (NtPackHeader *)blob;
    h->magic = NT_PACK_MAGIC;
    h->version = NT_PACK_VERSION;
    h->asset_count = (uint16_t)count;
    h->header_size = header_size;
    h->total_size = total_size;
    NtAssetEntry *entries = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    uint32_t data_offset = header_size;
    for (uint32_t i = 0; i < count; i++) {
        char name[32];
        (void)snprintf(name, sizeof(name), "multi_%u", i);
        entries[i].resource_id = nt_hash64_str(name).value;
        entries[i].format_version = 1;
        entries[i].asset_type = atype;
        entries[i]._pad = 0;
        entries[i].meta_offset = 0;
        entries[i].offset = data_offset;
        entries[i].size = data_per_asset;
        uint32_t aligned = (data_per_asset + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
        data_offset += aligned;
    }
    h->checksum = nt_crc32(blob + header_size, data_size);

    FILE *f = fopen(path, "wb");
    if (f) {
        (void)fwrite(blob, 1, total_size, f);
        (void)fclose(f);
    }
    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_activation_unlimited_activates_all(void) {
    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
    nt_resource_set_activate_time_budget(0); /* unlimited */

    nt_hash32_t pid = nt_hash32_str("budget_pack");

    /* Build pack with 3 mesh assets */
    write_test_pack_multi("build/test_budget.ntpack", NT_ASSET_MESH, 3);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_budget.ntpack"));
    nt_resource_step();

    /* Unlimited budget: all 3 should activate in one step */
    TEST_ASSERT_EQUAL_UINT32(3, s_activate_call_count);

    (void)remove("build/test_budget.ntpack");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_activation_guarantees_minimum_one(void) {
    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
    /* Very tight time budget — fake_activate is instant so all will fit,
     * but this verifies the guarantee-minimum-1 path compiles and runs. */
    nt_resource_set_activate_time_budget(0.001F);

    nt_hash32_t pid = nt_hash32_str("budget_skip_pack");
    nt_hash64_t rid = nt_hash64_str("budget_skip_res");

    write_test_pack_file("build/test_budget_skip.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_budget_skip.ntpack"));
    nt_resource_step();

    /* At least 1 must activate (guarantee minimum 1 per step) */
    TEST_ASSERT_TRUE(s_activate_call_count >= 1);

    (void)remove("build/test_budget_skip.ntpack");
}

void test_no_activator_stays_registered(void) {
    /* Don't register any activator -- assets stay REGISTERED */
    nt_hash32_t pid = nt_hash32_str("no_act_pack");
    nt_hash64_t rid = nt_hash64_str("no_act_res");

    write_test_pack_file("build/test_no_act.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_no_act.ntpack"));
    nt_resource_step();

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();
    TEST_ASSERT_FALSE(nt_resource_is_ready(h));

    (void)remove("build/test_no_act.ntpack");
}

/* ---- Retry policy tests ---- */

void test_retry_policy_defaults(void) {
    /* Defaults are set in init: max_attempts=0 (infinite), base=500, max=10000.
     * We verify by setting a policy and checking it works. No direct getter. */
    /* Just test the set function doesn't crash */
    nt_resource_set_retry_policy(3, 200, 5000);
}

void test_set_retry_policy(void) {
    nt_resource_set_retry_policy(5, 100, 2000);
    /* Verify by triggering a load failure and checking retry behavior */
    /* This is implicitly tested by other failure tests */
}

/* ---- Blob policy tests ---- */

void test_blob_policy_keep(void) {
    nt_hash32_t pid = nt_hash32_str("blob_keep_pack");
    nt_hash64_t rid = nt_hash64_str("blob_keep_res");

    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);

    write_test_pack_file("build/test_blob_keep.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    nt_resource_set_blob_policy(pid, NT_BLOB_KEEP, 0);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_blob_keep.ntpack"));
    nt_resource_step();

    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(pid));

    /* Call step many times -- blob should persist (NT_BLOB_KEEP) */
    for (int i = 0; i < 10; i++) {
        nt_resource_step();
    }
    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(pid));

    (void)remove("build/test_blob_keep.ntpack");
}

/* ---- Invalidate tests ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_invalidate_marks_registered(void) {
    s_activate_call_count = 0;
    s_deactivate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);

    nt_hash32_t pid = nt_hash32_str("inv_reg_pack");
    nt_hash64_t rid = nt_hash64_str("inv_reg_res");

    write_test_pack_file("build/test_inv_reg.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_inv_reg.ntpack"));
    nt_resource_step(); /* load + activate */

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, nt_resource_get(h));

    /* Invalidate mesh type */
    nt_resource_invalidate(NT_ASSET_MESH);
    nt_resource_step();

    /* After invalidation, assets are REGISTERED, will be re-activated from blob */
    /* Step again to re-activate */
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, nt_resource_get(h));
    TEST_ASSERT_TRUE(s_activate_call_count >= 2); /* activated at least twice */

    (void)remove("build/test_inv_reg.ntpack");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_invalidate_calls_deactivator(void) {
    s_activate_call_count = 0;
    s_deactivate_call_count = 0;
    s_last_deactivated_handle = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);

    nt_hash32_t pid = nt_hash32_str("inv_deact_pack");
    nt_hash64_t rid = nt_hash64_str("inv_deact_res");

    write_test_pack_file("build/test_inv_deact.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_inv_deact.ntpack"));
    nt_resource_step();

    TEST_ASSERT_EQUAL_UINT32(0, s_deactivate_call_count);

    nt_resource_invalidate(NT_ASSET_MESH);
    TEST_ASSERT_EQUAL_UINT32(1, s_deactivate_call_count);
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, s_last_deactivated_handle);

    (void)remove("build/test_inv_deact.ntpack");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_invalidate_skips_virtual(void) {
    s_activate_call_count = 0;
    s_deactivate_call_count = 0;

    /* Virtual pack asset should NOT be invalidated */
    nt_hash32_t pid = nt_hash32_str("inv_virt_pack");
    nt_hash64_t rid = nt_hash64_str("inv_virt_res");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, 42));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(42, nt_resource_get(h));

    /* Invalidate texture type -- virtual assets should be unaffected */
    nt_resource_invalidate(NT_ASSET_TEXTURE);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(42, nt_resource_get(h));
    TEST_ASSERT_EQUAL_UINT32(0, s_deactivate_call_count);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_invalidate_triggers_redownload_on_evicted_blob(void) {
    s_activate_call_count = 0;
    s_deactivate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);

    nt_hash32_t pid = nt_hash32_str("inv_redl_pack");
    nt_hash64_t rid = nt_hash64_str("inv_redl_res");

    write_test_pack_file("build/test_inv_redl.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    nt_resource_set_blob_policy(pid, NT_BLOB_AUTO, 1); /* TTL = 1ms */
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_inv_redl.ntpack"));

    nt_resource_step(); /* load, parse, activate */
    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(pid));
    TEST_ASSERT_EQUAL_UINT32(1, s_activate_call_count);

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, nt_resource_get(h));

    /* Wait for blob TTL to expire (1ms) then step to trigger eviction */
    nt_time_sleep(0.005); /* 5ms, well past 1ms TTL */
    nt_resource_step();   /* should evict blob (TTL expired) */

    /* Now invalidate -- since blob is evicted, should trigger re-download */
    nt_resource_invalidate(NT_ASSET_MESH);
    TEST_ASSERT_EQUAL_UINT32(1, s_deactivate_call_count);

    /* pack_state should be NONE (re-download pending) */
    TEST_ASSERT_EQUAL(NT_PACK_STATE_NONE, nt_resource_pack_state(pid));

    /* Step again -- should re-download from load_path, parse, activate */
    nt_resource_step();
    nt_pack_state_t st = nt_resource_pack_state(pid);
    TEST_ASSERT_TRUE(st == NT_PACK_STATE_READY || st == NT_PACK_STATE_REQUESTED);

    if (st == NT_PACK_STATE_REQUESTED) {
        nt_resource_step();
    }
    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(pid));
    TEST_ASSERT_TRUE(s_activate_call_count >= 2);

    (void)remove("build/test_inv_redl.ntpack");
}

/* ---- Unmount with deactivation test ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_unmount_deactivates_assets(void) {
    s_activate_call_count = 0;
    s_deactivate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);

    nt_hash32_t pid = nt_hash32_str("unmount_deact_pack");
    nt_hash64_t rid = nt_hash64_str("unmount_deact_res");

    write_test_pack_file("build/test_unmount_deact.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_unmount_deact.ntpack"));
    nt_resource_step();

    TEST_ASSERT_EQUAL_UINT32(1, s_activate_call_count);

    /* Unmount should call deactivator */
    nt_resource_unmount(pid);
    TEST_ASSERT_EQUAL_UINT32(1, s_deactivate_call_count);

    (void)remove("build/test_unmount_deact.ntpack");
}

/* ---- Activation failure test ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_activation_failure_sets_failed(void) {
    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate_fail, NULL);

    nt_hash32_t pid = nt_hash32_str("act_fail_pack");
    nt_hash64_t rid = nt_hash64_str("act_fail_res");

    write_test_pack_file("build/test_act_fail.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_act_fail.ntpack"));
    nt_resource_step();

    /* Activator returned 0 (failure) -- asset should be FAILED */
    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_STATE_FAILED, nt_resource_get_state(h));

    (void)remove("build/test_act_fail.ntpack");
}

/* ---- Load URL on native (should fail gracefully) ---- */

void test_load_url_native_fails(void) {
    nt_hash32_t pid = nt_hash32_str("url_fail_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    nt_resource_set_retry_policy(1, 100, 1000);

    /* On native, nt_http_request immediately fails. load_url may return error
     * (if request ID is 0) or OK (if request ID is non-zero but state is FAILED). */
    nt_result_t r = nt_resource_load_url(pid, "http://example.com/test.ntpack");
    if (r == NT_OK) {
        nt_resource_step();
    }
    TEST_ASSERT_EQUAL(NT_PACK_STATE_FAILED, nt_resource_pack_state(pid));
}

/* ---- Blob asset pack builder ---- */

/*
 * Build a NTPACK blob with one NT_ASSET_BLOB entry containing `payload_size`
 * bytes of test data (filled with `fill_byte`). Returns malloc'd blob (caller
 * frees). Sets *out_size to total blob size.
 */
static uint8_t *build_blob_pack(uint64_t rid, uint32_t payload_size, uint8_t fill_byte, uint32_t *out_size) {
    uint32_t asset_data_size = (uint32_t)sizeof(NtBlobAssetHeader) + payload_size;
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);
    uint32_t aligned_data = (asset_data_size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
    uint32_t total_size = header_size + aligned_data;

    uint8_t *blob = (uint8_t *)calloc(1, total_size);
    if (!blob) {
        return NULL;
    }

    NtPackHeader *h = (NtPackHeader *)blob;
    h->magic = NT_PACK_MAGIC;
    h->version = NT_PACK_VERSION;
    h->asset_count = 1;
    h->header_size = header_size;
    h->total_size = total_size;

    NtAssetEntry *entry = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    entry->resource_id = rid;
    entry->format_version = 1;
    entry->asset_type = NT_ASSET_BLOB;
    entry->_pad = 0;
    entry->meta_offset = 0;
    entry->offset = header_size;
    entry->size = asset_data_size;

    /* Write blob asset header */
    NtBlobAssetHeader *bhdr = (NtBlobAssetHeader *)(blob + header_size);
    bhdr->magic = NT_BLOB_MAGIC;
    bhdr->version = NT_BLOB_VERSION;
    bhdr->_pad = 0;

    /* Fill payload data after header */
    memset(blob + header_size + sizeof(NtBlobAssetHeader), fill_byte, payload_size);

    h->checksum = nt_crc32(blob + header_size, aligned_data);

    *out_size = total_size;
    return blob;
}

/* ---- Blob asset tests ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_blob_asset_ready_after_parse(void) {
    nt_hash32_t pid = nt_hash32_str("blob_ready_pack");
    nt_hash64_t rid = nt_hash64_str("blob_ready_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_blob_pack(rid.value, 32, 0xAB, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_BLOB);
    TEST_ASSERT_TRUE(h.id != 0);

    /* Blob assets should be READY immediately after parse + step (no activator needed) */
    nt_resource_step();
    TEST_ASSERT_TRUE(nt_resource_is_ready(h));

    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_get_blob_returns_data(void) {
    nt_hash32_t pid = nt_hash32_str("blob_data_pack");
    nt_hash64_t rid = nt_hash64_str("blob_data_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t payload_size = 16;
    uint32_t blob_size = 0;
    uint8_t *blob = build_blob_pack(rid.value, payload_size, 0xCD, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_BLOB);
    nt_resource_step();

    uint32_t data_size = 0;
    const uint8_t *data = nt_resource_get_blob(h, &data_size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_UINT32(payload_size, data_size);

    /* Verify payload content matches fill byte */
    for (uint32_t i = 0; i < payload_size; i++) {
        TEST_ASSERT_EQUAL_UINT8(0xCD, data[i]);
    }

    free(blob);
}

void test_get_blob_null_for_non_blob(void) {
    nt_hash32_t pid = nt_hash32_str("blob_non_pack");
    nt_hash64_t rid = nt_hash64_str("blob_non_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_pack_with_rid(rid.value, NT_ASSET_MESH, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();

    uint32_t data_size = 99;
    const uint8_t *data = nt_resource_get_blob(h, &data_size);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_UINT32(0, data_size);

    free(blob);
}

void test_get_blob_null_for_invalid_handle(void) {
    uint32_t data_size = 99;
    const uint8_t *data = nt_resource_get_blob(NT_RESOURCE_INVALID, &data_size);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_UINT32(0, data_size);
}

/* ---- Metadata test helpers ---- */

/* Test payload for metadata tests (24 bytes, same size as old NtAabbData) */
typedef struct {
    float values[6];
} TestMetaPayload;

/*
 * Build a NTPACK blob with one fake mesh asset and a metadata entry.
 * The mesh asset data is just zeros (we don't actually parse mesh data in the
 * metadata tests, only the meta section). Returns malloc'd blob (caller frees).
 */
static uint8_t *build_meta_pack(uint64_t rid, uint64_t kind, const void *payload, uint32_t payload_size, uint32_t *out_size) {
    uint32_t asset_data_size = 64; /* fake mesh data */
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);
    uint32_t aligned_data = (asset_data_size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);

    /* Meta section: one NtMetaEntryHeader + payload */
    uint32_t meta_entry_size = (uint32_t)sizeof(NtMetaEntryHeader) + payload_size;
    uint32_t aligned_meta = (meta_entry_size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);

    uint32_t total_size = header_size + aligned_data + aligned_meta;
    uint8_t *blob = (uint8_t *)calloc(1, total_size);
    if (!blob) {
        return NULL;
    }

    uint32_t meta_section_start = header_size + aligned_data;

    NtPackHeader *h = (NtPackHeader *)blob;
    h->magic = NT_PACK_MAGIC;
    h->version = NT_PACK_VERSION;
    h->asset_count = 1;
    h->meta_count = 1;
    h->meta_offset = meta_section_start;
    h->header_size = header_size;
    h->total_size = total_size;

    NtAssetEntry *entry = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    entry->resource_id = rid;
    entry->format_version = 1;
    entry->asset_type = NT_ASSET_MESH;
    entry->_pad = 0;
    entry->offset = header_size;
    entry->size = asset_data_size;
    entry->meta_offset = meta_section_start;

    /* Write meta entry header + payload */
    NtMetaEntryHeader *mh = (NtMetaEntryHeader *)(blob + meta_section_start);
    mh->resource_id = rid;
    mh->kind = kind;
    mh->size = payload_size;
    memcpy(blob + meta_section_start + sizeof(NtMetaEntryHeader), payload, payload_size);

    /* CRC32 over data region (everything after header) */
    h->checksum = nt_crc32(blob + header_size, total_size - header_size);

    *out_size = total_size;
    return blob;
}

/* ---- Metadata query tests ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_resource_get_meta_aabb(void) {
    nt_hash32_t pid = nt_hash32_str("meta_aabb_pack");
    nt_hash64_t rid = nt_hash64_str("meshes/test_cube");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint64_t test_kind = nt_hash64_str("test_meta").value;
    TestMetaPayload test_payload = {{-1.0F, -2.0F, -3.0F, 1.0F, 2.0F, 3.0F}};
    uint32_t blob_size = 0;
    uint8_t *blob = build_meta_pack(rid.value, test_kind, &test_payload, (uint32_t)sizeof(test_payload), &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    TEST_ASSERT_TRUE(h.id != 0);

    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
    nt_resource_step();

    /* Query metadata */
    uint32_t meta_size = 0;
    const void *meta_ptr = nt_resource_get_meta(h, test_kind, &meta_size);
    TEST_ASSERT_NOT_NULL(meta_ptr);
    TEST_ASSERT_EQUAL_UINT32(sizeof(TestMetaPayload), meta_size);

    /* Verify data matches what was written */
    TEST_ASSERT_EQUAL_MEMORY(&test_payload, meta_ptr, sizeof(TestMetaPayload));

    free(blob);
}

void test_resource_get_meta_absent(void) {
    nt_hash32_t pid = nt_hash32_str("meta_absent_pack");
    nt_hash64_t rid = nt_hash64_str("meta_absent_res");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    /* Build a blob pack with no metadata (meta_count=0) */
    uint32_t blob_size = 0;
    uint8_t *blob = build_blob_pack(rid.value, 32, 0xAB, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_BLOB);
    nt_resource_step();

    uint32_t meta_size = 99;
    const void *meta_ptr = nt_resource_get_meta(h, nt_hash64_str("nonexistent").value, &meta_size);
    TEST_ASSERT_NULL(meta_ptr);
    TEST_ASSERT_EQUAL_UINT32(0, meta_size);

    free(blob);
}

void test_resource_get_meta_invalid_handle(void) {
    nt_resource_t invalid = NT_RESOURCE_INVALID;
    uint32_t sz = 99;
    const void *p = nt_resource_get_meta(invalid, 0, &sz);
    TEST_ASSERT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(0, sz);
}

void test_resource_get_meta_wrong_kind(void) {
    nt_hash32_t pid = nt_hash32_str("meta_wrong_kind");
    nt_hash64_t rid = nt_hash64_str("meshes/wrong_kind");

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint64_t stored_kind = nt_hash64_str("stored_kind").value;
    TestMetaPayload payload = {{0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F}};
    uint32_t blob_size = 0;
    uint8_t *blob = build_meta_pack(rid.value, stored_kind, &payload, (uint32_t)sizeof(payload), &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
    nt_resource_step();

    /* Query with a different kind -- should return NULL */
    uint32_t meta_size = 99;
    const void *meta_ptr = nt_resource_get_meta(h, nt_hash64_str("nonexistent").value, &meta_size);
    TEST_ASSERT_NULL(meta_ptr);
    TEST_ASSERT_EQUAL_UINT32(0, meta_size);

    free(blob);
}

/* ---- Resolve callbacks and user_data tests ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_on_resolve_fires_on_winner_change(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);

    nt_hash32_t pid = nt_hash32_str("resolve_pack");
    nt_hash64_t rid = nt_hash64_str("resolve_test");

    write_test_pack_file("build/test_resolve.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_resolve.ntpack"));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);

    nt_resource_step(); /* I/O + parse + activate + resolve */

    /* on_resolve should have fired once */
    TEST_ASSERT_EQUAL_UINT32(1, s_resolve_call_count);
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, s_last_resolve_handle);
    TEST_ASSERT_TRUE(s_last_resolve_size > 0);

    /* user_data allocated with value 42 */
    TEST_ASSERT_NOT_NULL(s_last_resolve_user_data);
    TEST_ASSERT_EQUAL_UINT32(42, *(uint32_t *)s_last_resolve_user_data);

    /* nt_resource_get confirms runtime handle */
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, nt_resource_get(h));

    (void)remove("build/test_resolve.ntpack");
}

void test_on_post_resolve_can_request_dependency_and_resolve_same_step(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    s_next_handle = 0xBEEF;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate_seq, fake_deactivate);
    nt_resource_set_activator(NT_ASSET_TEXTURE, fake_activate_seq, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);
    nt_resource_set_post_resolve_callback(NT_ASSET_MESH, mock_on_post_resolve);

    nt_hash32_t pid = nt_hash32_str("post_resolve_dep_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_pack(2, &blob_size);
    TEST_ASSERT_NOT_NULL(blob);

    /* build_test_pack(2) emits:
     *   asset0 -> rid "asset0", type NT_ASSET_MESH
     *   asset1 -> rid "asset1", type NT_ASSET_TEXTURE */
    nt_hash64_t mesh_rid = nt_hash64_str("asset0");
    nt_hash64_t tex_rid = nt_hash64_str("asset1");
    s_post_resolve_dependency_rid = tex_rid.value;

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, blob, blob_size));

    nt_resource_t mesh = nt_resource_request(mesh_rid, NT_ASSET_MESH);
    TEST_ASSERT_TRUE(mesh.id != 0);

    nt_resource_step();

    TEST_ASSERT_EQUAL_UINT32(1, s_post_resolve_call_count);
    TEST_ASSERT_TRUE(s_last_post_resolve_requested.id != 0);
    TEST_ASSERT_TRUE(nt_resource_is_ready(s_last_post_resolve_requested));
    TEST_ASSERT_TRUE(nt_resource_get(s_last_post_resolve_requested) != 0);

    free(blob);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_on_resolve_merge_on_reactivation(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    s_next_handle = 0xBEEF;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate_seq, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);

    nt_hash64_t rid = nt_hash64_str("merge_test");

    /* First pack at priority 0 */
    nt_hash32_t pid1 = nt_hash32_str("merge_pack1");
    write_test_pack_file("build/test_merge1.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid1, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid1, "build/test_merge1.ntpack"));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step(); /* activate + resolve -> on_resolve fires (first time, user_data=42) */

    TEST_ASSERT_EQUAL_UINT32(1, s_resolve_call_count);
    TEST_ASSERT_EQUAL_UINT32(42, *(uint32_t *)s_last_resolve_user_data);
    void *first_user_data = s_last_resolve_user_data;

    /* Second pack with same resource_id at higher priority */
    nt_hash32_t pid2 = nt_hash32_str("merge_pack2");
    write_test_pack_file("build/test_merge2.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid2, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid2, "build/test_merge2.ntpack"));
    nt_resource_step(); /* new winner from higher-prio pack -> on_resolve fires again (merge) */

    TEST_ASSERT_EQUAL_UINT32(2, s_resolve_call_count);
    /* Merge incremented value: 42 + 1 = 43 */
    TEST_ASSERT_EQUAL_UINT32(43, *(uint32_t *)s_last_resolve_user_data);
    /* Same pointer reused (merge, not re-alloc) */
    TEST_ASSERT_EQUAL_PTR(first_user_data, s_last_resolve_user_data);

    (void)h;
    (void)remove("build/test_merge1.ntpack");
    (void)remove("build/test_merge2.ntpack");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_aux_publish_falls_back_to_best_usable_winner(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    s_next_handle = 0xBEEF;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate_seq, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);
    nt_resource_set_behavior_flags(NT_ASSET_MESH, NT_RESOURCE_BEHAVIOR_AUX_BACKED);

    nt_hash64_t rid = nt_hash64_str("aux_publish_fallback_res");

    nt_hash32_t pid_b = nt_hash32_str("aux_publish_pack_b");
    write_test_pack_file("build/test_aux_publish_b.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid_b, "build/test_aux_publish_b.ntpack"));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();
    uint32_t handle_b = nt_resource_get(h);
    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_TRUE(handle_b != 0);

    nt_hash32_t pid_a = nt_hash32_str("aux_publish_pack_a");
    write_test_pack_file("build/test_aux_publish_a.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 10));
    nt_resource_set_blob_policy(pid_a, NT_BLOB_AUTO, 1);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid_a, "build/test_aux_publish_a.ntpack"));
    nt_resource_step();
    uint32_t handle_a = nt_resource_get(h);
    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_NOT_EQUAL(handle_b, handle_a);

    nt_hash32_t pid_c = nt_hash32_str("aux_publish_pack_c");
    write_test_pack_file("build/test_aux_publish_c.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_c, 20));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid_c, "build/test_aux_publish_c.ntpack"));
    nt_resource_step();
    uint32_t handle_c = nt_resource_get(h);
    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_NOT_EQUAL(handle_a, handle_c);

    nt_time_sleep(0.005);
    nt_resource_step(); /* evict pack A while pack C is still published */

    nt_resource_unmount(pid_c);
    nt_resource_step(); /* target A missing -> published should fall back to resident B */

    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_EQUAL_UINT32(handle_b, nt_resource_get(h));
    TEST_ASSERT_EQUAL(NT_PACK_STATE_NONE, nt_resource_pack_state(pid_a));
    TEST_ASSERT_EQUAL_UINT32(4, s_resolve_call_count);
    TEST_ASSERT_EQUAL_UINT32(45, *(uint32_t *)s_last_resolve_user_data);

    nt_resource_step(); /* re-download A and republish it */
    if (nt_resource_pack_state(pid_a) == NT_PACK_STATE_REQUESTED) {
        nt_resource_step();
    }

    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(pid_a));
    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_EQUAL_UINT32(handle_a, nt_resource_get(h));
    TEST_ASSERT_EQUAL_UINT32(5, s_resolve_call_count);
    TEST_ASSERT_EQUAL_UINT32(46, *(uint32_t *)s_last_resolve_user_data);

    (void)remove("build/test_aux_publish_b.ntpack");
    (void)remove("build/test_aux_publish_a.ntpack");
    (void)remove("build/test_aux_publish_c.ntpack");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_aux_publish_waits_for_reload_when_no_usable_fallback_exists(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    s_next_handle = 0xBEEF;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate_seq, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);
    nt_resource_set_behavior_flags(NT_ASSET_MESH, NT_RESOURCE_BEHAVIOR_AUX_BACKED);

    nt_hash64_t rid = nt_hash64_str("aux_publish_reload_res");

    nt_hash32_t pid_b = nt_hash32_str("aux_reload_pack_b");
    write_test_pack_file("build/test_aux_reload_b.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 0));
    nt_resource_set_blob_policy(pid_b, NT_BLOB_AUTO, 1);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid_b, "build/test_aux_reload_b.ntpack"));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();
    uint32_t handle_b = nt_resource_get(h);
    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_TRUE(handle_b != 0);

    nt_hash32_t pid_a = nt_hash32_str("aux_reload_pack_a");
    write_test_pack_file("build/test_aux_reload_a.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid_a, "build/test_aux_reload_a.ntpack"));
    nt_resource_step();
    uint32_t handle_a = nt_resource_get(h);
    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_NOT_EQUAL(handle_b, handle_a);

    nt_time_sleep(0.005);
    nt_resource_step(); /* evict fallback B while A is published */

    nt_resource_unmount(pid_a);
    nt_resource_step(); /* target B missing and no other usable winner */

    TEST_ASSERT_FALSE(nt_resource_is_ready(h));
    TEST_ASSERT_EQUAL_UINT32(0, nt_resource_get(h));
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_STATE_LOADING, nt_resource_get_state(h));
    TEST_ASSERT_EQUAL(NT_PACK_STATE_NONE, nt_resource_pack_state(pid_b));
    TEST_ASSERT_EQUAL_UINT32(1, s_cleanup_call_count);
    TEST_ASSERT_NULL(nt_resource_get_user_data(h));

    nt_resource_step(); /* re-download B and publish it again */
    if (nt_resource_pack_state(pid_b) == NT_PACK_STATE_REQUESTED) {
        nt_resource_step();
    }

    TEST_ASSERT_EQUAL(NT_PACK_STATE_READY, nt_resource_pack_state(pid_b));
    TEST_ASSERT_TRUE(nt_resource_is_ready(h));
    TEST_ASSERT_EQUAL_UINT32(handle_b, nt_resource_get(h));
    TEST_ASSERT_EQUAL_UINT32(3, s_resolve_call_count);
    TEST_ASSERT_NOT_NULL(nt_resource_get_user_data(h));

    (void)remove("build/test_aux_reload_b.ntpack");
    (void)remove("build/test_aux_reload_a.ntpack");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_on_cleanup_fires_on_unmount(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    s_deactivate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);

    nt_hash32_t pid = nt_hash32_str("cleanup_unmount_pack");
    nt_hash64_t rid = nt_hash64_str("cleanup_unmount_res");

    write_test_pack_file("build/test_cleanup_unmount.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_cleanup_unmount.ntpack"));

    (void)nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step(); /* activate + resolve -> on_resolve sets user_data */

    TEST_ASSERT_EQUAL_UINT32(1, s_resolve_call_count);
    TEST_ASSERT_EQUAL_UINT32(0, s_cleanup_call_count);

    /* Unmount pack -> deactivates asset, then step triggers resolve */
    nt_resource_unmount(pid);
    nt_resource_step(); /* resolve: winner goes to 0 -> on_cleanup fires */

    TEST_ASSERT_EQUAL_UINT32(1, s_cleanup_call_count);

    (void)remove("build/test_cleanup_unmount.ntpack");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_on_cleanup_fires_on_shutdown(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);

    nt_hash32_t pid = nt_hash32_str("cleanup_shutdown_pack");
    nt_hash64_t rid = nt_hash64_str("cleanup_shutdown_res");

    write_test_pack_file("build/test_cleanup_shutdown.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_cleanup_shutdown.ntpack"));

    (void)nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step(); /* activate + resolve -> on_resolve sets user_data */

    TEST_ASSERT_EQUAL_UINT32(1, s_resolve_call_count);
    TEST_ASSERT_EQUAL_UINT32(0, s_cleanup_call_count);

    /* Shutdown should clean up remaining user_data */
    nt_resource_shutdown();

    TEST_ASSERT_EQUAL_UINT32(1, s_cleanup_call_count);

    /* Re-init so tearDown's shutdown doesn't crash */
    nt_resource_init(&s_desc);

    (void)remove("build/test_cleanup_shutdown.ntpack");
}

void test_get_user_data_valid_handle(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);

    nt_hash32_t pid = nt_hash32_str("ud_valid_pack");
    nt_hash64_t rid = nt_hash64_str("ud_valid_res");

    write_test_pack_file("build/test_ud_valid.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_ud_valid.ntpack"));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step(); /* activate + resolve -> on_resolve sets user_data */

    void *ud = nt_resource_get_user_data(h);
    TEST_ASSERT_NOT_NULL(ud);
    TEST_ASSERT_EQUAL_PTR(s_last_resolve_user_data, ud);

    (void)remove("build/test_ud_valid.ntpack");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_get_user_data_invalid_handle(void) {
    /* Invalid zero handle */
    TEST_ASSERT_NULL(nt_resource_get_user_data((nt_resource_t){0}));

    /* Request a resource to get a valid handle, then shutdown+reinit (stale generation) */
    reset_resolve_state();
    s_activate_call_count = 0;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);

    nt_hash32_t pid = nt_hash32_str("ud_invalid_pack");
    nt_hash64_t rid = nt_hash64_str("ud_invalid_res");

    write_test_pack_file("build/test_ud_invalid.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_ud_invalid.ntpack"));

    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step();

    /* Handle is valid now */
    TEST_ASSERT_NOT_NULL(nt_resource_get_user_data(h));

    /* Shutdown + reinit: old handle becomes stale */
    nt_resource_shutdown();
    nt_resource_init(&s_desc);

    TEST_ASSERT_NULL(nt_resource_get_user_data(h));

    (void)remove("build/test_ud_invalid.ntpack");
}

void test_no_on_resolve_without_registration(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    /* Register only activator, NOT resolve callbacks */
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate, fake_deactivate);

    nt_hash32_t pid = nt_hash32_str("no_resolve_pack");
    nt_hash64_t rid = nt_hash64_str("no_resolve_res");

    write_test_pack_file("build/test_no_resolve.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_no_resolve.ntpack"));

    (void)nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step(); /* Should not crash even without resolve callbacks */

    /* on_resolve was never called */
    TEST_ASSERT_EQUAL_UINT32(0, s_resolve_call_count);

    (void)remove("build/test_no_resolve.ntpack");
}

void test_user_data_null_initially(void) {
    /* Request a resource without any pack mounted or activation */
    nt_hash64_t rid = nt_hash64_str("ud_null_initial");
    nt_resource_t h = nt_resource_request(rid, NT_ASSET_MESH);

    TEST_ASSERT_NULL(nt_resource_get_user_data(h));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_on_resolve_fires_on_priority_change(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    s_next_handle = 0xBEEF;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate_seq, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);

    nt_hash64_t rid = nt_hash64_str("prio_change_res");

    /* Pack A at priority 10 */
    nt_hash32_t pidA = nt_hash32_str("prio_packA");
    write_test_pack_file("build/test_prio_a.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pidA, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pidA, "build/test_prio_a.ntpack"));

    /* Pack B at priority 0 (lower) */
    nt_hash32_t pidB = nt_hash32_str("prio_packB");
    write_test_pack_file("build/test_prio_b.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pidB, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pidB, "build/test_prio_b.ntpack"));

    (void)nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step(); /* A wins (prio 10 > 0) → on_resolve fires */

    TEST_ASSERT_EQUAL_UINT32(1, s_resolve_call_count);
    uint32_t first_handle = s_last_resolve_handle;

    /* Lower A's priority below B → B becomes winner */
    nt_resource_set_priority(pidA, -5);
    nt_resource_step(); /* B wins now → on_resolve fires again */

    TEST_ASSERT_EQUAL_UINT32(2, s_resolve_call_count);
    TEST_ASSERT_NOT_EQUAL(first_handle, s_last_resolve_handle);
    /* Merge: 42 + 1 = 43 */
    TEST_ASSERT_EQUAL_UINT32(43, *(uint32_t *)s_last_resolve_user_data);

    (void)remove("build/test_prio_a.ntpack");
    (void)remove("build/test_prio_b.ntpack");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_on_resolve_fires_on_invalidate(void) {
    reset_resolve_state();
    s_activate_call_count = 0;
    s_next_handle = 0xBEEF;
    nt_resource_set_activator(NT_ASSET_MESH, fake_activate_seq, fake_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_MESH, mock_on_resolve, mock_on_cleanup);

    nt_hash32_t pid = nt_hash32_str("inv_resolve_pack");
    nt_hash64_t rid = nt_hash64_str("inv_resolve_res");

    write_test_pack_file("build/test_inv_resolve.ntpack", rid.value, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_load_file(pid, "build/test_inv_resolve.ntpack"));

    (void)nt_resource_request(rid, NT_ASSET_MESH);
    nt_resource_step(); /* activate (handle=0xBEEF) + resolve → on_resolve */

    TEST_ASSERT_EQUAL_UINT32(1, s_resolve_call_count);
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, s_last_resolve_handle);

    /* Invalidate → deactivates, sets REGISTERED. Next step re-activates with new handle. */
    nt_resource_invalidate(NT_ASSET_MESH);
    nt_resource_step(); /* re-activate (handle=0xBEF0) + resolve */
    nt_resource_step(); /* resolve may need extra step */

    /* Same winner identity (resolve_asset_idx) but different handle → on_resolve must fire */
    TEST_ASSERT_TRUE(s_resolve_call_count >= 2);
    TEST_ASSERT_NOT_EQUAL(0xBEEF, s_last_resolve_handle);
    /* Merge: 42 + 1 = 43 */
    TEST_ASSERT_EQUAL_UINT32(43, *(uint32_t *)s_last_resolve_user_data);

    (void)remove("build/test_inv_resolve.ntpack");
}

/* ---- main ---- */

int main(void) {
    UNITY_BEGIN();

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
    RUN_TEST(test_parse_entries_overflow);

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
    RUN_TEST(test_publication_epoch_stable_when_no_slot_publication_changes);
    RUN_TEST(test_publication_epoch_increments_on_winner_change);
    RUN_TEST(test_virtual_overrides_file);
    RUN_TEST(test_file_overrides_virtual);
    RUN_TEST(test_unregister_fallback);
    RUN_TEST(test_unmount_virtual_clears);

    /* Placeholder tests */
    RUN_TEST(test_set_placeholder_fallback);
    RUN_TEST(test_placeholder_not_used_when_ready);
    RUN_TEST(test_placeholder_type_specific);

    /* Asset slot reuse */
    RUN_TEST(test_asset_slot_reuse);

    /* Pack loading tests */
    RUN_TEST(test_load_file_transitions_state);
    RUN_TEST(test_load_file_nonexistent);
    RUN_TEST(test_pack_state_api);
    RUN_TEST(test_pack_progress);

    /* Activator system tests */
    RUN_TEST(test_set_activator);
    RUN_TEST(test_activation_called_on_step);
    RUN_TEST(test_activation_sets_runtime_handle);
    RUN_TEST(test_activation_unlimited_activates_all);
    RUN_TEST(test_activation_guarantees_minimum_one);
    RUN_TEST(test_no_activator_stays_registered);

    /* Retry policy tests */
    RUN_TEST(test_retry_policy_defaults);
    RUN_TEST(test_set_retry_policy);

    /* Blob policy tests */
    RUN_TEST(test_blob_policy_keep);

    /* Invalidate tests */
    RUN_TEST(test_invalidate_marks_registered);
    RUN_TEST(test_invalidate_calls_deactivator);
    RUN_TEST(test_invalidate_skips_virtual);
    RUN_TEST(test_invalidate_triggers_redownload_on_evicted_blob);

    /* Unmount with deactivation */
    RUN_TEST(test_unmount_deactivates_assets);

    /* Activation failure */
    RUN_TEST(test_activation_failure_sets_failed);

    /* URL loading on native */
    RUN_TEST(test_load_url_native_fails);

    /* Blob asset tests */
    RUN_TEST(test_blob_asset_ready_after_parse);
    RUN_TEST(test_get_blob_returns_data);
    RUN_TEST(test_get_blob_null_for_non_blob);
    RUN_TEST(test_get_blob_null_for_invalid_handle);

    /* Metadata query tests */
    RUN_TEST(test_resource_get_meta_aabb);
    RUN_TEST(test_resource_get_meta_absent);
    RUN_TEST(test_resource_get_meta_invalid_handle);
    RUN_TEST(test_resource_get_meta_wrong_kind);

    /* Resolve callbacks and user_data tests */
    RUN_TEST(test_on_resolve_fires_on_winner_change);
    RUN_TEST(test_on_post_resolve_can_request_dependency_and_resolve_same_step);
    RUN_TEST(test_on_resolve_merge_on_reactivation);
    RUN_TEST(test_aux_publish_falls_back_to_best_usable_winner);
    RUN_TEST(test_aux_publish_waits_for_reload_when_no_usable_fallback_exists);
    RUN_TEST(test_on_cleanup_fires_on_unmount);
    RUN_TEST(test_on_cleanup_fires_on_shutdown);
    RUN_TEST(test_get_user_data_valid_handle);
    RUN_TEST(test_get_user_data_invalid_handle);
    RUN_TEST(test_no_on_resolve_without_registration);
    RUN_TEST(test_user_data_null_initially);
    RUN_TEST(test_on_resolve_fires_on_priority_change);
    RUN_TEST(test_on_resolve_fires_on_invalidate);

    return UNITY_END();
}
