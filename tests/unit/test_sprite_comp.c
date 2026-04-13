/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "atlas/nt_atlas.h"
#include "entity/nt_entity.h"
#include "nt_atlas_format.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "unity.h"
/* clang-format on */

/* ---- Mock atlas blob builder (same pattern as test_atlas.c) ---- */

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

/* ---- Atlas fixture constants ---- */

#define FIXTURE_R0_HASH 0x100ULL
#define FIXTURE_R1_HASH 0x200ULL

/* ---- Shared test state ---- */

static bool s_atlas_initialized;
static nt_resource_t s_atlas_res;
static uint8_t *s_pack_blobs[4]; /* heap-allocated pack blobs for cleanup */
static uint8_t s_pack_blob_count;

/* ---- Atlas fixture: full resource pipeline ---- */

static uint32_t build_fixture_atlas_blob(uint8_t *atlas_blob, uint32_t cap, float r0_origin_x, float r0_origin_y, float r1_origin_x, float r1_origin_y) {
    // #region Build 2-region atlas blob (no pages for simplicity)
    NtAtlasVertex verts[8];
    uint16_t indices[12];
    for (uint16_t i = 0; i < 8; i++) {
        verts[i].local_x = (int16_t)(i * 10);
        verts[i].local_y = (int16_t)(i * 20);
        verts[i].atlas_u = (uint16_t)(i * 1000);
        verts[i].atlas_v = (uint16_t)(i * 2000);
    }
    for (uint16_t i = 0; i < 12; i++) {
        indices[i] = (uint16_t)(i % 4);
    }

    NtAtlasRegion regions[2];
    memset(regions, 0, sizeof(regions));
    regions[0].name_hash = FIXTURE_R0_HASH;
    regions[0].source_w = 64;
    regions[0].source_h = 64;
    regions[0].origin_x = r0_origin_x;
    regions[0].origin_y = r0_origin_y;
    regions[0].vertex_start = 0;
    regions[0].index_start = 0;
    regions[0].vertex_count = 4;
    regions[0].index_count = 6;
    regions[0].page_index = 0;
    regions[0].transform = 0;

    regions[1].name_hash = FIXTURE_R1_HASH;
    regions[1].source_w = 32;
    regions[1].source_h = 48;
    regions[1].origin_x = r1_origin_x;
    regions[1].origin_y = r1_origin_y;
    regions[1].vertex_start = 4;
    regions[1].index_start = 6;
    regions[1].vertex_count = 4;
    regions[1].index_count = 6;
    regions[1].page_index = 0;
    regions[1].transform = 0;

    mock_atlas_spec_t spec = {
        .regions = regions,
        .region_count = 2,
        .vertices = verts,
        .total_vertex_count = 8,
        .indices = indices,
        .total_index_count = 12,
        .page_ids = NULL,
        .page_count = 0,
    };
    uint32_t atlas_blob_size = build_mock_atlas_blob(atlas_blob, cap, &spec);
    // #endregion

    return atlas_blob_size;
}

static uint8_t *build_pack_blob_for_atlas(uint64_t atlas_rid, const uint8_t *atlas_blob, uint32_t atlas_blob_size) {
    // #region Build NEOPAK pack containing the atlas blob
    const uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry));
    const uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(uint32_t)(NT_PACK_DATA_ALIGN - 1U);
    const uint32_t atlas_offset = header_size;
    const uint32_t aligned_atlas = (atlas_blob_size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(uint32_t)(NT_PACK_ASSET_ALIGN - 1U);
    const uint32_t total_size = atlas_offset + aligned_atlas;

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
    entry[0].resource_id = atlas_rid;
    entry[0].asset_type = NT_ASSET_ATLAS;
    entry[0].format_version = NT_ATLAS_VERSION;
    entry[0].offset = atlas_offset;
    entry[0].size = atlas_blob_size;
    entry[0].meta_offset = 0;
    entry[0]._pad = 0;

    memcpy(pack_blob + atlas_offset, atlas_blob, atlas_blob_size);
    ph->checksum = nt_crc32(pack_blob + header_size, total_size - header_size);
    // #endregion

    TEST_ASSERT_TRUE_MESSAGE(s_pack_blob_count < 4, "pack blob test fixture overflow");
    s_pack_blobs[s_pack_blob_count++] = pack_blob;
    return pack_blob;
}

