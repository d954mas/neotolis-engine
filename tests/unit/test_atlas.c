/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NT_ATLAS_TEST_ACCESS is provided by the CMake target. */

/* clang-format off */
#include "atlas/nt_atlas.h"
#include "nt_atlas_format.h"
#include "nt_pack_format.h"
#include "unity.h"
/* clang-format on */

/* ---- Mock blob builder ----
 *
 * Builds a byte-for-byte valid NT_ASSET_ATLAS v3 blob in a caller-owned buffer.
 * Layout follows shared/include/nt_atlas_format.h:
 *
 *   offset 0              : NtAtlasHeader (28 bytes)
 *   offset 28             : uint64_t texture_resource_ids[page_count]
 *   offset 28+page_count*8: NtAtlasRegion regions[region_count]
 *   offset hdr->vertex_offset: NtAtlasVertex vertices[total_vertex_count]
 *   offset hdr->index_offset : uint16_t indices[total_index_count]
 *
 * Tests supply raw region / vertex / index / page_id arrays and this helper
 * wires them together and computes offsets. The returned size is the number
 * of bytes actually written to `out`. */

typedef struct {
    const NtAtlasRegion *regions;
    uint16_t region_count;
    const NtAtlasVertex *vertices;
    uint32_t total_vertex_count;
    const uint16_t *indices;
    uint32_t total_index_count;
    const uint64_t *page_ids;
    uint16_t page_count;
} mock_atlas_spec_t;

static uint32_t build_mock_atlas_blob(uint8_t *out, uint32_t cap, const mock_atlas_spec_t *spec) {
    const uint32_t page_bytes = (uint32_t)spec->page_count * (uint32_t)sizeof(uint64_t);
    const uint32_t region_bytes = (uint32_t)spec->region_count * (uint32_t)sizeof(NtAtlasRegion);
    const uint32_t vertex_bytes = spec->total_vertex_count * (uint32_t)sizeof(NtAtlasVertex);
    const uint32_t index_bytes = spec->total_index_count * (uint32_t)sizeof(uint16_t);

    const uint32_t vertex_offset = (uint32_t)sizeof(NtAtlasHeader) + page_bytes + region_bytes;
    const uint32_t index_offset = vertex_offset + vertex_bytes;
    const uint32_t total = index_offset + index_bytes;

    TEST_ASSERT_MESSAGE(total <= cap, "mock blob buffer too small");

    memset(out, 0, total);

    NtAtlasHeader *hdr = (NtAtlasHeader *)out;
    hdr->magic = NT_ATLAS_MAGIC;
    hdr->version = NT_ATLAS_VERSION;
    hdr->region_count = spec->region_count;
    hdr->page_count = spec->page_count;
    hdr->_pad = 0;
    hdr->vertex_offset = vertex_offset;
    hdr->total_vertex_count = spec->total_vertex_count;
    hdr->index_offset = index_offset;
    hdr->total_index_count = spec->total_index_count;

    if (page_bytes > 0) {
        memcpy(out + sizeof(NtAtlasHeader), spec->page_ids, page_bytes);
    }
    if (region_bytes > 0) {
        memcpy(out + sizeof(NtAtlasHeader) + page_bytes, spec->regions, region_bytes);
    }
    if (vertex_bytes > 0) {
        memcpy(out + vertex_offset, spec->vertices, vertex_bytes);
    }
    if (index_bytes > 0) {
        memcpy(out + index_offset, spec->indices, index_bytes);
    }

    return total;
}

/* ---- Shared test state ---- */

/* The direct-drive tests each allocate their own user_data via
 * nt_atlas_test_drive_resolve and free it via nt_atlas_test_drive_cleanup.
 * This wrapper lets tearDown safely clean up on failure mid-test. */
static void *s_user_data;

void setUp(void) { s_user_data = NULL; }

void tearDown(void) {
    if (s_user_data != NULL) {
        nt_atlas_test_drive_cleanup(s_user_data);
        s_user_data = NULL;
    }
}

/* ---- Shared 3-region / 2-page fixture used by tests 1, 2, 3 ---- */

