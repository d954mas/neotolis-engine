/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NT_ATLAS_TEST_ACCESS is provided by the CMake target. */

/* clang-format off */
#include "atlas/nt_atlas.h"
#include "hash/nt_hash.h"
#include "nt_atlas_format.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"
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

    mock_atlas_spec_t spec = {
        .regions = regions,
        .region_count = 3,
        .vertices = verts,
        .total_vertex_count = 12,
        .indices = indices,
        .total_index_count = 18,
        .page_ids = NULL,
        .page_count = 0,
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
    TEST_ASSERT_EQUAL_UINT8(0, nt_atlas_test_page_count(ad));
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

    mock_atlas_spec_t spec = {
        .regions = &region,
        .region_count = 1,
        .vertices = verts,
        .total_vertex_count = 4,
        .indices = indices,
        .total_index_count = 6,
        .page_ids = NULL,
        .page_count = 0,
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

/* ---- Merge test helpers (Plan 02) ---- */

/* Compact per-region spec for the merge tests. Lets a test describe a
 * multi-region blob with one line per region and a shared vertex/index
 * payload stream. */
typedef struct {
    uint64_t name_hash;
    uint8_t vertex_count;
    uint8_t index_count;
    uint16_t source_w;
    uint8_t page_index;
    uint8_t transform;
    /* Per-region marker used to fill deterministic verts/indices payloads
     * so tests can assert that blob2's data landed at the new append cursor
     * and not blob1's data. */
    int16_t payload_seed;
} merge_region_spec_t;

/* Build an atlas blob from an array of merge_region_spec_t entries.
 * Generates synthetic vertices and indices tied to payload_seed so each
 * region's payload is visibly distinct. Returns bytes written. */
static uint32_t build_merge_blob(uint8_t *out, uint32_t cap, const merge_region_spec_t *specs, uint16_t region_count, const uint64_t *page_ids, uint16_t page_count) {
    static NtAtlasRegion regions_storage[8];
    static NtAtlasVertex verts_storage[128];
    static uint16_t indices_storage[256];

    TEST_ASSERT_MESSAGE(region_count <= 8, "merge helper region_count cap");
    uint32_t v_cursor = 0;
    uint32_t i_cursor = 0;
    for (uint16_t k = 0; k < region_count; k++) {
        const merge_region_spec_t *s = &specs[k];
        memset(&regions_storage[k], 0, sizeof(NtAtlasRegion));
        regions_storage[k].name_hash = s->name_hash;
        regions_storage[k].source_w = s->source_w;
        regions_storage[k].source_h = 32;
        regions_storage[k].origin_x = 0.5F;
        regions_storage[k].origin_y = 0.5F;
        regions_storage[k].vertex_start = v_cursor;
        regions_storage[k].index_start = i_cursor;
        regions_storage[k].vertex_count = s->vertex_count;
        regions_storage[k].index_count = s->index_count;
        regions_storage[k].page_index = s->page_index;
        regions_storage[k].transform = s->transform;

        for (uint8_t vi = 0; vi < s->vertex_count; vi++) {
            TEST_ASSERT_MESSAGE(v_cursor < 128, "merge helper vertex cap");
            verts_storage[v_cursor].local_x = (int16_t)(s->payload_seed + vi);
            verts_storage[v_cursor].local_y = (int16_t)(s->payload_seed + vi + 100);
            verts_storage[v_cursor].atlas_u = (uint16_t)(s->payload_seed + vi + 1000);
            verts_storage[v_cursor].atlas_v = (uint16_t)(s->payload_seed + vi + 2000);
            v_cursor++;
        }
        for (uint8_t ii = 0; ii < s->index_count; ii++) {
            TEST_ASSERT_MESSAGE(i_cursor < 256, "merge helper index cap");
            indices_storage[i_cursor] = (uint16_t)(s->payload_seed + ii);
            i_cursor++;
        }
    }

    mock_atlas_spec_t spec = {
        .regions = regions_storage,
        .region_count = region_count,
        .vertices = verts_storage,
        .total_vertex_count = v_cursor,
        .indices = indices_storage,
        .total_index_count = i_cursor,
        .page_ids = page_ids,
        .page_count = page_count,
    };
    return build_mock_atlas_blob(out, cap, &spec);
}

/* ---- Test 6: merge common region updates metadata in place ---- */
void test_atlas_merge_common_region_updates_in_place(void) {
    /* Pages omitted — page tests use full resource system (test 15/16). */

    /* blob1: single region hash=0x111, 4 verts / 6 indices, payload seed 10 */
    merge_region_spec_t blob1_specs[1] = {
        {.name_hash = 0x111ULL, .vertex_count = 4, .index_count = 6, .source_w = 100, .page_index = 0, .transform = 0, .payload_seed = 10},
    };
    uint8_t buf1[512];
    uint32_t size1 = build_merge_blob(buf1, sizeof(buf1), blob1_specs, 1, NULL, 0);

    /* blob2: same hash=0x111 but source_w=200, DIFFERENT payload (seed=500).
     * Same vertex/index counts so we can assert exact cursor positions. */
    merge_region_spec_t blob2_specs[1] = {
        {.name_hash = 0x111ULL, .vertex_count = 4, .index_count = 6, .source_w = 200, .page_index = 0, .transform = 0, .payload_seed = 500},
    };
    uint8_t buf2[512];
    uint32_t size2 = build_merge_blob(buf2, sizeof(buf2), blob2_specs, 1, NULL, 0);

    /* First parse */
    nt_atlas_test_drive_resolve(buf1, size1, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;
    TEST_ASSERT_EQUAL_UINT32(1, nt_atlas_test_region_count(ad));
    TEST_ASSERT_EQUAL_UINT32(4, nt_atlas_test_vertex_count(ad));
    TEST_ASSERT_EQUAL_UINT32(6, nt_atlas_test_index_count(ad));

    /* Merge */
    nt_atlas_test_drive_resolve(buf2, size2, &s_user_data);

    /* Stable index: find_region of hash 0x111 still returns 0. */
    TEST_ASSERT_EQUAL_UINT32(0, nt_atlas_test_find_region_raw(ad, 0x111ULL));
    TEST_ASSERT_EQUAL_UINT32(1, nt_atlas_test_region_count(ad));

    /* In-place metadata update */
    const nt_texture_region_t *r = nt_atlas_test_get_region_raw(ad, 0);
    TEST_ASSERT_EQUAL_UINT16(200, r->source_w);
    TEST_ASSERT_EQUAL_UINT8(4, r->vertex_count);
    TEST_ASSERT_EQUAL_UINT8(6, r->index_count);

    /* Cursors reset on merge — no fragmentation, exact fit */
    TEST_ASSERT_EQUAL_UINT32(4, nt_atlas_test_vertex_count(ad));
    TEST_ASSERT_EQUAL_UINT32(6, nt_atlas_test_index_count(ad));
    TEST_ASSERT_EQUAL_UINT32(0, r->vertex_start);
    TEST_ASSERT_EQUAL_UINT32(0, r->index_start);
}

/* ---- Test 7: merge appends new region with fresh index ---- */
void test_atlas_merge_new_region_appends_with_fresh_index(void) {
    /* Pages omitted. */

    merge_region_spec_t blob1_specs[2] = {
        {.name_hash = 0xAAAULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 10},
        {.name_hash = 0xBBBULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 20},
    };
    uint8_t buf1[512];
    uint32_t size1 = build_merge_blob(buf1, sizeof(buf1), blob1_specs, 2, NULL, 0);

    merge_region_spec_t blob2_specs[3] = {
        {.name_hash = 0xAAAULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 10},
        {.name_hash = 0xBBBULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 20},
        {.name_hash = 0xCCCULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 30},
    };
    uint8_t buf2[512];
    uint32_t size2 = build_merge_blob(buf2, sizeof(buf2), blob2_specs, 3, NULL, 0);

    nt_atlas_test_drive_resolve(buf1, size1, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;
    TEST_ASSERT_EQUAL_UINT32(2, nt_atlas_test_region_count(ad));

    nt_atlas_test_drive_resolve(buf2, size2, &s_user_data);

    TEST_ASSERT_EQUAL_UINT32(3, nt_atlas_test_region_count(ad));
    TEST_ASSERT_EQUAL_UINT32(0, nt_atlas_test_find_region_raw(ad, 0xAAAULL));
    TEST_ASSERT_EQUAL_UINT32(1, nt_atlas_test_find_region_raw(ad, 0xBBBULL));
    TEST_ASSERT_EQUAL_UINT32(2, nt_atlas_test_find_region_raw(ad, 0xCCCULL)); /* fresh index == previous region_count */
}

/* ---- Test 8: removed region becomes a tombstone (stable indices) ---- */
void test_atlas_merge_removed_region_becomes_tombstone(void) {
    /* Pages omitted. */

    merge_region_spec_t blob1_specs[3] = {
        {.name_hash = 0xAAAULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 10},
        {.name_hash = 0xBBBULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 20},
        {.name_hash = 0xCCCULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 30},
    };
    uint8_t buf1[512];
    uint32_t size1 = build_merge_blob(buf1, sizeof(buf1), blob1_specs, 3, NULL, 0);

    /* blob2 drops B. */
    merge_region_spec_t blob2_specs[2] = {
        {.name_hash = 0xAAAULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 10},
        {.name_hash = 0xCCCULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 30},
    };
    uint8_t buf2[512];
    uint32_t size2 = build_merge_blob(buf2, sizeof(buf2), blob2_specs, 2, NULL, 0);

    nt_atlas_test_drive_resolve(buf1, size1, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;
    TEST_ASSERT_EQUAL_UINT32(3, nt_atlas_test_region_count(ad));

    nt_atlas_test_drive_resolve(buf2, size2, &s_user_data);

    /* Tombstones don't shrink the count — region_count stays at 3. */
    TEST_ASSERT_EQUAL_UINT32(3, nt_atlas_test_region_count(ad));

    /* Surviving indices are NOT renumbered. */
    TEST_ASSERT_EQUAL_UINT32(0, nt_atlas_test_find_region_raw(ad, 0xAAAULL));
    TEST_ASSERT_EQUAL_UINT32(2, nt_atlas_test_find_region_raw(ad, 0xCCCULL));
    TEST_ASSERT_EQUAL_UINT32(NT_ATLAS_INVALID_REGION, nt_atlas_test_find_region_raw(ad, 0xBBBULL));

    /* Slot 1 is a tombstone: NT_ATLAS_TOMBSTONE_HASH, zero counts. */
    const nt_texture_region_t *r1 = nt_atlas_test_get_region_raw(ad, 1);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_EQUAL_UINT64(NT_ATLAS_TOMBSTONE_HASH, r1->name_hash);
    TEST_ASSERT_EQUAL_UINT8(0, r1->vertex_count);
    TEST_ASSERT_EQUAL_UINT8(0, r1->index_count);
}

/* ---- Test 9: find_region returns INVALID for a tombstoned hash across
 * multiple merges; re-adding a previously-tombstoned hash creates a NEW
 * region at a fresh monotonic index — the old tombstone stays dead. ---- */
void test_atlas_find_region_returns_invalid_for_tombstone(void) {
    /* Pages omitted. */

    merge_region_spec_t only_a[1] = {
        {.name_hash = 0xAAAULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 10},
    };
    merge_region_spec_t a_and_b[2] = {
        {.name_hash = 0xAAAULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 10},
        {.name_hash = 0xBBBULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 20},
    };

    uint8_t buf_a[512];
    uint8_t buf_ab[512];
    uint32_t size_a = build_merge_blob(buf_a, sizeof(buf_a), only_a, 1, NULL, 0);
    uint32_t size_ab = build_merge_blob(buf_ab, sizeof(buf_ab), a_and_b, 2, NULL, 0);

    /* Merge 1: parse {A, B} (first parse) */
    nt_atlas_test_drive_resolve(buf_ab, size_ab, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;
    TEST_ASSERT_EQUAL_UINT32(2, nt_atlas_test_region_count(ad));
    const uint32_t b_first_idx = nt_atlas_test_find_region_raw(ad, 0xBBBULL);
    TEST_ASSERT_EQUAL_UINT32(1, b_first_idx);

    /* Merge 2: {A} — B is tombstoned. */
    nt_atlas_test_drive_resolve(buf_a, size_a, &s_user_data);
    TEST_ASSERT_EQUAL_UINT32(2, nt_atlas_test_region_count(ad));
    TEST_ASSERT_EQUAL_UINT32(NT_ATLAS_INVALID_REGION, nt_atlas_test_find_region_raw(ad, 0xBBBULL));

    /* Merge 3: {A, B} — re-adding B MUST create a fresh index (monotonic). */
    nt_atlas_test_drive_resolve(buf_ab, size_ab, &s_user_data);
    const uint32_t b_second_idx = nt_atlas_test_find_region_raw(ad, 0xBBBULL);
    TEST_ASSERT_NOT_EQUAL_UINT32(NT_ATLAS_INVALID_REGION, b_second_idx);
    /* The old tombstone at index 1 stays dead, new B lives at a higher
     * index. region_count must have grown by exactly 1. */
    TEST_ASSERT_EQUAL_UINT32(3, nt_atlas_test_region_count(ad));
    TEST_ASSERT_TRUE_MESSAGE(b_second_idx > b_first_idx, "re-added region must get a strictly higher index");

    /* Merge 4: {A} — tombstone the re-added B again. */
    nt_atlas_test_drive_resolve(buf_a, size_a, &s_user_data);
    TEST_ASSERT_EQUAL_UINT32(NT_ATLAS_INVALID_REGION, nt_atlas_test_find_region_raw(ad, 0xBBBULL));
    TEST_ASSERT_EQUAL_UINT32(3, nt_atlas_test_region_count(ad));

    /* A stayed at index 0 through the whole chain. */
    TEST_ASSERT_EQUAL_UINT32(0, nt_atlas_test_find_region_raw(ad, 0xAAAULL));
}

/* ---- Test 10: get_region on a tombstoned slot returns a non-NULL
 * pointer with vertex_count==0 && index_count==0 (D-08 zero-draw
 * without NULL-branch in the hot path). ---- */
void test_atlas_get_region_returns_vertex_count_zero_for_tombstone(void) {
    /* Pages omitted. */

    merge_region_spec_t blob1_specs[3] = {
        {.name_hash = 0xAAAULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 10},
        {.name_hash = 0xBBBULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 20},
        {.name_hash = 0xCCCULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 30},
    };
    merge_region_spec_t blob2_specs[2] = {
        {.name_hash = 0xAAAULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 10},
        {.name_hash = 0xCCCULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 30},
    };

    uint8_t buf1[512];
    uint8_t buf2[512];
    uint32_t size1 = build_merge_blob(buf1, sizeof(buf1), blob1_specs, 3, NULL, 0);
    uint32_t size2 = build_merge_blob(buf2, sizeof(buf2), blob2_specs, 2, NULL, 0);

    nt_atlas_test_drive_resolve(buf1, size1, &s_user_data);
    nt_atlas_test_drive_resolve(buf2, size2, &s_user_data);

    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;

    /* get_region(1) on the tombstone returns non-NULL with zero counts. */
    const nt_texture_region_t *r1 = nt_atlas_test_get_region_raw(ad, 1);
    TEST_ASSERT_NOT_NULL(r1); /* no NULL branch in the hot path */
    TEST_ASSERT_EQUAL_UINT8(0, r1->vertex_count);
    TEST_ASSERT_EQUAL_UINT8(0, r1->index_count);
    TEST_ASSERT_EQUAL_UINT64(NT_ATLAS_TOMBSTONE_HASH, r1->name_hash);
}

/* ---- Test 11: Hash collisions resolve correctly via linear probing ----
 * Two name_hashes that differ only in high bits (both map to the same slot
 * modulo any power-of-two capacity) plus one unrelated control hash.
 * REGION-06 hash robustness. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_atlas_hash_collisions_probe_correctly(void) {
    /* Hash A and Hash B differ only in bit 63 — both map to slot (0x10 & mask)
     * for any power-of-two mask ≤ 63 bits. */
    const uint64_t hash_a = 0x0000000000000010ULL;
    const uint64_t hash_b = 0x8000000000000010ULL;
    const uint64_t hash_ctrl = 0x00000000DEADBEEFULL; /* unrelated */

    NtAtlasVertex verts[9];
    uint16_t indices[9];
    for (int i = 0; i < 9; i++) {
        verts[i].local_x = (int16_t)(i * 10);
        verts[i].local_y = (int16_t)(i * 20);
        verts[i].atlas_u = (uint16_t)(i * 1000);
        verts[i].atlas_v = (uint16_t)(i * 2000);
        indices[i] = (uint16_t)(i % 3);
    }

    NtAtlasRegion regions[3];
    memset(regions, 0, sizeof(regions));
    /* Region 0: hash_a, 3 verts / 3 indices */
    regions[0].name_hash = hash_a;
    regions[0].source_w = 10;
    regions[0].source_h = 10;
    regions[0].origin_x = 0.5F;
    regions[0].origin_y = 0.5F;
    regions[0].vertex_start = 0;
    regions[0].index_start = 0;
    regions[0].vertex_count = 3;
    regions[0].index_count = 3;
    regions[0].page_index = 0;

    /* Region 1: hash_b (collides with hash_a), 3 verts / 3 indices */
    regions[1].name_hash = hash_b;
    regions[1].source_w = 20;
    regions[1].source_h = 20;
    regions[1].origin_x = 0.5F;
    regions[1].origin_y = 0.5F;
    regions[1].vertex_start = 3;
    regions[1].index_start = 3;
    regions[1].vertex_count = 3;
    regions[1].index_count = 3;
    regions[1].page_index = 0;

    /* Region 2: hash_ctrl (no collision), 3 verts / 3 indices */
    regions[2].name_hash = hash_ctrl;
    regions[2].source_w = 30;
    regions[2].source_h = 30;
    regions[2].origin_x = 0.5F;
    regions[2].origin_y = 0.5F;
    regions[2].vertex_start = 6;
    regions[2].index_start = 6;
    regions[2].vertex_count = 3;
    regions[2].index_count = 3;
    regions[2].page_index = 0;

    mock_atlas_spec_t spec = {
        .regions = regions,
        .region_count = 3,
        .vertices = verts,
        .total_vertex_count = 9,
        .indices = indices,
        .total_index_count = 9,
        .page_ids = NULL,
        .page_count = 0,
    };

    uint8_t buf[512];
    uint32_t size = build_mock_atlas_blob(buf, sizeof(buf), &spec);

    nt_atlas_test_drive_resolve(buf, size, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;

    /* All three must map to distinct valid indices. */
    uint32_t idx_a = nt_atlas_test_find_region_raw(ad, hash_a);
    uint32_t idx_b = nt_atlas_test_find_region_raw(ad, hash_b);
    uint32_t idx_ctrl = nt_atlas_test_find_region_raw(ad, hash_ctrl);

    TEST_ASSERT_EQUAL_UINT32(0, idx_a);
    TEST_ASSERT_EQUAL_UINT32(1, idx_b);
    TEST_ASSERT_EQUAL_UINT32(2, idx_ctrl);

    /* Cross-check: no collision caused wrong region to be returned. */
    TEST_ASSERT_NOT_EQUAL(idx_a, idx_b);
    TEST_ASSERT_NOT_EQUAL(idx_a, idx_ctrl);
    TEST_ASSERT_NOT_EQUAL(idx_b, idx_ctrl);

    /* Verify each region's source_w matches what we set (proves the right data landed). */
    const nt_texture_region_t *r_a = nt_atlas_test_get_region_raw(ad, idx_a);
    const nt_texture_region_t *r_b = nt_atlas_test_get_region_raw(ad, idx_b);
    const nt_texture_region_t *r_ctrl = nt_atlas_test_get_region_raw(ad, idx_ctrl);
    TEST_ASSERT_EQUAL_UINT16(10, r_a->source_w);
    TEST_ASSERT_EQUAL_UINT16(20, r_b->source_w);
    TEST_ASSERT_EQUAL_UINT16(30, r_ctrl->source_w);
}

/* ---- Test 12: Hash table growth under 1000 regions ----
 * Exercises multiple rehashes and reallocs. Then merges with a shifted
 * working set [500..1500) to verify tombstones, common, and new regions
 * at scale. REGION-09 coverage. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_atlas_hash_table_growth_under_1000_regions(void) {
    /* Build blob1 with 1000 regions, each with name_hash = i + 1,
     * minimal 3 verts / 3 indices per region. */
    const uint32_t n1 = 1000;
    const uint32_t verts_per_region = 3;
    const uint32_t indices_per_region = 3;
    const uint32_t total_verts_1 = n1 * verts_per_region;
    const uint32_t total_indices_1 = n1 * indices_per_region;

    /* Estimate blob size:
     * header(28) + regions(1000*36) + verts(3000*8) + indices(3000*2)
     * = 28 + 36000 + 24000 + 6000 = 66028 bytes */
    const uint32_t blob_cap = 70000;
    uint8_t *buf1 = (uint8_t *)malloc(blob_cap);
    TEST_ASSERT_NOT_NULL_MESSAGE(buf1, "malloc for 1000-region blob");

    NtAtlasRegion *regs1 = (NtAtlasRegion *)malloc(n1 * sizeof(NtAtlasRegion));
    NtAtlasVertex *verts1 = (NtAtlasVertex *)malloc(total_verts_1 * sizeof(NtAtlasVertex));
    uint16_t *inds1 = (uint16_t *)malloc(total_indices_1 * sizeof(uint16_t));
    TEST_ASSERT_NOT_NULL(regs1);
    TEST_ASSERT_NOT_NULL(verts1);
    TEST_ASSERT_NOT_NULL(inds1);

    for (uint32_t i = 0; i < n1; i++) {
        memset(&regs1[i], 0, sizeof(NtAtlasRegion));
        regs1[i].name_hash = (uint64_t)i + 1U;
        regs1[i].source_w = (uint16_t)(i & 0xFFFFU);
        regs1[i].source_h = 16;
        regs1[i].origin_x = 0.5F;
        regs1[i].origin_y = 0.5F;
        regs1[i].vertex_start = i * verts_per_region;
        regs1[i].index_start = i * indices_per_region;
        regs1[i].vertex_count = (uint8_t)verts_per_region;
        regs1[i].index_count = (uint8_t)indices_per_region;
        regs1[i].page_index = 0;

        for (uint32_t vi = 0; vi < verts_per_region; vi++) {
            uint32_t idx = (i * verts_per_region) + vi;
            verts1[idx].local_x = (int16_t)(i & 0x7FFF);
            verts1[idx].local_y = (int16_t)(vi);
            verts1[idx].atlas_u = (uint16_t)(i & 0xFFFF);
            verts1[idx].atlas_v = (uint16_t)(vi);
        }
        for (uint32_t ii = 0; ii < indices_per_region; ii++) {
            inds1[(i * indices_per_region) + ii] = (uint16_t)ii;
        }
    }

    mock_atlas_spec_t spec1 = {
        .regions = regs1,
        .region_count = (uint16_t)n1,
        .vertices = verts1,
        .total_vertex_count = total_verts_1,
        .indices = inds1,
        .total_index_count = total_indices_1,
        .page_ids = NULL,
        .page_count = 0,
    };
    uint32_t size1 = build_mock_atlas_blob(buf1, blob_cap, &spec1);

    /* First parse: 1000 regions */
    nt_atlas_test_drive_resolve(buf1, size1, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;
    TEST_ASSERT_EQUAL_UINT32(n1, nt_atlas_test_region_count(ad));

    /* Verify all 1000 are findable */
    for (uint32_t i = 0; i < n1; i++) {
        uint32_t idx = nt_atlas_test_find_region_raw(ad, (uint64_t)i + 1U);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(i, idx, "first parse: find_region mismatch");
    }

    /* Build blob2 with regions [500..1500) — hashes 501..1500 */
    const uint32_t n2 = 1000;
    const uint32_t total_verts_2 = n2 * verts_per_region;
    const uint32_t total_indices_2 = n2 * indices_per_region;

    uint8_t *buf2 = (uint8_t *)malloc(blob_cap);
    NtAtlasRegion *regs2 = (NtAtlasRegion *)malloc(n2 * sizeof(NtAtlasRegion));
    NtAtlasVertex *verts2 = (NtAtlasVertex *)malloc(total_verts_2 * sizeof(NtAtlasVertex));
    uint16_t *inds2 = (uint16_t *)malloc(total_indices_2 * sizeof(uint16_t));
    TEST_ASSERT_NOT_NULL(buf2);
    TEST_ASSERT_NOT_NULL(regs2);
    TEST_ASSERT_NOT_NULL(verts2);
    TEST_ASSERT_NOT_NULL(inds2);

    for (uint32_t i = 0; i < n2; i++) {
        uint32_t region_id = 500 + i; /* range [500..1500) */
        memset(&regs2[i], 0, sizeof(NtAtlasRegion));
        regs2[i].name_hash = (uint64_t)region_id + 1U; /* hashes 501..1500 */
        regs2[i].source_w = (uint16_t)((region_id + 1000) & 0xFFFFU);
        regs2[i].source_h = 32;
        regs2[i].origin_x = 0.5F;
        regs2[i].origin_y = 0.5F;
        regs2[i].vertex_start = i * verts_per_region;
        regs2[i].index_start = i * indices_per_region;
        regs2[i].vertex_count = (uint8_t)verts_per_region;
        regs2[i].index_count = (uint8_t)indices_per_region;
        regs2[i].page_index = 0;

        for (uint32_t vi = 0; vi < verts_per_region; vi++) {
            uint32_t idx = (i * verts_per_region) + vi;
            verts2[idx].local_x = (int16_t)(region_id & 0x7FFF);
            verts2[idx].local_y = (int16_t)(vi + 100);
            verts2[idx].atlas_u = (uint16_t)((region_id + 1000) & 0xFFFF);
            verts2[idx].atlas_v = (uint16_t)(vi + 100);
        }
        for (uint32_t ii = 0; ii < indices_per_region; ii++) {
            inds2[(i * indices_per_region) + ii] = (uint16_t)ii;
        }
    }

    mock_atlas_spec_t spec2 = {
        .regions = regs2,
        .region_count = (uint16_t)n2,
        .vertices = verts2,
        .total_vertex_count = total_verts_2,
        .indices = inds2,
        .total_index_count = total_indices_2,
        .page_ids = NULL,
        .page_count = 0,
    };
    uint32_t size2 = build_mock_atlas_blob(buf2, blob_cap, &spec2);

    /* Merge */
    nt_atlas_test_drive_resolve(buf2, size2, &s_user_data);

    /* region_count should be 1500: original 1000 + 500 new (1001..1500) */
    TEST_ASSERT_EQUAL_UINT32(1500, nt_atlas_test_region_count(ad));

    /* Regions [0..499] (hashes 1..500) should be tombstoned. */
    for (uint32_t i = 0; i < 500; i++) {
        uint32_t idx = nt_atlas_test_find_region_raw(ad, (uint64_t)i + 1U);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_INVALID_REGION, idx, "tombstoned region still findable");
        const nt_texture_region_t *r = nt_atlas_test_get_region_raw(ad, i);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, r->vertex_count, "tombstoned region vertex_count != 0");
    }

    /* Regions [500..999] (hashes 501..1000) are common — indices unchanged. */
    for (uint32_t i = 500; i < 1000; i++) {
        uint32_t idx = nt_atlas_test_find_region_raw(ad, (uint64_t)i + 1U);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(i, idx, "common region index shifted");
    }

    /* Regions [1000..1499] (hashes 1001..1500) are new — appended. */
    for (uint32_t i = 1000; i < 1500; i++) {
        uint32_t idx = nt_atlas_test_find_region_raw(ad, (uint64_t)i + 1U);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(i, idx, "new region index mismatch");
    }

    /* Cleanup heap allocations */
    free(buf1);
    free(regs1);
    free(verts1);
    free(inds1);
    free(buf2);
    free(regs2);
    free(verts2);
    free(inds2);
}

/* ---- Test 13: on_cleanup releases all buffers (ASan validates) ----
 * Parse, merge, then call cleanup directly. ASan reports zero leaks
 * at test-binary exit if every malloc has a matching free. */
void test_atlas_on_cleanup_releases_all_buffers(void) {
    merge_region_spec_t blob1_specs[3] = {
        {.name_hash = 0xA01ULL, .vertex_count = 4, .index_count = 6, .source_w = 16, .page_index = 0, .transform = 0, .payload_seed = 10},
        {.name_hash = 0xA02ULL, .vertex_count = 3, .index_count = 3, .source_w = 32, .page_index = 0, .transform = 1, .payload_seed = 20},
        {.name_hash = 0xA03ULL, .vertex_count = 4, .index_count = 6, .source_w = 48, .page_index = 0, .transform = 0, .payload_seed = 30},
    };
    uint8_t buf1[512];
    uint32_t size1 = build_merge_blob(buf1, sizeof(buf1), blob1_specs, 3, NULL, 0);

    /* First parse */
    nt_atlas_test_drive_resolve(buf1, size1, &s_user_data);
    TEST_ASSERT_NOT_NULL(s_user_data);

    /* Merge with different set to exercise realloc paths */
    merge_region_spec_t blob2_specs[4] = {
        {.name_hash = 0xA01ULL, .vertex_count = 4, .index_count = 6, .source_w = 64, .page_index = 0, .transform = 0, .payload_seed = 100},
        {.name_hash = 0xA03ULL, .vertex_count = 4, .index_count = 6, .source_w = 64, .page_index = 0, .transform = 0, .payload_seed = 110},
        {.name_hash = 0xA04ULL, .vertex_count = 4, .index_count = 6, .source_w = 64, .page_index = 0, .transform = 0, .payload_seed = 120},
        {.name_hash = 0xA05ULL, .vertex_count = 4, .index_count = 6, .source_w = 64, .page_index = 0, .transform = 0, .payload_seed = 130},
    };
    uint8_t buf2[512];
    uint32_t size2 = build_merge_blob(buf2, sizeof(buf2), blob2_specs, 4, NULL, 0);

    nt_atlas_test_drive_resolve(buf2, size2, &s_user_data);

    /* Explicit cleanup — tearDown would also clean up, but calling it
     * directly makes the test's intent clear. */
    nt_atlas_test_drive_cleanup(s_user_data);
    s_user_data = NULL;
    /* If ASan is active and any buffer leaked, the test binary will
     * report a leak-check failure at exit. */
}

/* ---- Test 14: Header validation rejects corruption ----
 * Uses the test-only nt_atlas_test_validate_header helper that mirrors
 * the NT_ASSERT logic in validate_and_carve_blob but returns false on
 * failure instead of trapping. Automated coverage for magic, version,
 * and size-bounds checks. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_atlas_on_resolve_header_validation_rejects_corruption(void) {
    /* Build a valid blob first */
    uint8_t buf[512];
    uint32_t valid_size = 0;
    build_fixture_blob(buf, sizeof(buf), &valid_size);

    /* Valid blob should pass */
    TEST_ASSERT_TRUE(nt_atlas_test_validate_header(buf, valid_size));

    /* Magic corruption: overwrite first 4 bytes */
    uint8_t corrupt_magic[512];
    memcpy(corrupt_magic, buf, valid_size);
    uint32_t bad_magic = 0xDEADBEEFU;
    memcpy(corrupt_magic, &bad_magic, sizeof(bad_magic));
    TEST_ASSERT_FALSE_MESSAGE(nt_atlas_test_validate_header(corrupt_magic, valid_size), "corrupted magic should fail");

    /* Version corruption: overwrite bytes 4-5 */
    uint8_t corrupt_version[512];
    memcpy(corrupt_version, buf, valid_size);
    uint16_t bad_version = 0xFFFFU;
    memcpy(corrupt_version + 4, &bad_version, sizeof(bad_version));
    TEST_ASSERT_FALSE_MESSAGE(nt_atlas_test_validate_header(corrupt_version, valid_size), "bad version should fail");

    /* Truncated blob: size < sizeof(NtAtlasHeader) */
    TEST_ASSERT_FALSE_MESSAGE(nt_atlas_test_validate_header(buf, 10), "truncated blob should fail");

    /* Truncated blob: header valid but size too small for pages + regions */
    TEST_ASSERT_FALSE_MESSAGE(nt_atlas_test_validate_header(buf, sizeof(NtAtlasHeader)), "undersized blob should fail");
}

/* ---- Test 15: Page ids stored at parse, handles resolved via nt_resource_find ----
 * Uses the resource system so nt_resource_find resolves page handles.
 * Verifies page_resource_ids[] are stored correctly at parse and replaced
 * on merge, and that handles are non-zero (resolved). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_atlas_page_resources_stored_at_parse(void) {
    /* Set up resource system so nt_resource_find resolves page handles. */
    nt_resource_init(NULL);
    nt_atlas_init();

    /* Register all page texture IDs used in this test. */
    nt_resource_request((nt_hash64_t){FIXTURE_PAGE0_ID}, NT_ASSET_TEXTURE);
    nt_resource_request((nt_hash64_t){FIXTURE_PAGE1_ID}, NT_ASSET_TEXTURE);
    nt_resource_request((nt_hash64_t){0xCCCULL}, NT_ASSET_TEXTURE);
    nt_resource_request((nt_hash64_t){0xDDDULL}, NT_ASSET_TEXTURE);

    /* Build blob with 1 region, 2 pages. */
    static NtAtlasVertex verts[3] = {{10, 20, 1000, 2000}, {30, 40, 3000, 4000}, {50, 60, 5000, 6000}};
    static uint16_t indices[3] = {0, 1, 2};
    static NtAtlasRegion region;
    memset(&region, 0, sizeof(region));
    region.name_hash = FIXTURE_R0_HASH;
    region.source_w = 64;
    region.source_h = 64;
    region.origin_x = 0.5F;
    region.origin_y = 0.5F;
    region.vertex_count = 3;
    region.index_count = 3;

    static const uint64_t page_ids[2] = {FIXTURE_PAGE0_ID, FIXTURE_PAGE1_ID};
    mock_atlas_spec_t spec = {
        .regions = &region,
        .region_count = 1,
        .vertices = verts,
        .total_vertex_count = 3,
        .indices = indices,
        .total_index_count = 3,
        .page_ids = page_ids,
        .page_count = 2,
    };
    uint8_t buf[512];
    uint32_t size = build_mock_atlas_blob(buf, sizeof(buf), &spec);

    nt_atlas_test_drive_resolve(buf, size, &s_user_data);
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;

    /* Page ids stored */
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_PAGE0_ID, nt_atlas_test_page_resource_id(ad, 0));
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_PAGE1_ID, nt_atlas_test_page_resource_id(ad, 1));

    /* Handles resolved (non-zero — resource system is running) */
    TEST_ASSERT_TRUE(nt_atlas_test_page_resource_handle(ad, 0) != 0);
    TEST_ASSERT_TRUE(nt_atlas_test_page_resource_handle(ad, 1) != 0);

    /* Merge with different page ids (0xCCC, 0xDDD) */
    static NtAtlasVertex merge_verts[3];
    static uint16_t merge_indices[3] = {0, 1, 2};
    for (int i = 0; i < 3; i++) {
        merge_verts[i].local_x = (int16_t)(i * 5);
        merge_verts[i].local_y = (int16_t)(i * 10);
        merge_verts[i].atlas_u = (uint16_t)(i * 500);
        merge_verts[i].atlas_v = (uint16_t)(i * 1000);
    }
    static NtAtlasRegion merge_region;
    memset(&merge_region, 0, sizeof(merge_region));
    merge_region.name_hash = FIXTURE_R0_HASH;
    merge_region.source_w = 64;
    merge_region.source_h = 64;
    merge_region.origin_x = 0.5F;
    merge_region.origin_y = 0.5F;
    merge_region.vertex_count = 3;
    merge_region.index_count = 3;

    static const uint64_t merge_page_ids[2] = {0xCCCULL, 0xDDDULL};
    mock_atlas_spec_t merge_spec = {
        .regions = &merge_region,
        .region_count = 1,
        .vertices = merge_verts,
        .total_vertex_count = 3,
        .indices = merge_indices,
        .total_index_count = 3,
        .page_ids = merge_page_ids,
        .page_count = 2,
    };
    uint8_t merge_buf[512];
    uint32_t merge_size = build_mock_atlas_blob(merge_buf, sizeof(merge_buf), &merge_spec);

    nt_atlas_test_drive_resolve(merge_buf, merge_size, &s_user_data);

    /* After merge: page ids replaced, handles re-resolved */
    TEST_ASSERT_EQUAL_UINT64(0xCCCULL, nt_atlas_test_page_resource_id(ad, 0));
    TEST_ASSERT_EQUAL_UINT64(0xDDDULL, nt_atlas_test_page_resource_id(ad, 1));
    TEST_ASSERT_TRUE(nt_atlas_test_page_resource_handle(ad, 0) != 0);
    TEST_ASSERT_TRUE(nt_atlas_test_page_resource_handle(ad, 1) != 0);

    /* Cleanup: free atlas data, then shut down resource system. */
    nt_atlas_test_drive_cleanup(s_user_data);
    s_user_data = NULL;
    nt_resource_shutdown();
    nt_atlas_test_reset();
}

