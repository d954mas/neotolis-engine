/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "atlas/nt_atlas.h"
#include "entity/nt_entity.h"
#include "hash/nt_hash.h"
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
static uint8_t *s_pack_blob; /* heap-allocated pack blob for cleanup */

/* ---- Atlas fixture: full resource pipeline ---- */

static void setup_atlas_fixture(void) {
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
    regions[1].vertex_count = 4;
    regions[1].index_count = 6;
    regions[1].page_index = 0;
    regions[1].transform = 0;

    uint8_t atlas_blob[1024];
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
    uint32_t atlas_blob_size = build_mock_atlas_blob(atlas_blob, sizeof(atlas_blob), &spec);
    // #endregion

    // #region Build NEOPAK pack containing the atlas blob
    const uint64_t atlas_rid = 0x1234567890ABCDEFULL;
    const uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry));
    const uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(uint32_t)(NT_PACK_DATA_ALIGN - 1U);
    const uint32_t atlas_offset = header_size;
    const uint32_t aligned_atlas = (atlas_blob_size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(uint32_t)(NT_PACK_ASSET_ALIGN - 1U);
    const uint32_t total_size = atlas_offset + aligned_atlas;

    s_pack_blob = (uint8_t *)calloc(1, total_size);
    TEST_ASSERT_NOT_NULL(s_pack_blob);

    NtPackHeader *ph = (NtPackHeader *)s_pack_blob;
    ph->magic = NT_PACK_MAGIC;
    ph->version = NT_PACK_VERSION;
    ph->asset_count = 1;
    ph->header_size = header_size;
    ph->total_size = total_size;
    ph->meta_offset = 0;
    ph->meta_count = 0;

    NtAssetEntry *entry = (NtAssetEntry *)(s_pack_blob + sizeof(NtPackHeader));
    entry[0].resource_id = atlas_rid;
    entry[0].asset_type = NT_ASSET_ATLAS;
    entry[0].format_version = NT_ATLAS_VERSION;
    entry[0].offset = atlas_offset;
    entry[0].size = atlas_blob_size;
    entry[0].meta_offset = 0;
    entry[0]._pad = 0;

    memcpy(s_pack_blob + atlas_offset, atlas_blob, atlas_blob_size);
    ph->checksum = nt_crc32(s_pack_blob + header_size, total_size - header_size);
    // #endregion

    // #region Resource system init + parse + step
    nt_resource_init(NULL);
    nt_atlas_init();

    nt_hash32_t pid = {.value = 0xDEAD};
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, s_pack_blob, total_size));

    nt_hash64_t rid = {.value = atlas_rid};
    s_atlas_res = nt_resource_request(rid, NT_ASSET_ATLAS);
    TEST_ASSERT_TRUE(s_atlas_res.id != 0);

    nt_resource_step();
    TEST_ASSERT_TRUE_MESSAGE(nt_resource_is_ready(s_atlas_res), "atlas not ready after step");
    // #endregion

    s_atlas_initialized = true;
}

static void teardown_atlas_fixture(void) {
    if (s_atlas_initialized) {
        nt_atlas_test_reset();
        nt_resource_shutdown();
        s_atlas_initialized = false;
    }
    free(s_pack_blob);
    s_pack_blob = NULL;
}

/* ---- setUp / tearDown ---- */

void setUp(void) {
    s_atlas_initialized = false;
    s_atlas_res = NT_RESOURCE_INVALID;
    s_pack_blob = NULL;
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
    TEST_ASSERT_EQUAL_UINT16(0, *nt_sprite_comp_region_index(e));
    TEST_ASSERT_EQUAL_UINT8(0, *nt_sprite_comp_flags(e));
}

/* ---- Test 3: has and remove ---- */

void test_sprite_has_and_remove(void) {
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    TEST_ASSERT_TRUE(nt_sprite_comp_has(e));
    nt_sprite_comp_remove(e);
    TEST_ASSERT_FALSE(nt_sprite_comp_has(e));
}

/* ---- Test 4: set_region stores atlas + region_index ---- */

void test_sprite_set_region(void) {
    setup_atlas_fixture();
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_set_region(e, s_atlas_res, 1);
    TEST_ASSERT_EQUAL_UINT32(s_atlas_res.id, nt_sprite_comp_atlas(e)->id);
    TEST_ASSERT_EQUAL_UINT16(1, *nt_sprite_comp_region_index(e));
}

/* ---- Test 5: set_region clears origin override but preserves flip ---- */

void test_sprite_set_region_resets_flags(void) {
    setup_atlas_fixture();
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_set_flip(e, true, false);
    nt_sprite_comp_set_origin(e, 0.1F, 0.2F);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, NT_SPRITE_FLAG_ORIGIN_OV, *nt_sprite_comp_flags(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_X, NT_SPRITE_FLAG_FLIP_X, *nt_sprite_comp_flags(e));
    nt_sprite_comp_set_region(e, s_atlas_res, 0);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, 0, *nt_sprite_comp_flags(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_X, NT_SPRITE_FLAG_FLIP_X, *nt_sprite_comp_flags(e));
}

/* ---- Test 6: set_region_by_hash resolves hash to index ---- */

void test_sprite_set_region_by_hash(void) {
    setup_atlas_fixture();
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_set_region_by_hash(e, s_atlas_res, FIXTURE_R1_HASH);
    TEST_ASSERT_EQUAL_UINT32(s_atlas_res.id, nt_sprite_comp_atlas(e)->id);
    TEST_ASSERT_EQUAL_UINT16(1, *nt_sprite_comp_region_index(e));
}

/* ---- Test 7: set_origin stores values and sets flag ---- */