#define FIXTURE_R0_HASH 0x100ULL
#define FIXTURE_R1_HASH 0x200ULL
#define FIXTURE_R2_HASH 0x300ULL
#define FIXTURE_PAGE0_ID 0xAAAULL
#define FIXTURE_PAGE1_ID 0xBBBULL

static void build_fixture_blob(uint8_t *buf, uint32_t cap, uint32_t *out_size) {
    /* 3 regions: 4 + 3 + 5 = 12 vertices, 6 + 3 + 9 = 18 indices.
     * Region 0: verts [0..4),  indices [0..6),  page 0, hash 0x100
     * Region 1: verts [4..7),  indices [6..9),  page 1, hash 0x200
     * Region 2: verts [7..12), indices [9..18), page 0, hash 0x300 */
    static NtAtlasVertex verts[12];
    static uint16_t indices[18];
    for (uint16_t i = 0; i < 12; i++) {
        verts[i].local_x = (int16_t)(i * 10);
        verts[i].local_y = (int16_t)(i * 20);
        verts[i].atlas_u = (uint16_t)(i * 1000);
        verts[i].atlas_v = (uint16_t)(i * 2000);
    }
    for (uint16_t i = 0; i < 18; i++) {
        indices[i] = (uint16_t)(i % 5); /* local-per-region, deliberately not 0..N */
    }

    static NtAtlasRegion regions[3];
    memset(regions, 0, sizeof(regions));
    regions[0].name_hash = FIXTURE_R0_HASH;
    regions[0].source_w = 64;
    regions[0].source_h = 64;
    regions[0].origin_x = 0.5F;
    regions[0].origin_y = 0.5F;
    regions[0].vertex_start = 0;
    regions[0].index_start = 0;
    regions[0].vertex_count = 4;
    regions[0].index_count = 6;
    regions[0].page_index = 0;
    regions[0].transform = 0;

    regions[1].name_hash = FIXTURE_R1_HASH;
    regions[1].source_w = 32;
    regions[1].source_h = 48;
    regions[1].origin_x = 0.25F;
    regions[1].origin_y = 0.75F;
    regions[1].vertex_start = 4;
    regions[1].index_start = 6;
    regions[1].vertex_count = 3;
    regions[1].index_count = 3;
    regions[1].page_index = 1;
    regions[1].transform = 1;

    regions[2].name_hash = FIXTURE_R2_HASH;
    regions[2].source_w = 128;
    regions[2].source_h = 96;
    regions[2].origin_x = 0.0F;
    regions[2].origin_y = 1.0F;
    regions[2].vertex_start = 7;
    regions[2].index_start = 9;
    regions[2].vertex_count = 5;
    regions[2].index_count = 9;
    regions[2].page_index = 0;
    regions[2].transform = 4;

    static const uint64_t page_ids[2] = {FIXTURE_PAGE0_ID, FIXTURE_PAGE1_ID};

    mock_atlas_spec_t spec = {
        .regions = regions,
        .region_count = 3,
        .vertices = verts,
        .total_vertex_count = 12,
        .indices = indices,
        .total_index_count = 18,
        .page_ids = page_ids,
        .page_count = 2,
    };

    *out_size = build_mock_atlas_blob(buf, cap, &spec);
}

/* ---- Tests ---- */

/* Test 1: Valid blob → on_resolve populates a fresh nt_atlas_data_t
 * Verifies REGION-04 (activator parses blob into runtime structure). */
void test_atlas_parse_valid_blob(void) {
    uint8_t buf[512];
    uint32_t size = 0;
    build_fixture_blob(buf, sizeof(buf), &size);

    nt_atlas_test_drive_resolve(buf, size, &s_user_data);
    TEST_ASSERT_NOT_NULL_MESSAGE(s_user_data, "on_resolve should allocate user_data on first parse");

    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;
    TEST_ASSERT_EQUAL_UINT32(3, nt_atlas_test_region_count(ad));
    TEST_ASSERT_EQUAL_UINT32(12, nt_atlas_test_vertex_count(ad));
    TEST_ASSERT_EQUAL_UINT32(18, nt_atlas_test_index_count(ad));
    TEST_ASSERT_EQUAL_UINT8(2, nt_atlas_test_page_count(ad));
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_PAGE0_ID, nt_atlas_test_page_resource_id(ad, 0));
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_PAGE1_ID, nt_atlas_test_page_resource_id(ad, 1));
}

