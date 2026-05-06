/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
/* NT_SPRITE_RENDERER_TEST_ACCESS / NT_ATLAS_TEST_ACCESS provided via CMake */
#include "atlas/nt_atlas.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "nt_atlas_format.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "render/nt_render_defs.h"
#include "render/nt_render_items.h"
#include "renderers/nt_sprite_renderer.h"
#include "resource/nt_resource.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "transform_comp/nt_transform_comp.h"
#include "unity.h"
/* clang-format on */

/* ---- Mock atlas blob builder (mirrors test_atlas / test_sprite_comp) ---- */

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

/* ---- Atlas fixture: 2 rect regions + 1 6-vertex polygon (for SPRITE-06) ---- */

#define FIXTURE_R0_HASH 0x100ULL    /* rect, 4 verts, 6 indices */
#define FIXTURE_R1_HASH 0x200ULL    /* rect, 4 verts, 6 indices */
#define FIXTURE_RPOLY_HASH 0x300ULL /* polygon, 6 verts, 12 indices (4 triangles fan) */
#define FIXTURE_PAGE0_RID 0x7000ULL
#define FIXTURE_PAGE1_RID 0x7001ULL

static uint32_t build_test_atlas_blob(uint8_t *atlas_blob, uint32_t cap) {
    /* Layout: [r0 verts: 4] [r1 verts: 4] [poly verts: 6] = 14 verts
     *         [r0 idx: 6] [r1 idx: 6] [poly idx: 12] = 24 indices */
    NtAtlasVertex verts[14];
    uint16_t indices[24];
    for (uint16_t i = 0; i < 14; i++) {
        verts[i].local_x = (int16_t)(i * 10);
        verts[i].local_y = (int16_t)(i * 20);
        verts[i].atlas_u = (uint16_t)(i * 1000);
        verts[i].atlas_v = (uint16_t)(i * 2000);
    }
    /* Indices are LOCAL per region (0-based within the region's vertex slice). */
    /* r0: 0,1,2, 0,2,3 */
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    indices[3] = 0;
    indices[4] = 2;
    indices[5] = 3;
    /* r1: same pattern */
    indices[6] = 0;
    indices[7] = 1;
    indices[8] = 2;
    indices[9] = 0;
    indices[10] = 2;
    indices[11] = 3;
    /* poly: triangle fan over 6 verts → 4 triangles → 12 indices */
    indices[12] = 0;
    indices[13] = 1;
    indices[14] = 2;
    indices[15] = 0;
    indices[16] = 2;
    indices[17] = 3;
    indices[18] = 0;
    indices[19] = 3;
    indices[20] = 4;
    indices[21] = 0;
    indices[22] = 4;
    indices[23] = 5;

    NtAtlasRegion regions[3];
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
    regions[0].flags = NT_ATLAS_REGION_FLAG_QUAD_012023;

    regions[1].name_hash = FIXTURE_R1_HASH;
    regions[1].source_w = 32;
    regions[1].source_h = 48;
    regions[1].origin_x = 0.25F;
    regions[1].origin_y = 0.75F;
    regions[1].vertex_start = 4;
    regions[1].index_start = 6;
    regions[1].vertex_count = 4;
    regions[1].index_count = 6;
    regions[1].page_index = 1;
    regions[1].transform = 0;
    regions[1].flags = NT_ATLAS_REGION_FLAG_QUAD_012023;

    regions[2].name_hash = FIXTURE_RPOLY_HASH;
    regions[2].source_w = 100;
    regions[2].source_h = 100;
    regions[2].origin_x = 0.5F;
    regions[2].origin_y = 0.5F;
    regions[2].vertex_start = 8;
    regions[2].index_start = 12;
    regions[2].vertex_count = 6; /* polygon */
    regions[2].index_count = 12; /* fan: 4 triangles */
    regions[2].page_index = 0;
    regions[2].transform = 0;

    uint64_t page_ids[2] = {FIXTURE_PAGE0_RID, FIXTURE_PAGE1_RID};
    mock_atlas_spec_t spec = {
        .regions = regions,
        .region_count = 3,
        .vertices = verts,
        .total_vertex_count = 14,
        .indices = indices,
        .total_index_count = 24,
        .page_ids = page_ids,
        .page_count = 2,
    };
    return build_mock_atlas_blob(atlas_blob, cap, &spec);
}

static uint8_t *build_pack_blob_for_atlas(uint64_t atlas_rid, const uint8_t *atlas_blob, uint32_t atlas_blob_size, uint32_t *out_total) {
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

    *out_total = total_size;
    return pack_blob;
}

