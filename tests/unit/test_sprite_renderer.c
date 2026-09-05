#include "test_helpers/nt_gfx_fake.h"
/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
/* NT_TEST_ACCESS / NT_TEST_ACCESS provided via CMake */
#include "atlas/nt_atlas.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "math/nt_math.h"
#include "nt_atlas_format.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "render/nt_render_defs.h"
#include "render/nt_render_items.h"
#include "renderers/nt_sprite_renderer.h"
#include "resource/nt_resource.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "test_helpers/nt_assert_trap.h"
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

/* ---- Atlas fixture: 2 rect regions + 1 6-vertex polygon ---- */

#define FIXTURE_R0_HASH 0x100ULL    /* rect, 4 verts, 6 indices */
#define FIXTURE_R1_HASH 0x200ULL    /* rect, 4 verts, 6 indices */
#define FIXTURE_RPOLY_HASH 0x300ULL /* polygon, 6 verts, 12 indices (4 triangles fan) */
#define FIXTURE_RS9_HASH 0x400ULL   /* rect with baked slice9 borders 16/16/16/16 */
#define FIXTURE_PAGE0_RID 0x7000ULL
#define FIXTURE_PAGE1_RID 0x7001ULL

static uint32_t build_test_atlas_blob(uint8_t *atlas_blob, uint32_t cap) {
    /* Layout: [r0 verts: 4] [r1 verts: 4] [poly verts: 6] [rs9 verts: 4] = 18 verts
     *         [r0 idx: 6] [r1 idx: 6] [poly idx: 12] [rs9 idx: 6] = 30 indices */
    NtAtlasVertex verts[18];
    uint16_t indices[30];
    for (uint16_t i = 0; i < 18; i++) {
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
    /* rs9: rect quad */
    indices[24] = 0;
    indices[25] = 1;
    indices[26] = 2;
    indices[27] = 0;
    indices[28] = 2;
    indices[29] = 3;

    NtAtlasRegion regions[4];
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

    /* rs9: 100x100 rect with baked slice9 borders {16,16,16,16}. Source big
     * enough that scale=2.0F (→ 32) still satisfies emit_slice9's sl+sr<source_w
     * contract; lets the from_region helper pass borders verbatim. */
    regions[3].name_hash = FIXTURE_RS9_HASH;
    regions[3].source_w = 100;
    regions[3].source_h = 100;
    regions[3].origin_x = 0.0F;
    regions[3].origin_y = 0.0F;
    regions[3].vertex_start = 14;
    regions[3].index_start = 24;
    regions[3].vertex_count = 4;
    regions[3].index_count = 6;
    regions[3].page_index = 0;
    regions[3].transform = 0;
    regions[3].flags = NT_ATLAS_REGION_FLAG_QUAD_012023;
    regions[3].slice9_lrtb[0] = 16;
    regions[3].slice9_lrtb[1] = 16;
    regions[3].slice9_lrtb[2] = 16;
    regions[3].slice9_lrtb[3] = 16;

    uint64_t page_ids[2] = {FIXTURE_PAGE0_RID, FIXTURE_PAGE1_RID};
    mock_atlas_spec_t spec = {
        .regions = regions,
        .region_count = 4,
        .vertices = verts,
        .total_vertex_count = 18,
        .indices = indices,
        .total_index_count = 30,
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

/* ---- Helper: create a minimal real material backed by test backend shader handles ---- */

static nt_material_t create_test_material_with_blend(nt_blend_state_t blend) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1);
    desc.depth_test = false;
    desc.depth_write = false;
    desc.blend = blend;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.textures[0].name = "u_texture";
    desc.texture_count = 1;
    desc.label = "test_sprite_material";

    nt_material_t mat = nt_material_create(&desc);
    return mat;
}

static nt_material_t create_test_material(void) { return create_test_material_with_blend(nt_blend_opaque()); }

/* Slot 0 plus one vec4 param, so a flush's uniform counts are not vacuous. */
static nt_material_t create_test_material_with_param(void) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1);
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.textures[0].name = "u_texture";
    desc.texture_count = 1;
    desc.params[0].name = "u_tint";
    desc.params[0].value[0] = 1.0F;
    desc.param_count = 1;
    desc.label = "test_sprite_material_param";

    nt_material_t mat = nt_material_create(&desc);
    return mat;
}

/* Analytic-coverage shape (nt_ui_radial's flat SDF): borrows region geometry, samples nothing. */
static nt_material_t create_test_material_textureless(void) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = nt_gfx_fake_make_program(NULL, 0);
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.label = "test_sprite_material_textureless";

    nt_material_t mat = nt_material_create(&desc);
    return mat;
}

/* ---- Helper: material declaring a custom per-vertex attr_map ----
 *
 * Shares ONE vs/fs pair across calls so the only pipeline-key axis that varies
 * is the vertex layout — this isolates the layout discriminator: a base material
 * and an attr_map material that differ ONLY in attr_map must still resolve to two
 * distinct pipelines, proving the layout is folded into the key.
 *
 * loc==0 → no attr_map (plain 20B base). loc>0 → one custom FLOAT4 attr
 * "a_radial" bound to that GL location. */
/* One program shared by every radial material — reset in setUp each test
 * (subsystems are re-init'd per test, so cached handles cannot persist). */
static nt_program_t s_radial_shared_program;

static nt_material_t create_radial_test_material(const char *stream_name, uint8_t loc) {
    if (s_radial_shared_program.id == 0) {
        s_radial_shared_program = nt_gfx_fake_make_program(NULL, 0);
    }

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = s_radial_shared_program;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.label = "radial_test_material";
    if (loc != 0) {
        desc.attr_map[0].stream_name = stream_name;
        desc.attr_map[0].location = loc;
        desc.attr_map_count = 1;
    }

    nt_material_t mat = nt_material_create(&desc);
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

static uint32_t sprite_batch_key(nt_entity_t entity, nt_material_t material) {
    nt_sprite_comp_view_t sprites = nt_sprite_comp_view();
    uint16_t dense_idx = sprites.sparse_indices[nt_entity_index(entity)];
    TEST_ASSERT_NOT_EQUAL_UINT16(UINT16_MAX, dense_idx);
    TEST_ASSERT_BITS_HIGH(NT_SPRITE_FLAG_RESOLVED, sprites.flags[dense_idx]);
    return nt_sprite_renderer_batch_key(material, sprites.resolved[dense_idx].page_resource);
}

/* ---- setUp / tearDown ---- */

void setUp(void) {
    s_pack_blob_count = 0;
    memset((void *)s_pack_blobs, 0, sizeof(s_pack_blobs));
    s_atlas_res = NT_RESOURCE_INVALID;
    s_vpack_counter = 0;
    s_radial_shared_program = NT_PROGRAM_INVALID;

    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(
        &(nt_gfx_desc_t){.max_shaders = 32, .max_programs = 16, .max_pipelines = 16, .max_buffers = 64, .max_textures = 32, .max_meshes = 16, .max_vertex_inputs = 16, .max_render_targets = 16});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_atlas_init();

    nt_hash32_t page_pid = nt_hash32_str("sprite_renderer_pages");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(page_pid, 100));
    nt_texture_t page0 = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = s_white_pixel, .format = NT_TEXTURE_FORMAT_RGBA8, .label = "page0"});
    nt_texture_t page1 = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = s_white_pixel, .format = NT_TEXTURE_FORMAT_RGBA8, .label = "page1"});
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