/* Test 2: find_region by name hash
 * Verifies REGION-06 (open-addressing lookup returns correct index). */
void test_atlas_find_region_by_hash(void) {
    uint8_t buf[512];
    uint32_t size = 0;
    build_fixture_blob(buf, sizeof(buf), &size);

    nt_atlas_test_drive_resolve(buf, size, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;

    TEST_ASSERT_EQUAL_UINT32(0, nt_atlas_test_find_region_raw(ad, FIXTURE_R0_HASH));
    TEST_ASSERT_EQUAL_UINT32(1, nt_atlas_test_find_region_raw(ad, FIXTURE_R1_HASH));
    TEST_ASSERT_EQUAL_UINT32(2, nt_atlas_test_find_region_raw(ad, FIXTURE_R2_HASH));
    TEST_ASSERT_EQUAL_UINT32(NT_ATLAS_INVALID_REGION, nt_atlas_test_find_region_raw(ad, 0xDEADBEEFULL));
}

/* Test 3: get_region by index returns valid pointer with matching name_hash
 * Verifies REGION-07 (O(1) array lookup). The out-of-bounds death case is
 * documented here but not asserted — the ctest death-test infrastructure is
 * not wired and 48-VALIDATION.md marks that row as manual. */
void test_atlas_get_region_by_index_bounds_check(void) {
    uint8_t buf[512];
    uint32_t size = 0;
    build_fixture_blob(buf, sizeof(buf), &size);

    nt_atlas_test_drive_resolve(buf, size, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;

    const nt_texture_region_t *r0 = nt_atlas_test_get_region_raw(ad, 0);
    TEST_ASSERT_NOT_NULL(r0);
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_R0_HASH, r0->name_hash);

    const nt_texture_region_t *r2 = nt_atlas_test_get_region_raw(ad, 2);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_R2_HASH, r2->name_hash);
    /* nt_atlas_get_region(res, 3) would trip NT_ASSERT — skipped per plan. */
}

/* Test 4: Every NtAtlasRegion field round-trips into nt_texture_region_t
 * Verifies REGION-08 field-passthrough guard (transform D4, all raw ints). */