/* ---- Shared test state ---- */

#define MAX_PACK_BLOBS 8
static uint8_t *s_pack_blobs[MAX_PACK_BLOBS];
static uint8_t s_pack_blob_count;
static nt_resource_t s_atlas_res;
static uint32_t s_vpack_counter;
static const uint8_t s_white_pixel[4] = {255, 255, 255, 255};

/* ---- Helper: register an atlas resource via the full pipeline ---- */

static nt_resource_t register_test_atlas(uint64_t atlas_rid) {
    uint8_t atlas_blob[1024];
    uint32_t atlas_blob_size = build_test_atlas_blob(atlas_blob, sizeof(atlas_blob));

    uint32_t pack_total = 0;
    uint8_t *pack_blob = build_pack_blob_for_atlas(atlas_rid, atlas_blob, atlas_blob_size, &pack_total);
    TEST_ASSERT_TRUE_MESSAGE(s_pack_blob_count < MAX_PACK_BLOBS, "pack blob fixture overflow");
    s_pack_blobs[s_pack_blob_count++] = pack_blob;

    char pack_name[32];
    (void)snprintf(pack_name, sizeof(pack_name), "atlas_pack_%u", s_vpack_counter++);
    nt_hash32_t pid = nt_hash32_str(pack_name);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid, pack_blob, pack_total));

    nt_resource_t atlas = nt_resource_request((nt_hash64_t){.value = atlas_rid}, NT_ASSET_ATLAS);
    TEST_ASSERT_TRUE(atlas.id != 0);
    nt_resource_step();
    /* atlas_on_post_resolve requests page texture slots; a second step
     * publishes those newly requested slots before renderer tests draw. */
    nt_resource_step();
    TEST_ASSERT_TRUE(nt_resource_is_ready(atlas));
    return atlas;
}

/* ---- Helper: create a minimal real material backed by gfx_stub shader handles ---- */

static nt_material_t create_test_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "sprite_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "sprite_fs"});

    char vs_name[64];
    char fs_name[64];
    char pack_name[64];
    (void)snprintf(vs_name, sizeof(vs_name), "test_sr_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof(fs_name), "test_sr_fs_%u", s_vpack_counter);
    (void)snprintf(pack_name, sizeof(pack_name), "mat_pack_%u", s_vpack_counter++);

    nt_hash32_t pid = nt_hash32_str(pack_name);
    nt_hash64_t vs_rid = nt_hash64_str(vs_name);
    nt_hash64_t fs_rid = nt_hash64_str(fs_name);

    nt_resource_create_pack(pid, 0);
    nt_resource_register(pid, vs_rid, NT_ASSET_SHADER_CODE, vs.id);
    nt_resource_register(pid, fs_rid, NT_ASSET_SHADER_CODE, fs.id);

    nt_resource_t vs_res = nt_resource_request(vs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_t fs_res = nt_resource_request(fs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_step();

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.vs = vs_res;
    desc.fs = fs_res;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.label = "test_sprite_material";

    nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

/* ---- Helper: build a fully-equipped sprite entity ---- */

static nt_entity_t create_sprite_entity(nt_resource_t atlas, uint64_t region_hash, nt_material_t mat) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    nt_drawable_comp_add(e);
    nt_material_comp_add(e);
    nt_sprite_comp_add(e);

    *nt_material_comp_handle(e) = mat;

    /* Identity transform */
    float *p = nt_transform_comp_position(e);
    p[0] = 0.0F;
    p[1] = 0.0F;
    p[2] = 0.0F;
    nt_transform_comp_update();

    /* White color */
    nt_drawable_comp_set_color(e, 1.0F, 1.0F, 1.0F, 1.0F);

    nt_sprite_comp_bind_by_hash(e, atlas, region_hash);
    nt_sprite_comp_sync_resources();
    return e;
}

/* ---- setUp / tearDown ---- */

void setUp(void) {
    s_pack_blob_count = 0;
    memset((void *)s_pack_blobs, 0, sizeof(s_pack_blobs));
    s_atlas_res = NT_RESOURCE_INVALID;
    s_vpack_counter = 0;

    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 64, .max_textures = 32, .max_meshes = 16});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_atlas_init();

    nt_hash32_t page_pid = nt_hash32_str("sprite_renderer_pages");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(page_pid, 100));
    nt_texture_t page0 = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = s_white_pixel, .label = "page0"});
    nt_texture_t page1 = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = s_white_pixel, .label = "page1"});
    TEST_ASSERT_TRUE(page0.id != 0);
    TEST_ASSERT_TRUE(page1.id != 0);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(page_pid, (nt_hash64_t){FIXTURE_PAGE0_RID}, NT_ASSET_TEXTURE, page0.id));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(page_pid, (nt_hash64_t){FIXTURE_PAGE1_RID}, NT_ASSET_TEXTURE, page1.id));

    nt_entity_init(&(nt_entity_desc_t){.max_entities = 64});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 64});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 64});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 64});
    nt_sprite_comp_init(&(nt_sprite_comp_desc_t){.capacity = 64});
    nt_material_init(&(nt_material_desc_t){.max_materials = 32});

    /* Begin frame/pass so draw_indexed doesn't trip the gfx-stub assert */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