/* ---- Test: init/shutdown lifecycle ---- */

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

static void assert_all_buffer_slots_available(void) {
    nt_buffer_t buffers[64];
    for (uint32_t i = 0; i < 64; i++) {
        buffers[i] = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, buffers[i].id);
    }
    for (uint32_t i = 0; i < 64; i++) {
        nt_gfx_destroy_buffer(buffers[i]);
    }
}

void test_sprite_renderer_init_retries_after_buffer_creation_failure(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    for (uint8_t mask = 1; mask <= 2; mask++) {
        nt_gfx_fake_fail_buffer_creates(mask);
        TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_sprite_renderer_init(&desc));
        TEST_ASSERT_FALSE(nt_sprite_renderer_test_initialized());
        TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_restore_gpu());
        TEST_ASSERT_FALSE(nt_sprite_renderer_test_initialized());
        assert_all_buffer_slots_available();
    }

    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));
    s_atlas_res = register_test_atlas(0xCBULL);
    nt_material_t mat = create_test_material();
    nt_entity_t entity = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_render_item_t item = {.entity = entity.id, .batch_key = sprite_batch_key(entity, mat)};
    nt_sprite_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_draw_call_count());
}

/* ---- Test: vertex size assert ---- */

/* The actual contract is enforced by the _Static_assert in nt_sprite_renderer.h.
 * This runtime test mirrors the assertion in case the static check is ever
 * accidentally relaxed (it would still catch the breakage in CI). */
void test_sprite_renderer_vertex_size_assert(void) { TEST_ASSERT_EQUAL_size_t(20, sizeof(nt_sprite_vertex_t)); }

void test_sprite_renderer_batch_key_packs_material_and_page_slots(void) {
    nt_material_t material = {.id = 0xABCD1234U};
    nt_resource_t page_resource = {.id = 0x43215678U};

    TEST_ASSERT_EQUAL_HEX32(0x12345678U, nt_sprite_renderer_batch_key(material, page_resource));
}

void test_sprite_renderer_batch_key_ignores_handle_generations(void) {
    nt_material_t material_a = {.id = 0x00011234U};
    nt_material_t material_b = {.id = 0xFFFF1234U};
    nt_resource_t page_a = {.id = 0x00015678U};
    nt_resource_t page_b = {.id = 0xFFFF5678U};

    TEST_ASSERT_EQUAL_HEX32(nt_sprite_renderer_batch_key(material_a, page_a), nt_sprite_renderer_batch_key(material_b, page_b));
}

void test_sprite_renderer_batch_key_distinguishes_page_slots(void) {
    nt_material_t material = {.id = 0x00010001U};
    nt_resource_t page_a = {.id = 0x00010001U};
    nt_resource_t page_b = {.id = 0x00010002U};

    TEST_ASSERT_NOT_EQUAL(nt_sprite_renderer_batch_key(material, page_a), nt_sprite_renderer_batch_key(material, page_b));
}

void test_sprite_renderer_draw_list_null_items_asserts_when_nonempty(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    NT_TEST_EXPECT_ASSERT(nt_sprite_renderer_draw_list(NULL, 1));
}

void test_sprite_renderer_draw_list_asserts_on_unresolved_sprite_item(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA0ULL);
    nt_material_t material = create_test_material();
    nt_entity_t entity = nt_entity_create();
    nt_transform_comp_add(entity);
    nt_drawable_comp_add(entity);
    nt_material_comp_add(entity);
    nt_sprite_comp_add(entity);
    *nt_material_comp_handle(entity) = material;

    nt_resource_t page_resource = nt_atlas_get_page_resource(s_atlas_res, 0);
    nt_render_item_t item = {
        .entity = entity.id,
        .batch_key = nt_sprite_renderer_batch_key(material, page_resource),
    };

    NT_TEST_EXPECT_ASSERT(nt_sprite_renderer_draw_list(&item, 1));
}

/* ---- Test: pipeline cache reuse + miss-creates-new ---- */

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
    items[0].batch_key = sprite_batch_key(e0, mat_a);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = sprite_batch_key(e1, mat_b);

    nt_sprite_renderer_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_pipeline_cache_count());

    /* Re-issuing the same materials must NOT inflate the cache */
    nt_sprite_renderer_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_pipeline_cache_count());
}

/* Programs come out of the pool in creation order, so two shader pairs get
 * neighbouring ids; one depth_write step on the neighbour must not land on the same key. */
void test_neighbouring_programs_one_depth_write_step_apart_get_their_own_pipelines(void) {
    nt_sprite_renderer_desc_t rdesc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&rdesc));
    s_atlas_res = register_test_atlas(0xA2ULL);

    nt_program_t p0 = nt_gfx_fake_make_program(NULL, 0);
    nt_program_t p1 = nt_gfx_fake_make_program(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(p0.id + 1, p1.id);

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.depth_test = false;
    desc.blend = nt_blend_opaque();
    desc.cull_mode = NT_CULL_NONE;
    desc.program = p0;
    desc.depth_write = true;
    nt_material_t mat_a = nt_material_create(&desc);
    desc.program = p1;
    desc.depth_write = false;
    nt_material_t mat_b = nt_material_create(&desc);

    nt_entity_t e0 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_a);
    nt_entity_t e1 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = sprite_batch_key(e0, mat_a)},
        {.sort_key = 1, .entity = e1.id, .batch_key = sprite_batch_key(e1, mat_b)},
    };

    nt_gfx_test_draw_trace_reset(true);
    nt_sprite_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(p0.id, nt_gfx_test_draw_trace_at(0).program.id);
    TEST_ASSERT_EQUAL_UINT32(p1.id, nt_gfx_test_draw_trace_at(1).program.id);
}

/* Context restore drops queued commands and cached pipelines without destroying borrowed programs. */
void test_sprite_renderer_reset_drops_commands_and_pipelines(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    nt_material_t mat = create_test_material();
    nt_sprite_renderer_set_material(mat); /* opens a cmd and caches a pipeline */
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_cmd_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_pipeline_cache_count());

    nt_sprite_renderer_restore_gpu();

    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_cmd_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_pipeline_cache_count());

    /* The material may keep or drop the handle; neither is required. */
    nt_material_set_program(mat, NT_PROGRAM_INVALID);
}

/* The restore window with the context already back: the material still holds the
 * program the game destroyed, and the live-context poll inside make_pipeline no
 * longer covers it. Binding must degrade to a pipeline-less cmd, not trap. */