static void setup_atlas_fixture(bool resolve_now) {
    uint8_t atlas_blob[1024];
    uint32_t atlas_blob_size = build_fixture_atlas_blob(atlas_blob, sizeof(atlas_blob), 0.5F, 0.5F, 0.25F, 0.75F);

    const uint64_t atlas_rid = 0x1234567890ABCDEFULL;
    uint8_t *pack_blob = build_pack_blob_for_atlas(atlas_rid, atlas_blob, atlas_blob_size);

    // #region Resource system init + parse + optional step
    nt_resource_init(NULL);
    nt_atlas_init();

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount((nt_hash32_t){.value = 0xDEAD}, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack((nt_hash32_t){.value = 0xDEAD}, pack_blob, ((NtPackHeader *)pack_blob)->total_size));

    s_atlas_res = nt_resource_request((nt_hash64_t){.value = atlas_rid}, NT_ASSET_ATLAS);
    TEST_ASSERT_TRUE(s_atlas_res.id != 0);

    if (resolve_now) {
        nt_resource_step();
        TEST_ASSERT_TRUE_MESSAGE(nt_resource_is_ready(s_atlas_res), "atlas not ready after step");
    }
    // #endregion

    s_atlas_initialized = true;
}

static void teardown_atlas_fixture(void) {
    if (s_atlas_initialized) {
        nt_atlas_test_reset();
        nt_resource_shutdown();
        s_atlas_initialized = false;
    }
    for (uint8_t i = 0; i < s_pack_blob_count; i++) {
        free(s_pack_blobs[i]);
        s_pack_blobs[i] = NULL;
    }
    s_pack_blob_count = 0;
}

/* ---- setUp / tearDown ---- */

void setUp(void) {
    s_atlas_initialized = false;
    s_atlas_res = NT_RESOURCE_INVALID;
    memset(s_pack_blobs, 0, sizeof(s_pack_blobs));
    s_pack_blob_count = 0;
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 16});
    nt_sprite_comp_init(&(nt_sprite_comp_desc_t){.capacity = 16});
}

void tearDown(void) {
    nt_sprite_comp_shutdown();
    teardown_atlas_fixture();
    nt_entity_shutdown();
}

/* ---- Test 1: add returns true ---- */

void test_sprite_add_returns_true(void) {
    nt_entity_t e = nt_entity_create();
    bool ok = nt_sprite_comp_add(e);
    TEST_ASSERT_TRUE(ok);
}

/* ---- Test 2: add defaults ---- */

void test_sprite_add_defaults(void) {
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);

    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_comp_atlas(e)->id);
    TEST_ASSERT_EQUAL_UINT64(0ULL, *nt_sprite_comp_region_hash(e));
    TEST_ASSERT_EQUAL_UINT16(0, *nt_sprite_comp_region_index(e));
    TEST_ASSERT_FALSE(nt_sprite_comp_is_resolved(e));
    TEST_ASSERT_EQUAL_UINT8(0, *nt_sprite_comp_flags(e));
}

/* ---- Test 3: bind-by-hash stores desired sprite before atlas is ready ---- */

void test_sprite_bind_by_hash_is_deferred(void) {
    setup_atlas_fixture(false);

    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_bind_by_hash(e, s_atlas_res, FIXTURE_R1_HASH);

    TEST_ASSERT_EQUAL_UINT32(s_atlas_res.id, nt_sprite_comp_atlas(e)->id);
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_R1_HASH, *nt_sprite_comp_region_hash(e));
    TEST_ASSERT_FALSE(nt_sprite_comp_is_resolved(e));
    TEST_ASSERT_EQUAL_UINT16(0, *nt_sprite_comp_region_index(e));

    const float *origin = nt_sprite_comp_origin(e);
    TEST_ASSERT_TRUE(origin[0] == 0.0F); /* NOLINT */
    TEST_ASSERT_TRUE(origin[1] == 0.0F); /* NOLINT */
}

/* ---- Test 4: sync resolves cached index and authored origin ---- */

void test_sprite_sync_resolves_ready_atlas(void) {
    setup_atlas_fixture(false);

    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_bind_by_hash(e, s_atlas_res, FIXTURE_R1_HASH);

    nt_resource_step();
    TEST_ASSERT_TRUE(nt_resource_is_ready(s_atlas_res));

    nt_sprite_comp_sync_resources();

    TEST_ASSERT_TRUE(nt_sprite_comp_is_resolved(e));
    TEST_ASSERT_EQUAL_UINT16(1, *nt_sprite_comp_region_index(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_RESOLVED, NT_SPRITE_FLAG_RESOLVED, *nt_sprite_comp_flags(e));

    const float *origin = nt_sprite_comp_origin(e);
    TEST_ASSERT_TRUE(origin[0] == 0.25F); /* NOLINT */
    TEST_ASSERT_TRUE(origin[1] == 0.75F); /* NOLINT */
}

/* ---- Test 5: set_region is strict fast path for a ready atlas ---- */

void test_sprite_set_region_resolves_immediately(void) {
    setup_atlas_fixture(true);

    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_set_region(e, s_atlas_res, 0);

    TEST_ASSERT_TRUE(nt_sprite_comp_is_resolved(e));
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_R0_HASH, *nt_sprite_comp_region_hash(e));
    TEST_ASSERT_EQUAL_UINT16(0, *nt_sprite_comp_region_index(e));

    const float *origin = nt_sprite_comp_origin(e);
    TEST_ASSERT_TRUE(origin[0] == 0.5F); /* NOLINT */
    TEST_ASSERT_TRUE(origin[1] == 0.5F); /* NOLINT */
}