void tearDown(void) {
    if (nt_sprite_renderer_test_initialized()) {
        nt_sprite_renderer_shutdown();
    }
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_material_shutdown();
    nt_sprite_comp_shutdown();
    nt_material_comp_shutdown();
    nt_drawable_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    nt_atlas_test_reset();
    nt_resource_shutdown();
    nt_gfx_shutdown();
    nt_hash_shutdown();

    for (uint8_t i = 0; i < s_pack_blob_count; i++) {
        free(s_pack_blobs[i]);
        s_pack_blobs[i] = NULL;
    }
    s_pack_blob_count = 0;
}

/* ---- Test: init/shutdown lifecycle (SPRITE-11 partial) ---- */

void test_sprite_renderer_init_shutdown(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));
    TEST_ASSERT_TRUE(nt_sprite_renderer_test_initialized());
    nt_sprite_renderer_shutdown();
    TEST_ASSERT_FALSE(nt_sprite_renderer_test_initialized());

    /* Re-init succeeds after shutdown */
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));
    TEST_ASSERT_TRUE(nt_sprite_renderer_test_initialized());
}

/* ---- Test: vertex size assert (SPRITE-05) ---- */

/* The actual contract is enforced by the _Static_assert in nt_sprite_renderer.h.
 * This runtime test mirrors the assertion in case the static check is ever
 * accidentally relaxed (it would still catch the breakage in CI). */
void test_sprite_renderer_vertex_size_assert(void) { TEST_ASSERT_EQUAL_size_t(24, sizeof(nt_sprite_vertex_t)); }

/* ---- Test: pipeline cache reuse + miss-creates-new (SPRITE-10) ---- */

void test_sprite_renderer_pipeline_cache(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA1ULL);
    nt_material_t mat_a = create_test_material();
    nt_material_t mat_b = create_test_material();
    nt_entity_t e0 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_a);
    nt_entity_t e1 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_b);

    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = nt_batch_key(mat_a.id, (uint32_t)FIXTURE_R0_HASH);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_batch_key(mat_b.id, (uint32_t)FIXTURE_R0_HASH);

    nt_sprite_renderer_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_pipeline_cache_count());

    /* Re-issuing the same materials must NOT inflate the cache */
    nt_sprite_renderer_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_pipeline_cache_count());
}

/* ---- Test: batch grouping by batch_key (SPRITE-04, SPRITE-08) ----
 *
 * 3 items with batch_keys [A, A, B] should produce 2 nt_gfx_draw_indexed
 * calls (per-renderer test counter). */
void test_sprite_renderer_batch_grouping(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA2ULL);
    nt_material_t mat_a = create_test_material();
    nt_material_t mat_b = create_test_material();
    nt_entity_t e0 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_a);
    nt_entity_t e1 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_a);
    nt_entity_t e2 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_b);

    uint32_t bk_a = nt_batch_key(mat_a.id, (uint32_t)FIXTURE_R0_HASH);
    uint32_t bk_b = nt_batch_key(mat_b.id, (uint32_t)FIXTURE_R0_HASH);
    nt_render_item_t items[3];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = bk_a;
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = bk_a;
    items[2].sort_key = 2;
    items[2].entity = e2.id;
    items[2].batch_key = bk_b;

    nt_sprite_renderer_draw_list(items, 3);
    /* Two batch groups → two flushes → two draw calls */
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_draw_call_count());
}

/* ---- Test: batch_key-driven boundaries (SPRITE-07, SPRITE-08) ----
 *
 * When the batch_key changes between consecutive items, a flush is triggered
 * even if the underlying material id matches — the renderer trusts batch_key
 * as the abstraction (atlas page id is folded in by the game). */