void test_sprite_set_origin(void) {
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_set_origin(e, 0.3F, 0.7F);
    const float *o = nt_sprite_comp_origin(e);
    TEST_ASSERT_TRUE(o[0] == 0.3F); /* NOLINT */
    TEST_ASSERT_TRUE(o[1] == 0.7F); /* NOLINT */
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, NT_SPRITE_FLAG_ORIGIN_OV, *nt_sprite_comp_flags(e));
}

/* ---- Test 8: reset_origin clears bit2, leaves bits 0-1 intact ---- */

void test_sprite_reset_origin(void) {
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_set_flip(e, true, true);
    nt_sprite_comp_set_origin(e, 0.5F, 0.5F);
    /* all three bits should be set */
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_X | NT_SPRITE_FLAG_FLIP_Y | NT_SPRITE_FLAG_ORIGIN_OV, NT_SPRITE_FLAG_FLIP_X | NT_SPRITE_FLAG_FLIP_Y | NT_SPRITE_FLAG_ORIGIN_OV, *nt_sprite_comp_flags(e));
    nt_sprite_comp_reset_origin(e);
    /* bit2 cleared, bits 0-1 remain */
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, 0, *nt_sprite_comp_flags(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_X | NT_SPRITE_FLAG_FLIP_Y, NT_SPRITE_FLAG_FLIP_X | NT_SPRITE_FLAG_FLIP_Y, *nt_sprite_comp_flags(e));
}

/* ---- Test 9: set_flip sets bits 0-1 correctly ---- */

void test_sprite_set_flip(void) {
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);

    nt_sprite_comp_set_flip(e, true, false);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_X, NT_SPRITE_FLAG_FLIP_X, *nt_sprite_comp_flags(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_Y, 0, *nt_sprite_comp_flags(e));

    nt_sprite_comp_set_flip(e, false, true);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_X, 0, *nt_sprite_comp_flags(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_Y, NT_SPRITE_FLAG_FLIP_Y, *nt_sprite_comp_flags(e));

    nt_sprite_comp_set_flip(e, true, true);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_X | NT_SPRITE_FLAG_FLIP_Y, NT_SPRITE_FLAG_FLIP_X | NT_SPRITE_FLAG_FLIP_Y, *nt_sprite_comp_flags(e));
}

/* ---- Test 10: set_flip preserves origin override ---- */

void test_sprite_flip_preserves_origin_override(void) {
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_set_origin(e, 0.5F, 0.5F);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, NT_SPRITE_FLAG_ORIGIN_OV, *nt_sprite_comp_flags(e));
    nt_sprite_comp_set_flip(e, true, false);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, NT_SPRITE_FLAG_ORIGIN_OV, *nt_sprite_comp_flags(e));
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_FLIP_X, NT_SPRITE_FLAG_FLIP_X, *nt_sprite_comp_flags(e));
}

/* ---- Test 11: set_region clears origin override ---- */

void test_sprite_set_region_clears_origin_override(void) {
    setup_atlas_fixture();
    nt_entity_t e = nt_entity_create();
    nt_sprite_comp_add(e);
    nt_sprite_comp_set_origin(e, 0.1F, 0.2F);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, NT_SPRITE_FLAG_ORIGIN_OV, *nt_sprite_comp_flags(e));
    nt_sprite_comp_set_region(e, s_atlas_res, 0);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, 0, *nt_sprite_comp_flags(e));
}

/* ---- Test 12: swap-and-pop preserves data ---- */

void test_sprite_swap_and_pop(void) {
    setup_atlas_fixture();
    nt_entity_t e1 = nt_entity_create();
    nt_entity_t e2 = nt_entity_create();
    nt_sprite_comp_add(e1);
    nt_sprite_comp_add(e2);

    nt_sprite_comp_set_region(e1, s_atlas_res, 0);
    nt_sprite_comp_set_flip(e1, true, false);

    nt_sprite_comp_set_region(e2, s_atlas_res, 1);
    nt_sprite_comp_set_origin(e2, 0.1F, 0.9F);

    /* Remove e1 triggers swap-and-pop: e2 data moves to e1's dense slot */
    nt_sprite_comp_remove(e1);

    TEST_ASSERT_FALSE(nt_sprite_comp_has(e1));
    TEST_ASSERT_TRUE(nt_sprite_comp_has(e2));
    TEST_ASSERT_EQUAL_UINT16(1, *nt_sprite_comp_region_index(e2));
    TEST_ASSERT_EQUAL_UINT32(s_atlas_res.id, nt_sprite_comp_atlas(e2)->id);
    TEST_ASSERT_BITS(NT_SPRITE_FLAG_ORIGIN_OV, NT_SPRITE_FLAG_ORIGIN_OV, *nt_sprite_comp_flags(e2));
    const float *o = nt_sprite_comp_origin(e2);
    TEST_ASSERT_TRUE(o[0] == 0.1F); /* NOLINT */
    TEST_ASSERT_TRUE(o[1] == 0.9F); /* NOLINT */
}

/* ---- Test 13: entity destroy auto-removes sprite ---- */

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
    RUN_TEST(test_sprite_has_and_remove);
    RUN_TEST(test_sprite_set_region);
    RUN_TEST(test_sprite_set_region_resets_flags);
    RUN_TEST(test_sprite_set_region_by_hash);
    RUN_TEST(test_sprite_set_origin);
    RUN_TEST(test_sprite_reset_origin);
    RUN_TEST(test_sprite_set_flip);
    RUN_TEST(test_sprite_flip_preserves_origin_override);
    RUN_TEST(test_sprite_set_region_clears_origin_override);
    RUN_TEST(test_sprite_swap_and_pop);
    RUN_TEST(test_entity_destroy_removes_sprite);
    return UNITY_END();
}