void test_sprite_renderer_set_material_survives_a_destroyed_program(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    nt_material_t mat = create_test_material();
    const nt_program_t dead = nt_material_get_info(mat)->program;

    nt_sprite_renderer_restore_gpu();     /* drops the cache, as recovery does */
    nt_gfx_destroy_program(dead);         /* game destroys its program */
    nt_sprite_renderer_set_material(mat); /* material still names it */

    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_cmd_count());
}

void test_sprite_renderer_capacity_flush_keeps_program_until_explicit_setter(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    desc.max_vertices = 16;
    desc.max_indices = 24;
    desc.custom_max_vertices = 16;
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));
    s_atlas_res = register_test_atlas(0xC8ULL);
    nt_material_t mat = create_test_material();
    nt_material_t other = create_test_material();
    nt_program_t program_a = nt_material_get_info(mat)->program;
    nt_program_t program_b = nt_material_get_info(other)->program;
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_flush();
    nt_sprite_renderer_set_material(other);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_flush();
    nt_gfx_test_draw_trace_reset(true);

    nt_sprite_renderer_set_material(mat);
    for (uint32_t i = 0; i < 4; i++) {
        nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    }
    nt_material_set_program(mat, program_b);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_set_material(mat);
    nt_gfx_fake_reset();
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_flush();

    /* One flush, one cmd, one material: one texture bind, no sampler int. */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_FALSE(nt_gfx_test_draw_trace_overflowed());
    nt_gfx_test_draw_t first = nt_gfx_test_draw_trace_at(0);
    nt_gfx_test_draw_t second = nt_gfx_test_draw_trace_at(1);
    nt_gfx_test_draw_t third = nt_gfx_test_draw_trace_at(2);
    TEST_ASSERT_EQUAL_UINT32(program_a.id, first.program.id);
    TEST_ASSERT_EQUAL_UINT32(program_a.id, second.program.id);
    TEST_ASSERT_EQUAL_UINT32(program_b.id, third.program.id);
    TEST_ASSERT_EQUAL_UINT32(first.pipeline.id, second.pipeline.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(second.pipeline.id, third.pipeline.id);
    for (uint32_t i = 0; i < 3; i++) {
        nt_gfx_test_draw_t draw = nt_gfx_test_draw_trace_at(i);
        TEST_ASSERT_EQUAL_UINT32(i == 0 ? 24 : 6, draw.num_indices);
        TEST_ASSERT_EQUAL_UINT32(i == 0 ? 16 : 4, draw.num_vertices);
    }
}

/* Queued work outlives the program it was built on when the owner destroys it
 * mid-frame. Flush drops those cmds: binding a destroyed pipeline leaves nothing
 * bound, and the draw would then trap pointing at the wrong cause. */
void test_sprite_renderer_flush_drops_cmds_whose_program_died(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xC7ULL);
    nt_material_t mat = create_test_material();
    const nt_program_t dead = nt_material_get_info(mat)->program;

    /* Immediate mode: draw_list flushes on exit, so only this path can leave a
     * cmd queued across the destroy. */
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0.0F, 0.0F, 0xFFFFFFFFU, 0);
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_cmd_count());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_count());

    nt_program_t replacement = nt_material_get_info(create_test_material())->program;
    nt_material_set_program(mat, replacement);
    nt_gfx_test_draw_trace_reset(true);
    nt_gfx_destroy_program(dead); /* takes the queued cmd's pipeline with it */
    nt_sprite_renderer_flush();

    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_draw_trace_count());
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(replacement.id, nt_gfx_test_draw_trace_at(0).program.id);
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_test_draw_trace_at(0).num_indices);
    TEST_ASSERT_FALSE(nt_gfx_test_draw_trace_overflowed());
}

void test_sprite_renderer_forwards_material_blend_state(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.constant_color[1] = 0.5F;
    blend.src_rgb = NT_BLEND_CONSTANT_COLOR;
    blend.dst_rgb = NT_BLEND_ONE_MINUS_DST_COLOR;
    blend.src_alpha = NT_BLEND_SRC_ALPHA_SATURATE;
    blend.dst_alpha = NT_BLEND_ONE_MINUS_DST_ALPHA;
    blend.op_rgb = NT_BLEND_OP_SUBTRACT;
    blend.op_alpha = NT_BLEND_OP_MAX;
    s_atlas_res = register_test_atlas(0xB1ULL);
    nt_material_t mat = create_test_material_with_blend(blend);
    nt_entity_t entity = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_render_item_t item = {.entity = entity.id, .batch_key = sprite_batch_key(entity, mat)};
    nt_sprite_renderer_init(&(nt_sprite_renderer_desc_t){.max_pipelines = 4});

    nt_sprite_renderer_draw_list(&item, 1);

    nt_blend_state_t actual = nt_gfx_fake_last_pipeline_blend();
    TEST_ASSERT_EQUAL_MEMORY(&blend, &actual, sizeof(blend));
}

/* ---- Test: batch grouping by batch_key ----
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

    uint32_t bk_a = sprite_batch_key(e0, mat_a);
    uint32_t bk_b = sprite_batch_key(e2, mat_b);
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

/* ---- Defensive test: actual atlas page splits a malformed batch_key run ----
 *
 * This deliberately violates the caller contract by reusing page 0's key for a
 * page 1 sprite. The renderer still splits commands to avoid a wrong texture. */
void test_sprite_renderer_splits_run_on_actual_page_change(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA7ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e0 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_entity_t e1 = create_sprite_entity(s_atlas_res, FIXTURE_R1_HASH, mat);

    uint32_t coarse_key = sprite_batch_key(e0, mat);
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

/* One material spanning two atlas pages: the page changes per cmd, the program
 * state (sampler unit + params) does not. */
void test_sprite_renderer_same_material_two_pages_state(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xE1ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e0 = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_entity_t e1 = create_sprite_entity(s_atlas_res, FIXTURE_R1_HASH, mat);

    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = sprite_batch_key(e0, mat);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = sprite_batch_key(e1, mat);

    nt_gfx_fake_reset();
    nt_sprite_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_draw_call_count());
    /* Two pages => two texture binds on the program's u_texture unit; no sampler int. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_slot_at(0));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_slot_at(1));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_pipeline_count());
}

/* A material that declares no textures never takes the page, so crossing pages
 * must not split the run — contrast the two-page test above, which draws twice. */
void test_sprite_renderer_textureless_material_ignores_page_change(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xE2ULL);
    nt_material_t mat = create_test_material_textureless();
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    nt_gfx_fake_reset();
    nt_gfx_test_draw_trace_reset(true);
    nt_sprite_renderer_set_material(mat);
    /* Region 0 lives on page 0, region 1 on page 1. */
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_emit_region(s_atlas_res, 1, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_flush();

    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    nt_gfx_test_draw_trace_reset(false);
}

/* An analytic-coverage material takes no page, so an unresolved page must not
 * hold its emit back. */
void test_sprite_renderer_textureless_material_emits_without_page(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xE3ULL);
    /* Unpublish the page texture: the atlas stays ready, its page does not resolve. */
    nt_resource_unregister(nt_hash32_str("sprite_renderer_pages"), (nt_hash64_t){FIXTURE_PAGE0_RID});
    nt_resource_step();
    nt_resource_t page0 = nt_atlas_get_page_resource(s_atlas_res, 0);
    TEST_ASSERT_EQUAL_UINT32(0, nt_resource_get(page0));

    nt_material_t mat = create_test_material_textureless();
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    nt_gfx_fake_reset();
    nt_gfx_test_draw_trace_reset(true);
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_flush();

    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
    nt_gfx_test_draw_trace_reset(false);
}