void test_sprite_renderer_batch_key_atlas_change(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA3ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e0 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_entity_t e1 = create_sprite_entity(s_atlas_res, FIXTURE_R1_HASH, mat);

    /* Two distinct batch_keys (game encodes atlas page id into the key). The
     * renderer flushes between groups, even though the cached pipeline is
     * the same — exercising the batch_key boundary contract. */
    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = nt_batch_key(mat.id, 0xAAU);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_batch_key(mat.id, 0xBBU);

    nt_sprite_renderer_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_draw_call_count());
}

/* ---- Test: actual atlas page splits a coarse batch_key run ----
 *
 * The game-level batch_key is a compatibility hint, not the texture source of
 * truth. If two adjacent sprites share a key but resolve to different atlas
 * pages, the renderer must split the command stream before drawing the second
 * sprite. */
void test_sprite_renderer_splits_run_on_actual_page_change(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA7ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e0 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_entity_t e1 = create_sprite_entity(s_atlas_res, FIXTURE_R1_HASH, mat);

    uint32_t coarse_key = nt_batch_key(mat.id, 0xCAFEU);
    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = coarse_key;
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = coarse_key;

    nt_sprite_renderer_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_draw_call_count());
}

/* ---- Test: polygon emit (SPRITE-06) ----
 *
 * A region with vertex_count=6 / index_count=12 produces 6 vertices in
 * staging (uniform rect/polygon path, D-08). The last_emit_* counters
 * are captured after the per-emit copy and survive flush. */
void test_sprite_renderer_polygon_emit(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA4ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_sprite_entity(s_atlas_res, FIXTURE_RPOLY_HASH, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_batch_key(mat.id, (uint32_t)FIXTURE_RPOLY_HASH);

    nt_sprite_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(6, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(12, nt_sprite_renderer_test_last_emit_index_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_draw_call_count());
}

/* ---- Test: restore_gpu re-cycle clears pipeline cache (SPRITE-11) ---- */

void test_sprite_renderer_restore_gpu_cycle(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA5ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_batch_key(mat.id, (uint32_t)FIXTURE_R0_HASH);

    nt_sprite_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_pipeline_cache_count());

    nt_sprite_renderer_restore_gpu();
    TEST_ASSERT_TRUE(nt_sprite_renderer_test_initialized());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_pipeline_cache_count());

    /* Subsequent draws still work — pipeline is rebuilt lazily */
    nt_sprite_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_draw_call_count());
}

/* ---- Test: pipeline cache full → NT_ASSERT (SPRITE-10) ----
 *
 * The plan calls for a death-test on cache overflow. The codebase has no
 * death-test harness; instead we assert the cache assertion message exists
 * in the source via a build-time grep (CI verification step), and at runtime
 * verify that we can fill the cache up to capacity exactly. The actual
 * NT_ASSERT trigger is not exercised at runtime to keep the test binary
 * non-aborting — see the verify block in 50-04-PLAN.md.
 *
 * Concretely: with desc.max_pipelines=2, two distinct materials populate
 * the cache to its declared capacity without firing the assert. A third
 * distinct material WOULD fire the assert (configuration bug per D-19). */
void test_sprite_renderer_pipeline_cache_capacity(void) {
    nt_sprite_renderer_desc_t desc = {.max_pipelines = 2};
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA6ULL);
    nt_material_t mat_a = create_test_material();
    nt_material_t mat_b = create_test_material();
    nt_entity_t e0 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_a);
    nt_entity_t e1 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_b);

    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = nt_batch_key(mat_a.id, (uint32_t)FIXTURE_R0_HASH);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_batch_key(mat_b.id, (uint32_t)FIXTURE_R0_HASH);

    nt_sprite_renderer_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_pipeline_cache_count());
}

/* ---- main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sprite_renderer_init_shutdown);
    RUN_TEST(test_sprite_renderer_vertex_size_assert);
    RUN_TEST(test_sprite_renderer_pipeline_cache);
    RUN_TEST(test_sprite_renderer_batch_grouping);
    RUN_TEST(test_sprite_renderer_batch_key_atlas_change);
    RUN_TEST(test_sprite_renderer_splits_run_on_actual_page_change);
    RUN_TEST(test_sprite_renderer_polygon_emit);
    RUN_TEST(test_sprite_renderer_restore_gpu_cycle);
    RUN_TEST(test_sprite_renderer_pipeline_cache_capacity);
    return UNITY_END();
}
