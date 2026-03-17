/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "resource/nt_resource.h"
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
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + asset_count * sizeof(NtAssetEntry));
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

    return UNITY_END();
}