/* The cmd captured its sampler names and the pipeline carries the program, so the
 * texture still lands on the right unit after the material died; params come from
 * the material and are simply dropped. */
void test_sprite_renderer_dead_material_cmd_binds_on_program_unit(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xE4ULL);
    nt_material_t mat = create_test_material_with_param();
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    /* Control: while the material lives, its one param goes out with the texture bind. */
    nt_gfx_fake_reset();
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());

    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);

    nt_material_destroy(mat);

    nt_gfx_fake_reset();
    nt_gfx_test_draw_trace_reset(true);
    nt_sprite_renderer_flush();

    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_slot_at(0));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_vec4_count());
    nt_gfx_test_draw_trace_reset(false);
}

/* The page is the material's slot 0, never the program's unit 0: a program that names
 * another sampler first must still read the page from the unit it gave "u_texture". */
void test_sprite_renderer_page_lands_on_its_program_unit(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xEAULL);

    nt_material_create_desc_t mdesc;
    memset(&mdesc, 0, sizeof(mdesc));
    mdesc.program = nt_gfx_fake_make_program((const char *const[]){"u_other", "u_texture"}, 2);
    mdesc.cull_mode = NT_CULL_NONE;
    mdesc.textures[0].name = "u_texture";
    mdesc.textures[0].resource = NT_RESOURCE_INVALID; /* the page substitutes into slot 0 */
    /* A distinct page makes swapped or duplicated bindings observable. */
    mdesc.textures[1].name = "u_other";
    mdesc.textures[1].resource = nt_atlas_get_page_resource(s_atlas_res, 1);
    mdesc.texture_count = 2;
    mdesc.label = "test_sprite_material_page_on_unit_1";
    nt_material_t mat = nt_material_create(&mdesc);

    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nt_gfx_fake_reset();
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_flush();

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_slot_at(0));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_slot_at(1));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(nt_atlas_get_page_resource(s_atlas_res, 1))}), nt_gfx_fake_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(nt_atlas_get_page_resource(s_atlas_res, 0))}), nt_gfx_fake_bound_texture_at(1));
}

/* A program replaced between an immediate emit and an ECS draw_list puts one
 * material id on two programs in one flush; the uniforms must go out twice. */
void test_sprite_renderer_program_replace_between_immediate_and_draw_list(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xE5ULL);
    nt_material_t mat = create_test_material_with_param();
    const nt_program_t program_a = nt_material_get_info(mat)->program;
    const nt_program_t program_b = nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1);

    nt_entity_t e = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_render_item_t item = {.entity = e.id, .batch_key = sprite_batch_key(e, mat)};
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    nt_gfx_fake_reset();
    nt_gfx_test_draw_trace_reset(true);
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_material_set_program(mat, program_b);
    /* draw_list opens its cmds on the new pipeline without flushing the pending one. */
    nt_sprite_renderer_draw_list(&item, 1);

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(program_a.id, nt_gfx_test_draw_trace_at(0).program.id);
    TEST_ASSERT_EQUAL_UINT32(program_b.id, nt_gfx_test_draw_trace_at(1).program.id);
    /* One bind per cmd per sampled slot; the backend GL cache drops the second one.
     * Params are program state and go out twice. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_uniform_vec4_count());
    nt_gfx_test_draw_trace_reset(false);
}

/* A sampler override picks filtering for a texture that still has to exist, so
 * an unresolved slot 1 is a bug even with one set. */
void test_sprite_renderer_flush_asserts_on_unresolved_slot_with_override(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xE6ULL);
    nt_sampler_t override = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
    });
    TEST_ASSERT_TRUE(override.id != 0);

    nt_material_create_desc_t mdesc;
    memset(&mdesc, 0, sizeof(mdesc));
    mdesc.program = nt_gfx_fake_make_program((const char *const[]){"u_texture", "u_second"}, 2);
    mdesc.cull_mode = NT_CULL_NONE;
    mdesc.textures[0].name = "u_texture";
    mdesc.textures[1].name = "u_second";
    mdesc.textures[1].resource = NT_RESOURCE_INVALID;
    mdesc.textures[1].sampler = override;
    mdesc.texture_count = 2;
    mdesc.label = "test_sprite_material_two_slots";
    nt_material_t mat = nt_material_create(&mdesc);

    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    NT_TEST_EXPECT_ASSERT(nt_sprite_renderer_flush());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "texture_pool"));
}

/* Flush resolves units from the pipeline's program, so a material that leaves one of
 * that program's samplers undeclared would sample whatever the last cmd left there. */
void test_sprite_renderer_material_missing_a_program_sampler_asserts(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xE7ULL);
    nt_material_create_desc_t mdesc;
    memset(&mdesc, 0, sizeof(mdesc));
    mdesc.program = nt_gfx_fake_make_program((const char *const[]){"u_texture", "u_second"}, 2);
    mdesc.cull_mode = NT_CULL_NONE;
    mdesc.textures[0].name = "u_texture";
    mdesc.texture_count = 1;
    mdesc.label = "test_sprite_material_missing_sampler";
    nt_material_t mat = nt_material_create(&mdesc);

    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    NT_TEST_EXPECT_ASSERT(nt_sprite_renderer_flush());
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "coverage is incomplete"));
}

/* A declared name the program never samples maps to no unit: the slot is skipped and
 * the program's (empty) interface still counts as covered. */
void test_sprite_renderer_unknown_sampler_name_is_ignored(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xE8ULL);
    nt_material_create_desc_t mdesc;
    memset(&mdesc, 0, sizeof(mdesc));
    mdesc.program = nt_gfx_fake_make_program(NULL, 0);
    mdesc.cull_mode = NT_CULL_NONE;
    mdesc.textures[0].name = "u_texture";
    mdesc.texture_count = 1;
    mdesc.label = "test_sprite_material_unknown_sampler";
    nt_material_t mat = nt_material_create(&mdesc);

    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nt_gfx_fake_reset();
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, identity, 0, 0, 0xFFFFFFFFU, 0);
    nt_sprite_renderer_flush();

    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
}