/* ---- Test 6: explicit origin override survives deferred resolve ---- */

void test_sprite_override_survives_sync(void) {
    setup_atlas_fixture(false);

    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_bind_by_hash(e, s_atlas_res, FIXTURE_R1_HASH);
    nt_sprite_comp_set_origin(e, 0.1F, 0.2F);

    nt_resource_step();
    nt_sprite_comp_sync_resources();

    TEST_ASSERT_TRUE(nt_sprite_comp_is_resolved(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV | NT_SPRITE_FLAG_RESOLVED, NT_SPRITE_FLAG_ORIGIN_OV | NT_SPRITE_FLAG_RESOLVED, *nt_sprite_comp_flags(e));

    const float *origin = nt_sprite_comp_origin(e);
    TEST_ASSERT_TRUE(origin[0] == 0.1F); /* NOLINT */
    TEST_ASSERT_TRUE(origin[1] == 0.2F); /* NOLINT */
}

/* ---- Test 7: reset_origin restores authored origin after resolve ---- */

void test_sprite_reset_origin_restores_authored_origin(void) {
    setup_atlas_fixture(true);

    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_bind_by_hash(e, s_atlas_res, FIXTURE_R1_HASH);
    nt_sprite_comp_sync_resources();
    nt_sprite_comp_set_origin(e, 0.9F, 0.1F);
    nt_sprite_comp_reset_origin(e);

    TEST_ASSERT_TRUE(nt_sprite_comp_is_resolved(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, 0, *nt_sprite_comp_flags(e));

    const float *origin = nt_sprite_comp_origin(e);
    TEST_ASSERT_TRUE(origin[0] == 0.25F); /* NOLINT */
    TEST_ASSERT_TRUE(origin[1] == 0.75F); /* NOLINT */
}

/* ---- Test 8: set_flip preserves origin override ---- */

void test_sprite_flip_preserves_origin_override(void) {
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_set_origin(e, 0.5F, 0.5F);

    nt_sprite_comp_set_flip(e, true, false);

    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, NT_SPRITE_FLAG_ORIGIN_OV, *nt_sprite_comp_flags(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_X, NT_SPRITE_FLAG_FLIP_X, *nt_sprite_comp_flags(e));
}

/* ---- Test 9: swap-and-pop preserves cached sprite data ---- */

void test_sprite_swap_and_pop_preserves_state(void) {
    setup_atlas_fixture(true);

    nt_entity_t e1 = nt_entity_create();
    nt_entity_t e2 = nt_entity_create();
    nt_sprite_comp_add(e1);
    nt_sprite_comp_add(e2);

    nt_sprite_comp_bind_by_hash(e1, s_atlas_res, FIXTURE_R0_HASH);
    nt_sprite_comp_bind_by_hash(e2, s_atlas_res, FIXTURE_R1_HASH);
    nt_sprite_comp_set_origin(e2, 0.1F, 0.9F);
    nt_sprite_comp_sync_resources();

    nt_sprite_comp_remove(e1);

    TEST_ASSERT_FALSE(nt_sprite_comp_has(e1));
    TEST_ASSERT_TRUE(nt_sprite_comp_has(e2));
    TEST_ASSERT_TRUE(nt_sprite_comp_is_resolved(e2));
    TEST_ASSERT_EQUAL_UINT64(FIXTURE_R1_HASH, *nt_sprite_comp_region_hash(e2));
    TEST_ASSERT_EQUAL_UINT16(1, *nt_sprite_comp_region_index(e2));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV | NT_SPRITE_FLAG_RESOLVED, NT_SPRITE_FLAG_ORIGIN_OV | NT_SPRITE_FLAG_RESOLVED, *nt_sprite_comp_flags(e2));

    const float *origin = nt_sprite_comp_origin(e2);
    TEST_ASSERT_TRUE(origin[0] == 0.1F); /* NOLINT */
    TEST_ASSERT_TRUE(origin[1] == 0.9F); /* NOLINT */
}

/* ---- Test 10: republish bumps atlas revision and refreshes authored origin ---- */

void test_sprite_sync_refreshes_authored_origin_on_atlas_republish(void) {
    setup_atlas_fixture(true);

    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_bind_by_hash(e, s_atlas_res, FIXTURE_R1_HASH);
    nt_sprite_comp_sync_resources();

    const float *origin_before = nt_sprite_comp_origin(e);
    TEST_ASSERT_TRUE(origin_before[0] == 0.25F); /* NOLINT */
    TEST_ASSERT_TRUE(origin_before[1] == 0.75F); /* NOLINT */
    uint32_t revision_before = nt_atlas_revision(s_atlas_res);

    uint8_t atlas_blob[1024];
    uint32_t atlas_blob_size = build_fixture_atlas_blob(atlas_blob, sizeof(atlas_blob), 0.5F, 0.5F, 0.6F, 0.2F);
    uint8_t *pack_blob = build_pack_blob_for_atlas(0x1234567890ABCDEFULL, atlas_blob, atlas_blob_size);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount((nt_hash32_t){.value = 0xBEEF}, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack((nt_hash32_t){.value = 0xBEEF}, pack_blob, ((NtPackHeader *)pack_blob)->total_size));

    nt_resource_step();
    nt_sprite_comp_sync_resources();

    TEST_ASSERT_TRUE(nt_atlas_revision(s_atlas_res) != revision_before);
    TEST_ASSERT_TRUE(nt_sprite_comp_is_resolved(e));

    const float *origin_after = nt_sprite_comp_origin(e);
    TEST_ASSERT_TRUE(origin_after[0] == 0.6F); /* NOLINT */
    TEST_ASSERT_TRUE(origin_after[1] == 0.2F); /* NOLINT */
}

/* ---- Test 11: republish does not overwrite explicit origin override ---- */

void test_sprite_sync_preserves_override_on_atlas_republish(void) {
    setup_atlas_fixture(true);

    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_bind_by_hash(e, s_atlas_res, FIXTURE_R1_HASH);
    nt_sprite_comp_sync_resources();
    nt_sprite_comp_set_origin(e, 0.1F, 0.9F);
    uint32_t revision_before = nt_atlas_revision(s_atlas_res);

    uint8_t atlas_blob[1024];
    uint32_t atlas_blob_size = build_fixture_atlas_blob(atlas_blob, sizeof(atlas_blob), 0.5F, 0.5F, 0.6F, 0.2F);
    uint8_t *pack_blob = build_pack_blob_for_atlas(0x1234567890ABCDEFULL, atlas_blob, atlas_blob_size);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount((nt_hash32_t){.value = 0xBEEF}, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack((nt_hash32_t){.value = 0xBEEF}, pack_blob, ((NtPackHeader *)pack_blob)->total_size));

    nt_resource_step();
    nt_sprite_comp_sync_resources();

    TEST_ASSERT_TRUE(nt_atlas_revision(s_atlas_res) != revision_before);
    TEST_ASSERT_TRUE(nt_sprite_comp_is_resolved(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, NT_SPRITE_FLAG_ORIGIN_OV, *nt_sprite_comp_flags(e));

    const float *origin = nt_sprite_comp_origin(e);
    TEST_ASSERT_TRUE(origin[0] == 0.1F); /* NOLINT */
    TEST_ASSERT_TRUE(origin[1] == 0.9F); /* NOLINT */
}

/* ---- Test 12: entity destroy auto-removes sprite ---- */

void test_entity_destroy_removes_sprite(void) {
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    TEST_ASSERT_TRUE(nt_sprite_comp_has(e));
    nt_entity_destroy(e);
    TEST_ASSERT_FALSE(nt_sprite_comp_has(e));
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sprite_add_returns_true);
    RUN_TEST(test_sprite_add_defaults);
    RUN_TEST(test_sprite_bind_by_hash_is_deferred);
    RUN_TEST(test_sprite_sync_resolves_ready_atlas);
    RUN_TEST(test_sprite_set_region_resolves_immediately);
    RUN_TEST(test_sprite_override_survives_sync);
    RUN_TEST(test_sprite_reset_origin_restores_authored_origin);
    RUN_TEST(test_sprite_flip_preserves_origin_override);
    RUN_TEST(test_sprite_swap_and_pop_preserves_state);
    RUN_TEST(test_sprite_sync_refreshes_authored_origin_on_atlas_republish);
    RUN_TEST(test_sprite_sync_preserves_override_on_atlas_republish);
    RUN_TEST(test_entity_destroy_removes_sprite);
    return UNITY_END();
}