/* ---- Test 16: Full resource pipeline integration ----
 * End-to-end: nt_resource_init + nt_atlas_init → mount + parse_pack →
 * nt_resource_request + nt_resource_step → on_resolve fires →
 * nt_atlas_find_region + nt_atlas_get_region via the public API →
 * nt_resource_shutdown (cleanup runs). Validates the full wiring. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_atlas_full_resource_pipeline_integration(void) {
    /* Build an atlas blob (3 regions, 2 pages) */
    uint8_t atlas_blob[512];
    uint32_t atlas_blob_size = 0;
    build_fixture_blob(atlas_blob, sizeof(atlas_blob), &atlas_blob_size);

    /* Build a .ntpack containing the atlas blob as a single NT_ASSET_ATLAS asset.
     * Layout: NtPackHeader(32) + NtAssetEntry(24) + alignment padding + atlas_blob */
    const uint64_t atlas_rid = 0x1234567890ABCDEFULL;
    const uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry));
    const uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(uint32_t)(NT_PACK_DATA_ALIGN - 1U);
    const uint32_t aligned_atlas = (atlas_blob_size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(uint32_t)(NT_PACK_ASSET_ALIGN - 1U);
    const uint32_t total_size = header_size + aligned_atlas;

    uint8_t *pack_blob = (uint8_t *)calloc(1, total_size);
    TEST_ASSERT_NOT_NULL(pack_blob);

    NtPackHeader *ph = (NtPackHeader *)pack_blob;
    ph->magic = NT_PACK_MAGIC;
    ph->version = NT_PACK_VERSION;
    ph->asset_count = 1;
    ph->header_size = header_size;
    ph->total_size = total_size;
    ph->meta_offset = 0;
    ph->meta_count = 0;

    NtAssetEntry *entry = (NtAssetEntry *)(pack_blob + sizeof(NtPackHeader));
    entry->resource_id = atlas_rid;
    entry->asset_type = NT_ASSET_ATLAS;
    entry->format_version = NT_ATLAS_VERSION;
    entry->offset = header_size;
    entry->size = atlas_blob_size;
    entry->meta_offset = 0;
    entry->_pad = 0;

    memcpy(pack_blob + header_size, atlas_blob, atlas_blob_size);
    ph->checksum = nt_crc32(pack_blob + header_size, total_size - header_size);

    /* --- Resource system init --- */
    nt_resource_init(NULL);
    nt_atlas_init();

    /* Mount pack, parse, request */
    nt_hash32_t pid = nt_hash32_str("atlas_integ_pack");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, pack_blob, total_size));

    nt_hash64_t rid = {atlas_rid};
    nt_resource_t atlas_res = nt_resource_request(rid, NT_ASSET_ATLAS);
    TEST_ASSERT_TRUE(atlas_res.id != 0);

    /* Step: Phase B activates (atlas_activate returns 1 → READY),
     * Phase D resolves slot → on_resolve fires → nt_atlas_data_t created. */
    nt_resource_step();

    /* Assert resource is ready */
    TEST_ASSERT_TRUE_MESSAGE(nt_resource_is_ready(atlas_res), "atlas resource not ready after step");

    /* Query via public API */
    uint32_t idx = nt_atlas_find_region(atlas_res, FIXTURE_R0_HASH);
    TEST_ASSERT_EQUAL_UINT32(0, idx);

    const nt_texture_region_t *r = nt_atlas_get_region(atlas_res, 0);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_R0_HASH, r->name_hash);
    TEST_ASSERT_EQUAL_UINT16(64, r->source_w);
    TEST_ASSERT_EQUAL_UINT8(4, r->vertex_count);
    TEST_ASSERT_EQUAL_UINT8(6, r->index_count);

    /* Verify all 3 regions are queryable */
    TEST_ASSERT_EQUAL_UINT32(1, nt_atlas_find_region(atlas_res, FIXTURE_R1_HASH));
    TEST_ASSERT_EQUAL_UINT32(2, nt_atlas_find_region(atlas_res, FIXTURE_R2_HASH));
    TEST_ASSERT_EQUAL_UINT32(NT_ATLAS_INVALID_REGION, nt_atlas_find_region(atlas_res, 0xDEADULL));

    /* Cleanup: shutdown frees user_data via on_cleanup, then we free the pack blob */
    nt_resource_shutdown();
    nt_atlas_test_reset();
    free(pack_blob);

    /* s_user_data is NOT involved in this test — set to NULL to prevent
     * tearDown from double-freeing. */
    s_user_data = NULL;
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_atlas_parse_valid_blob);
    RUN_TEST(test_atlas_find_region_by_hash);
    RUN_TEST(test_atlas_get_region_by_index_bounds_check);
    RUN_TEST(test_atlas_get_region_returns_field_passthrough);
    RUN_TEST(test_atlas_on_resolve_null_data_early_returns);
    RUN_TEST(test_atlas_merge_common_region_updates_in_place);
    RUN_TEST(test_atlas_merge_new_region_appends_with_fresh_index);
    RUN_TEST(test_atlas_merge_removed_region_becomes_tombstone);
    RUN_TEST(test_atlas_find_region_returns_invalid_for_tombstone);
    RUN_TEST(test_atlas_get_region_returns_vertex_count_zero_for_tombstone);
    RUN_TEST(test_atlas_hash_collisions_probe_correctly);
    RUN_TEST(test_atlas_hash_table_growth_under_1000_regions);
    RUN_TEST(test_atlas_on_cleanup_releases_all_buffers);
    RUN_TEST(test_atlas_on_resolve_header_validation_rejects_corruption);
    RUN_TEST(test_atlas_page_resources_stored_at_parse);
    RUN_TEST(test_atlas_full_resource_pipeline_integration);
    return UNITY_END();
}