/* ---- Test: polygon emit ----
 *
 * A region with vertex_count=6 / index_count=12 produces 6 vertices in
 * staging (uniform rect/polygon path). The last_emit_* counters
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
    items[0].batch_key = sprite_batch_key(e, mat);

    nt_sprite_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(6, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(12, nt_sprite_renderer_test_last_emit_index_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_draw_call_count());
}

/* ==== radial custom per-vertex attribute capability ==== */

/* A material declaring attr_map_count>0 builds an EXTENDED layout:
 * the verbatim 20B base (pos@0/tex@12/color@16) PLUS the declared custom attr
 * appended at offset 20, with its GL location pulled from attr_map (NOT
 * hardcoded). A plain material keeps the verbatim 20B base. */
void test_sprite_renderer_extended_layout_from_attr_map(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA6ULL);

    /* Plain material — base 20B layout, 3 attrs. */
    nt_material_t mat_base = create_radial_test_material(NULL, 0);
    nt_sprite_renderer_set_material(mat_base);
    nt_sprite_layout_info_t base_layout;
    nt_sprite_renderer_test_layout(mat_base, &base_layout);
    TEST_ASSERT_EQUAL_UINT32(20, base_layout.stride);
    TEST_ASSERT_EQUAL_UINT32(3, base_layout.attr_count);

    /* Custom-attr material — extended layout: base 3 attrs + a_radial @ loc 4. */
    nt_material_t mat_radial = create_radial_test_material("a_radial", 4);
    nt_sprite_renderer_set_material(mat_radial);
    nt_sprite_layout_info_t ext_layout;
    nt_sprite_renderer_test_layout(mat_radial, &ext_layout);

    TEST_ASSERT_EQUAL_UINT32(4, ext_layout.attr_count);
    TEST_ASSERT_EQUAL_UINT32(36, ext_layout.stride);
    /* Base attrs unchanged: position @0, texcoord @12, color @16. */
    TEST_ASSERT_EQUAL_UINT32(0, ext_layout.offsets[0]);
    TEST_ASSERT_EQUAL_UINT32(12, ext_layout.offsets[1]);
    TEST_ASSERT_EQUAL_UINT32(16, ext_layout.offsets[2]);
    /* Custom attr appended at offset 20, location from attr_map (==4). */
    TEST_ASSERT_EQUAL_UINT32(20, ext_layout.offsets[3]);
    TEST_ASSERT_EQUAL_UINT32(4, ext_layout.locations[3]);
}

/* Equal program/state share a pipeline; base and extended layouts use
 * separate vertex inputs. */
void test_sprite_renderer_layout_splits_vertex_inputs_not_pipelines(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA7ULL);

    nt_material_t mat_base = create_radial_test_material(NULL, 0);
    nt_material_t mat_radial = create_radial_test_material("a_radial", 4);

    nt_sprite_renderer_set_material(mat_base);
    nt_sprite_renderer_set_material(mat_radial);

    /* Same vs/fs/state: one pipeline; different layout: two vertex inputs. */
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_vertex_input_cache_count());
}

/* The vertex-input key packs every attr_map location: one location step on the
 * same program and state is a second vertex input, still one pipeline. */
void test_sprite_renderer_attr_map_location_step_splits_vertex_inputs(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));
    s_atlas_res = register_test_atlas(0xA7ULL);

    nt_material_t mat_loc4 = create_radial_test_material("a_radial", 4);
    nt_material_t mat_loc5 = create_radial_test_material("a_radial", 5);

    nt_sprite_renderer_set_material(mat_loc4);
    nt_sprite_renderer_set_material(mat_loc5);

    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_vertex_input_cache_count());
}

void test_sprite_renderer_retries_vertex_input_after_backend_failure(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));
    s_atlas_res = register_test_atlas(0xA7ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_render_item_t item = {.entity = e.id, .batch_key = sprite_batch_key(e, mat)};

    nt_gfx_fake_reset();
    nt_gfx_fake_fail_next_vertex_input_create();
    nt_sprite_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_input_cache_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_vertex_input_create_count());

    nt_sprite_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_vertex_input_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_vertex_input_create_count());

    nt_sprite_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_vertex_input_create_count());
}

/* The custom-attr emit path bakes the per-widget float block into
 * EVERY vertex it emits (like color), into a separate byte-staging buffer at
 * the extended stride — read back via the radial test accessor. */
void test_sprite_renderer_custom_attr_emit_bakes_per_vertex(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA8ULL);
    nt_material_t mat_radial = create_radial_test_material("a_radial", 4);

    nt_sprite_renderer_set_material(mat_radial);

    const float radial[4] = {1.5F, 2.25F, 0.5F, 1.75F};
    nt_sprite_renderer_set_custom_attrs(radial, (uint8_t)sizeof(radial));

    /* Emit a quad against the white/rect region — 4 verts, 6 indices. */
    const float positions[4][2] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    uint32_t region_index = nt_atlas_find_region(s_atlas_res, FIXTURE_R0_HASH);
    nt_sprite_renderer_emit_geometry(s_atlas_res, region_index, positions, 4, idx, 6, NT_MATH_MAT4_IDENTITY, 0xFFFFFFFFU);

    TEST_ASSERT_EQUAL_UINT32(4, nt_sprite_renderer_test_last_emit_vertex_count());
    /* Unity is built with UNITY_EXCLUDE_FLOAT — these are exact bit-copies of
     * the input block, so an exact float compare via fabsf tolerance is fine. */
    for (uint32_t v = 0; v < 4; v++) {
        float out[4] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 4);
        for (uint8_t c = 0; c < 4; c++) {
            TEST_ASSERT_TRUE_MESSAGE(fabsf(out[c] - radial[c]) < 1e-6F, "radial attr not baked per-vertex");
        }
    }
}

/* ---- Test: FLIP_X / FLIP_Y mirror around the region pivot ---- */

static void assert_pos_close(float ex, float ey, const float pos[3], const char *msg) {
    if (fabsf(pos[0] - ex) > 1e-4F || fabsf(pos[1] - ey) > 1e-4F) {
        char buf[160];
        (void)snprintf(buf, sizeof(buf), "%s (expected=(%g,%g) actual=(%g,%g))", msg, (double)ex, (double)ey, (double)pos[0], (double)pos[1]);
        TEST_FAIL_MESSAGE(buf);
    }
}