void test_atlas_get_region_returns_field_passthrough(void) {
    static NtAtlasVertex verts[4] = {
        {.local_x = 10, .local_y = 20, .atlas_u = 1000, .atlas_v = 2000},
        {.local_x = 30, .local_y = 40, .atlas_u = 3000, .atlas_v = 4000},
        {.local_x = 50, .local_y = 60, .atlas_u = 5000, .atlas_v = 6000},
        {.local_x = 70, .local_y = 80, .atlas_u = 7000, .atlas_v = 8000},
    };
    static uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

    static NtAtlasRegion region;
    memset(&region, 0, sizeof(region));
    region.name_hash = 0xABCD1234ULL;
    region.source_w = 123;
    region.source_h = 456;
    region.trim_offset_x = -7;
    region.trim_offset_y = 11;
    region.origin_x = 0.25F;
    region.origin_y = 0.75F;
    region.vertex_start = 0;
    region.index_start = 0;
    region.vertex_count = 4;
    region.index_count = 6;
    region.page_index = 1;
    region.transform = 0x05; /* bit0=flipH, bit2=diagonal */

    static const uint64_t page_ids[2] = {0xDEAD, 0xBEEF};

    mock_atlas_spec_t spec = {
        .regions = &region,
        .region_count = 1,
        .vertices = verts,
        .total_vertex_count = 4,
        .indices = indices,
        .total_index_count = 6,
        .page_ids = page_ids,
        .page_count = 2,
    };

    uint8_t buf[256];
    uint32_t size = build_mock_atlas_blob(buf, sizeof(buf), &spec);

    nt_atlas_test_drive_resolve(buf, size, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;
    const nt_texture_region_t *r = nt_atlas_test_get_region_raw(ad, 0);

    TEST_ASSERT_EQUAL_UINT64(0xABCD1234ULL, r->name_hash);
    TEST_ASSERT_EQUAL_UINT16(123, r->source_w);
    TEST_ASSERT_EQUAL_UINT16(456, r->source_h);
    TEST_ASSERT_EQUAL_INT16(-7, r->trim_offset_x);
    TEST_ASSERT_EQUAL_INT16(11, r->trim_offset_y);
    /* Unity is built with UNITY_EXCLUDE_FLOAT (engine-wide policy) so we
     * compare origin_x/origin_y byte-for-byte against the expected IEEE-754
     * encoding. Field-passthrough test: we want byte-level equality anyway,
     * not tolerance comparison. */
    uint32_t origin_x_bits = 0;
    uint32_t origin_y_bits = 0;
    memcpy(&origin_x_bits, &r->origin_x, sizeof(origin_x_bits));
    memcpy(&origin_y_bits, &r->origin_y, sizeof(origin_y_bits));
    const float expected_x = 0.25F;
    const float expected_y = 0.75F;
    uint32_t expected_x_bits = 0;
    uint32_t expected_y_bits = 0;
    memcpy(&expected_x_bits, &expected_x, sizeof(expected_x_bits));
    memcpy(&expected_y_bits, &expected_y, sizeof(expected_y_bits));
    TEST_ASSERT_EQUAL_HEX32(expected_x_bits, origin_x_bits);
    TEST_ASSERT_EQUAL_HEX32(expected_y_bits, origin_y_bits);

    TEST_ASSERT_EQUAL_UINT8(4, r->vertex_count);
    TEST_ASSERT_EQUAL_UINT8(6, r->index_count);
    TEST_ASSERT_EQUAL_UINT8(1, r->page_index);
    TEST_ASSERT_EQUAL_UINT8(0x05, r->transform);
    TEST_ASSERT_EQUAL_UINT32(0, r->vertex_start);
    TEST_ASSERT_EQUAL_UINT32(0, r->index_start);
}

/* Test 5: on_resolve with data==NULL is a no-op (blob eviction edge case).
 * After a normal first parse the user_data must be untouched. */
void test_atlas_on_resolve_null_data_early_returns(void) {
    uint8_t buf[512];
    uint32_t size = 0;
    build_fixture_blob(buf, sizeof(buf), &size);

    nt_atlas_test_drive_resolve(buf, size, &s_user_data);
    TEST_ASSERT_NOT_NULL(s_user_data);

    void *before = s_user_data;
    const struct nt_atlas_data *ad_before = (const struct nt_atlas_data *)before;
    const uint32_t region_count_before = nt_atlas_test_region_count(ad_before);
    const uint32_t vertex_count_before = nt_atlas_test_vertex_count(ad_before);

    /* Drive with NULL data — should early-return without touching state. */
    nt_atlas_test_drive_resolve(NULL, 0, &s_user_data);
    TEST_ASSERT_EQUAL_PTR(before, s_user_data);

    const struct nt_atlas_data *ad_after = (const struct nt_atlas_data *)s_user_data;
    TEST_ASSERT_EQUAL_UINT32(region_count_before, nt_atlas_test_region_count(ad_after));
    TEST_ASSERT_EQUAL_UINT32(vertex_count_before, nt_atlas_test_vertex_count(ad_after));

    /* Also drive with size==0 on valid pointer — same early return. */
    nt_atlas_test_drive_resolve(buf, 0, &s_user_data);
    TEST_ASSERT_EQUAL_PTR(before, s_user_data);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_atlas_parse_valid_blob);
    RUN_TEST(test_atlas_find_region_by_hash);
    RUN_TEST(test_atlas_get_region_by_index_bounds_check);
    RUN_TEST(test_atlas_get_region_returns_field_passthrough);
    RUN_TEST(test_atlas_on_resolve_null_data_early_returns);
    return UNITY_END();
}