void test_sprite_renderer_flip_mirrors_around_pivot(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xF1ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = sprite_batch_key(e, mat);

    /* Region r0: 64x64 source, origin (0.5, 0.5) → pivot at source (32, 32).
     * 4 verts at source-space (i*10, i*20) for i=0..3 = (0,0),(10,20),(20,40),(30,60).
     * ipu = 1.0 (no pixels_per_unit metadata in fixture).
     * Identity transform at entity world (0,0).
     * Expected world position (no flip) = source_xy - pivot.
     * FLIP_X negates pivot-relative x → world x = -(source_x - 32) = 32 - source_x.
     * FLIP_Y negates pivot-relative y. */

    float p[3];

    /* No flip baseline */
    nt_sprite_comp_set_flip(e, false, false);
    nt_sprite_renderer_draw_list(items, 1);
    nt_sprite_renderer_test_last_emit_position(0, p);
    assert_pos_close(-32.0F, -32.0F, p, "no-flip v0");
    nt_sprite_renderer_test_last_emit_position(3, p);
    assert_pos_close(-2.0F, 28.0F, p, "no-flip v3");

    /* FLIP_X — x mirrored around pivot, y unchanged */
    nt_sprite_comp_set_flip(e, true, false);
    nt_sprite_renderer_draw_list(items, 1);
    nt_sprite_renderer_test_last_emit_position(0, p);
    assert_pos_close(32.0F, -32.0F, p, "flip-x v0");
    nt_sprite_renderer_test_last_emit_position(3, p);
    assert_pos_close(2.0F, 28.0F, p, "flip-x v3");

    /* FLIP_Y — y mirrored around pivot, x unchanged */
    nt_sprite_comp_set_flip(e, false, true);
    nt_sprite_renderer_draw_list(items, 1);
    nt_sprite_renderer_test_last_emit_position(0, p);
    assert_pos_close(-32.0F, 32.0F, p, "flip-y v0");
    nt_sprite_renderer_test_last_emit_position(3, p);
    assert_pos_close(-2.0F, -28.0F, p, "flip-y v3");

    /* Both flips — position negated relative to no-flip */
    nt_sprite_comp_set_flip(e, true, true);
    nt_sprite_renderer_draw_list(items, 1);
    nt_sprite_renderer_test_last_emit_position(0, p);
    assert_pos_close(32.0F, 32.0F, p, "flip-both v0");
    nt_sprite_renderer_test_last_emit_position(3, p);
    assert_pos_close(2.0F, -28.0F, p, "flip-both v3");
}

/* ---- Test: restore_gpu clears both caches before recreating buffers ---- */

void test_sprite_renderer_restore_gpu_cycle(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA5ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = sprite_batch_key(e, mat);

    nt_gfx_fake_reset();
    nt_sprite_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_vertex_input_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_vertex_input_create_count());

    nt_sprite_renderer_restore_gpu();
    TEST_ASSERT_TRUE(nt_sprite_renderer_test_initialized());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_pipeline_cache_count());

    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_input_cache_count());
    nt_sprite_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_vertex_input_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_vertex_input_create_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_draw_call_count());
}

void test_sprite_renderer_restore_retries_after_context_loss(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    desc.max_vertices = 16;
    desc.max_indices = 24;
    desc.custom_max_vertices = 16;
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));
    s_atlas_res = register_test_atlas(0xC9ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t entity = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_render_item_t item = {.entity = entity.id, .batch_key = sprite_batch_key(entity, mat)};
    nt_sprite_renderer_set_material(mat);
    nt_sprite_renderer_emit_region(s_atlas_res, 0, NT_MATH_MAT4_IDENTITY, 0, 0, 0xFFFFFFFFU, 0);
    TEST_ASSERT_EQUAL_UINT32(4, nt_sprite_renderer_test_vertex_count());

    nt_gfx_fake_set_context_lost(true);
    nt_result_t result = nt_sprite_renderer_restore_gpu();
    nt_gfx_fake_set_context_lost(false);

    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, result);
    TEST_ASSERT_TRUE(nt_sprite_renderer_test_initialized());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_cmd_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_input_cache_count());

    NT_TEST_EXPECT_ASSERT(nt_sprite_renderer_set_material(mat));
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_restore_gpu());
    nt_render_item_t items[5] = {item, item, item, item, item};
    nt_sprite_renderer_draw_list(items, 5);
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_cmd_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_count());
}

void test_sprite_renderer_restore_on_inactive_renderer_does_nothing(void) {
    TEST_ASSERT_FALSE(nt_sprite_renderer_test_initialized());
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_restore_gpu());
    TEST_ASSERT_FALSE(nt_sprite_renderer_test_initialized());
}

void test_sprite_renderer_restore_cleans_up_after_index_buffer_failure(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));
    s_atlas_res = register_test_atlas(0xCAULL);
    nt_material_t mat = create_test_material();
    nt_entity_t entity = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat);
    nt_render_item_t item = {.entity = entity.id, .batch_key = sprite_batch_key(entity, mat)};
    nt_sprite_renderer_draw_list(&item, 1);

    nt_gfx_fake_fail_buffer_creates(2);
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_sprite_renderer_restore_gpu());
    TEST_ASSERT_TRUE(nt_sprite_renderer_test_initialized());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_cmd_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_input_cache_count());

    /* Every slot must be available before retry; no partial VBO may linger. */
    assert_all_buffer_slots_available();

    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_restore_gpu());
    nt_sprite_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_sprite_renderer_test_vertex_input_cache_count());
}

/* ---- Test: pipeline cache full → NT_ASSERT ----
 *
 * Death-test on cache overflow is not exercised at runtime to keep the test
 * binary non-aborting; instead we verify that we can fill the cache up to
 * capacity exactly. With desc.max_pipelines=2, two distinct materials populate
 * the cache to its declared capacity without firing the assert. A third
 * distinct material WOULD fire the assert (configuration bug). */
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
    items[0].batch_key = sprite_batch_key(e0, mat_a);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = sprite_batch_key(e1, mat_b);

    nt_sprite_renderer_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_pipeline_cache_count());
}

/* ---- Test: material sampler override does not stick across cmds ----
 *
 * Regression for the "sticky sampler" bug. Two materials, both rendering
 * the same atlas region (so the same page texture). Material A has a
 * sampler override (LINEAR). Material B has none — should fall back to
 * the texture's default sampler (NEAREST, set when page0 was created).
 * Order: [A_override, B_no_override]. After flush, the unit must hold
 * the texture default, not A's override. Without the bound_sampler_ids
 * delta-tracking in flush, B's cmd would skip bind_sampler entirely
 * (texture-bind cache hit), leaving A's override on the unit. */
static nt_material_t create_test_material_with_sampler(nt_sampler_t override) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = nt_gfx_fake_make_program((const char *const[]){"u_tex"}, 1);
    desc.cull_mode = NT_CULL_NONE;
    desc.texture_count = 1;
    desc.textures[0].name = "u_tex";
    desc.textures[0].resource = NT_RESOURCE_INVALID;
    desc.textures[0].sampler = override;
    desc.label = "test_mat_override";
    nt_material_t mat = nt_material_create(&desc);
    return mat;
}

void test_sprite_renderer_sampler_override_does_not_stick(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xA9ULL);

    /* Override sampler differs from page0's default (NEAREST/CLAMP from zero-init
     * desc in setUp). LINEAR + REPEAT guarantees a fresh dedup slot. */
    nt_sampler_t override = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
    });
    TEST_ASSERT_TRUE(override.id != 0);

    nt_material_t mat_override = create_test_material_with_sampler(override);
    nt_material_t mat_plain = create_test_material();

    nt_entity_t e_a = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_override);
    nt_entity_t e_b = create_sprite_entity(s_atlas_res, FIXTURE_R0_HASH, mat_plain);

    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e_a.id;
    items[0].batch_key = sprite_batch_key(e_a, mat_override);
    items[1].sort_key = 1;
    items[1].entity = e_b.id;
    items[1].batch_key = sprite_batch_key(e_b, mat_plain);

    nt_gfx_fake_reset();
    nt_sprite_renderer_draw_list(items, 2);

    /* Resolve the page texture's default sampler (what the second cmd should
     * leave bound). page0.id was registered under FIXTURE_PAGE0_RID; FIXTURE_R0
     * lives on page 0. */
    nt_resource_t page0_res = nt_atlas_get_page_resource(s_atlas_res, 0);
    nt_texture_t page0_tex = (nt_texture_t){.id = nt_resource_get(page0_res)};
    nt_sampler_t page0_default = nt_gfx_get_texture_default_sampler(page0_tex);

    /* Last sampler on slot 0 must equal page0's default, not the override
     * carried over from cmd 0. */
    uint32_t last = nt_gfx_fake_last_sampler(0);
    uint32_t default_backend = nt_gfx_test_sampler_backend_id(page0_default);
    uint32_t override_backend = nt_gfx_test_sampler_backend_id(override);
    TEST_ASSERT_TRUE(default_backend != 0 && override_backend != 0 && default_backend != override_backend);
    TEST_ASSERT_EQUAL_UINT32(default_backend, last);
}

/* ---- emit_slice9 NULL-src (atlas-baked borders) tests ---- */

/* Helper: resolve the rs9 region's runtime index in the registered atlas. */
static uint32_t find_rs9_region_index(nt_resource_t atlas) {
    const uint32_t count = nt_atlas_region_count(atlas);
    for (uint32_t i = 0; i < count; i++) {
        const nt_texture_region_t *r = nt_atlas_get_region(atlas, i);
        if (r->slice9_lrtb[0] == 16 && r->slice9_lrtb[1] == 16 && r->slice9_lrtb[2] == 16 && r->slice9_lrtb[3] == 16) {
            return i;
        }
    }
    TEST_FAIL_MESSAGE("rs9 region with slice9_lrtb=16/16/16/16 not found");
    return 0;
}

/* scale=1.0F → atlas borders unchanged → grid x_inner == x + 16; matches emit_slice9. */
void test_emit_slice9_null_src_scale_one_matches_atlas(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xB1ULL);
    nt_material_t mat = create_test_material();
    nt_sprite_renderer_set_material(mat);

    const uint32_t rs9 = find_rs9_region_index(s_atlas_res);
    const float x = 0.0F;
    const float y = 0.0F;
    const float w = 100.0F;
    const float h = 100.0F;
    nt_sprite_renderer_emit_slice9(s_atlas_res, rs9, x, y, w, h, NULL, 1.0F, 0xFFFFFFFFU, 0, NT_MATH_MAT4_IDENTITY);

    TEST_ASSERT_EQUAL_UINT32(16U, nt_sprite_renderer_test_last_slice9_vertex_count());
    /* Inner column 1 = x + (16 * 1.0F) = 16. */
    float v1[3];
    nt_sprite_renderer_test_last_emit_position(1, v1);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(v1[0] - 16.0F) < 0.5F, "scale=1.0 inner-left should be 16 px");
    /* Inner column 2 = x + w - (16 * 1.0F) = 84. */
    float v2[3];
    nt_sprite_renderer_test_last_emit_position(2, v2);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(v2[0] - 84.0F) < 0.5F, "scale=1.0 inner-right should be 84 px");
}

/* scale=2.0F → DST borders doubled (positions 32/68) BUT SRC borders unchanged
 * → UV cut stays at atlas src_l/source_w (scaling src too would sample edge content
 * into the corners). */
void test_emit_slice9_null_src_scale_two_doubles_borders(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xB2ULL);
    nt_material_t mat = create_test_material();
    nt_sprite_renderer_set_material(mat);

    const uint32_t rs9 = find_rs9_region_index(s_atlas_res);
    nt_sprite_renderer_emit_slice9(s_atlas_res, rs9, 0.0F, 0.0F, 100.0F, 100.0F, NULL, 2.0F, 0xFFFFFFFFU, 0, NT_MATH_MAT4_IDENTITY);

    TEST_ASSERT_EQUAL_UINT32(16U, nt_sprite_renderer_test_last_slice9_vertex_count());
    /* Positions reflect DST (= src*scale = 32). */
    float v1[3];
    nt_sprite_renderer_test_last_emit_position(1, v1);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(v1[0] - 32.0F) < 0.5F, "scale=2.0 inner-left should be 32 px");
    float v2[3];
    nt_sprite_renderer_test_last_emit_position(2, v2);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(v2[0] - 68.0F) < 0.5F, "scale=2.0 inner-right should be 68 px");
    /* UV reflects SRC (= atlas-baked 16). For the fixture: u_min=14000, u_range=3000,
     * src_l=16, source_w=100 → uv_col1 = 14000 + 16*3000/100 = 14480.
     * If a regression scales src too, this u shifts to 14960 (32 instead of 16). */
    uint16_t uv1[2];
    nt_sprite_renderer_test_last_emit_texcoord(1, uv1);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(14480U, uv1[0], "scale=2.0 UV column-1 must use SRC=atlas borders, NOT DST");
    uint16_t uv2[2];
    nt_sprite_renderer_test_last_emit_texcoord(2, uv2);
    /* uv_col2 = u_max - 16*u_range/100 = 17000 - 480 = 16520. */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(16520U, uv2[0], "scale=2.0 UV column-2 must use SRC=atlas borders");
}

/* ECS path: set_slice9_scale on sprite_comp must scale destination corner size
 * (via emit_one), keeping UV unchanged. Mirrors the from_region semantics. */
void test_sprite_comp_slice9_scale_affects_emit_position(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xC1ULL);
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_sprite_entity(s_atlas_res, FIXTURE_RS9_HASH, mat);
    nt_sprite_comp_set_slice9_scale(e, 2.0F);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = sprite_batch_key(e, mat);
    nt_sprite_renderer_draw_list(items, 1);

    /* rs9: 100×100 src, slice9=16, origin (0,0) → local lxs[1] = 16 × scale = 32. */
    float v1[3];
    nt_sprite_renderer_test_last_emit_position(1, v1);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(v1[0] - 32.0F) < 0.5F, "ECS slice9_scale=2 must shift inner-left to 32 px");
    /* UV stays at src=16. */
    uint16_t uv1[2];
    nt_sprite_renderer_test_last_emit_texcoord(1, uv1);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(14480U, uv1[0], "ECS slice9_scale must not shift UV");
}

/* Graceful degradation: when dst < border sum the corners must not overflow the
 * requested rect. Emit into a rect smaller than the borders on one then both axes
 * and assert all 16 grid vertices stay inside [x, x+w] x [y, y+h]. Mirrors Unity/
 * Defold behavior — geometry never exceeds the requested rect. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void assert_slice9_within_rect(float x, float y, float w, float h, const char *msg) {
    TEST_ASSERT_EQUAL_UINT32(16U, nt_sprite_renderer_test_last_slice9_vertex_count());
    const float eps = 0.5F;
    for (uint32_t v = 0; v < 16U; ++v) {
        float p[3];
        nt_sprite_renderer_test_last_emit_position(v, p);
        TEST_ASSERT_TRUE_MESSAGE(p[0] >= x - eps && p[0] <= x + w + eps, msg);
        TEST_ASSERT_TRUE_MESSAGE(p[1] >= y - eps && p[1] <= y + h + eps, msg);
    }
}

void test_emit_slice9_degrades_when_dst_smaller_than_borders(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    s_atlas_res = register_test_atlas(0xD9ULL);
    nt_material_t mat = create_test_material();
    nt_sprite_renderer_set_material(mat);

    const uint32_t rs9 = find_rs9_region_index(s_atlas_res); /* 16/16/16/16 borders */
    const float x = 100.0F;
    const float y = 200.0F;

    /* Horizontal squeeze: w=20 < (16+16)=32; inner columns must not cross. */
    nt_sprite_renderer_emit_slice9(s_atlas_res, rs9, x, y, 20.0F, 100.0F, NULL, 1.0F, 0xFFFFFFFFU, 0, NT_MATH_MAT4_IDENTITY);
    assert_slice9_within_rect(x, y, 20.0F, 100.0F, "narrow-w slice9 corners must stay within rect");
    /* Inner-left <= inner-right (no crossing). */
    float vl[3];
    float vr[3];
    nt_sprite_renderer_test_last_emit_position(1, vl);
    nt_sprite_renderer_test_last_emit_position(2, vr);
    TEST_ASSERT_TRUE_MESSAGE(vl[0] <= vr[0] + 0.5F, "narrow-w inner-left must not cross inner-right");

    /* Vertical squeeze: h=10 < 32. */
    nt_sprite_renderer_emit_slice9(s_atlas_res, rs9, x, y, 100.0F, 10.0F, NULL, 1.0F, 0xFFFFFFFFU, 0, NT_MATH_MAT4_IDENTITY);
    assert_slice9_within_rect(x, y, 100.0F, 10.0F, "short-h slice9 corners must stay within rect");

    /* Both axes squeezed: 12 x 8, both < 32. */
    nt_sprite_renderer_emit_slice9(s_atlas_res, rs9, x, y, 12.0F, 8.0F, NULL, 1.0F, 0xFFFFFFFFU, 0, NT_MATH_MAT4_IDENTITY);
    assert_slice9_within_rect(x, y, 12.0F, 8.0F, "tiny slice9 corners must stay within rect");
}

/* ---- main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sprite_renderer_init_shutdown);
    RUN_TEST(test_sprite_renderer_init_retries_after_buffer_creation_failure);
    RUN_TEST(test_sprite_renderer_vertex_size_assert);
    RUN_TEST(test_sprite_renderer_batch_key_packs_material_and_page_slots);
    RUN_TEST(test_sprite_renderer_batch_key_ignores_handle_generations);
    RUN_TEST(test_sprite_renderer_batch_key_distinguishes_page_slots);
    RUN_TEST(test_sprite_renderer_draw_list_null_items_asserts_when_nonempty);
    RUN_TEST(test_sprite_renderer_draw_list_asserts_on_unresolved_sprite_item);
    RUN_TEST(test_sprite_renderer_pipeline_cache);
    RUN_TEST(test_neighbouring_programs_one_depth_write_step_apart_get_their_own_pipelines);
    RUN_TEST(test_sprite_renderer_reset_drops_commands_and_pipelines);
    RUN_TEST(test_sprite_renderer_set_material_survives_a_destroyed_program);
    RUN_TEST(test_sprite_renderer_capacity_flush_keeps_program_until_explicit_setter);
    RUN_TEST(test_sprite_renderer_flush_drops_cmds_whose_program_died);
    RUN_TEST(test_sprite_renderer_forwards_material_blend_state);
    RUN_TEST(test_sprite_renderer_batch_grouping);
    RUN_TEST(test_sprite_renderer_splits_run_on_actual_page_change);
    RUN_TEST(test_sprite_renderer_same_material_two_pages_state);
    RUN_TEST(test_sprite_renderer_textureless_material_ignores_page_change);
    RUN_TEST(test_sprite_renderer_textureless_material_emits_without_page);
    RUN_TEST(test_sprite_renderer_dead_material_cmd_binds_on_program_unit);
    RUN_TEST(test_sprite_renderer_page_lands_on_its_program_unit);
    RUN_TEST(test_sprite_renderer_program_replace_between_immediate_and_draw_list);
    RUN_TEST(test_sprite_renderer_flush_asserts_on_unresolved_slot_with_override);
    RUN_TEST(test_sprite_renderer_material_missing_a_program_sampler_asserts);
    RUN_TEST(test_sprite_renderer_unknown_sampler_name_is_ignored);
    RUN_TEST(test_sprite_renderer_polygon_emit);
    RUN_TEST(test_sprite_renderer_extended_layout_from_attr_map);
    RUN_TEST(test_sprite_renderer_layout_splits_vertex_inputs_not_pipelines);
    RUN_TEST(test_sprite_renderer_attr_map_location_step_splits_vertex_inputs);
    RUN_TEST(test_sprite_renderer_retries_vertex_input_after_backend_failure);
    RUN_TEST(test_sprite_renderer_custom_attr_emit_bakes_per_vertex);
    RUN_TEST(test_sprite_renderer_flip_mirrors_around_pivot);
    RUN_TEST(test_sprite_renderer_restore_gpu_cycle);
    RUN_TEST(test_sprite_renderer_restore_retries_after_context_loss);
    RUN_TEST(test_sprite_renderer_restore_on_inactive_renderer_does_nothing);
    RUN_TEST(test_sprite_renderer_restore_cleans_up_after_index_buffer_failure);
    RUN_TEST(test_sprite_renderer_pipeline_cache_capacity);
    RUN_TEST(test_sprite_renderer_sampler_override_does_not_stick);
    RUN_TEST(test_emit_slice9_null_src_scale_one_matches_atlas);
    RUN_TEST(test_emit_slice9_null_src_scale_two_doubles_borders);
    RUN_TEST(test_emit_slice9_degrades_when_dst_smaller_than_borders);
    RUN_TEST(test_sprite_comp_slice9_scale_affects_emit_position);
    return UNITY_END();
}
