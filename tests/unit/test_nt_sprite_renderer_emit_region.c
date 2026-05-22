/* Mirrors test_sprite_renderer.c setup -- full resource system + real
 * atlas + real material via nt_material_create -- because emit_region
 * asserts nt_resource_is_ready(atlas). */

/* System headers before Unity -- avoids __declspec(noreturn) clash on MSVC. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
/* NT_TEST_ACCESS / NT_TEST_ACCESS provided via CMake */
#include "atlas/nt_atlas.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "nt_atlas_format.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "resource/nt_resource.h"

#include "unity.h"
/* clang-format on */

/* ---- Atlas fixture: 1 white 4-vert quad + 1 polygon 6-vert ---- */

#define FIXTURE_WHITE_HASH 0xA00ULL   /* white, 4 verts, 6 indices */
#define FIXTURE_POLYGON_HASH 0xB00ULL /* polygon, 6 verts, 12 indices */
#define FIXTURE_PAGE0_RID 0x7100ULL

#define FIXTURE_WHITE_REGION_IDX 0u
#define FIXTURE_POLYGON_REGION_IDX 1u

typedef struct {
    const NtAtlasRegion *regions;
    uint16_t region_count;
    const NtAtlasVertex *vertices;
    uint32_t total_vertex_count;
    const uint16_t *indices;
    uint32_t total_index_count;
    const uint64_t *page_ids;
    uint16_t page_count;
} atlas_blob_spec_t;

static uint32_t build_atlas_blob(uint8_t *out, uint32_t cap, const atlas_blob_spec_t *spec) {
    const uint32_t page_bytes = (uint32_t)spec->page_count * (uint32_t)sizeof(uint64_t);
    const uint32_t region_bytes = (uint32_t)spec->region_count * (uint32_t)sizeof(NtAtlasRegion);
    const uint32_t vertex_bytes = spec->total_vertex_count * (uint32_t)sizeof(NtAtlasVertex);
    const uint32_t index_bytes = spec->total_index_count * (uint32_t)sizeof(uint16_t);
    const uint32_t vertex_offset = (uint32_t)sizeof(NtAtlasHeader) + page_bytes + region_bytes;
    const uint32_t index_offset = vertex_offset + vertex_bytes;
    const uint32_t total = index_offset + index_bytes;

    TEST_ASSERT_MESSAGE(total <= cap, "atlas blob buffer too small");
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

static uint32_t build_test_atlas(uint8_t *atlas_blob, uint32_t cap) {
    /* Layout: [white verts: 4] [poly verts: 6] = 10 verts
     *         [white idx: 6] [poly idx: 12] = 18 indices */
    NtAtlasVertex verts[10];
    uint16_t indices[18];
    for (uint16_t i = 0; i < 10; i++) {
        verts[i].local_x = (int16_t)(i * 10);
        verts[i].local_y = (int16_t)(i * 20);
        verts[i].atlas_u = (uint16_t)(i * 1000);
        verts[i].atlas_v = (uint16_t)(i * 2000);
    }
    /* white quad: 0,1,2 / 0,2,3 */
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    indices[3] = 0;
    indices[4] = 2;
    indices[5] = 3;
    /* polygon fan: 0,1,2 / 0,2,3 / 0,3,4 / 0,4,5 */
    indices[6] = 0;
    indices[7] = 1;
    indices[8] = 2;
    indices[9] = 0;
    indices[10] = 2;
    indices[11] = 3;
    indices[12] = 0;
    indices[13] = 3;
    indices[14] = 4;
    indices[15] = 0;
    indices[16] = 4;
    indices[17] = 5;

    NtAtlasRegion regions[2];
    memset(regions, 0, sizeof(regions));
    regions[0].name_hash = FIXTURE_WHITE_HASH;
    regions[0].source_w = 1;
    regions[0].source_h = 1;
    regions[0].origin_x = 0.0F;
    regions[0].origin_y = 0.0F;
    regions[0].vertex_start = 0;
    regions[0].index_start = 0;
    regions[0].vertex_count = 4;
    regions[0].index_count = 6;
    regions[0].page_index = 0;
    regions[0].transform = 0;
    regions[0].flags = NT_ATLAS_REGION_FLAG_QUAD_012023;

    regions[1].name_hash = FIXTURE_POLYGON_HASH;
    regions[1].source_w = 16;
    regions[1].source_h = 16;
    regions[1].origin_x = 0.5F;
    regions[1].origin_y = 0.5F;
    regions[1].vertex_start = 4;
    regions[1].index_start = 6;
    regions[1].vertex_count = 6;
    regions[1].index_count = 12;
    regions[1].page_index = 0;
    regions[1].transform = 0;

    uint64_t page_ids[1] = {FIXTURE_PAGE0_RID};
    atlas_blob_spec_t spec = {
        .regions = regions,
        .region_count = 2,
        .vertices = verts,
        .total_vertex_count = 10,
        .indices = indices,
        .total_index_count = 18,
        .page_ids = page_ids,
        .page_count = 1,
    };
    return build_atlas_blob(atlas_blob, cap, &spec);
}

static uint8_t *build_pack_blob(uint64_t atlas_rid, const uint8_t *atlas_blob, uint32_t atlas_blob_size, uint32_t *out_total) {
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

#define MAX_PACK_BLOBS 4
static uint8_t *s_pack_blobs[MAX_PACK_BLOBS];
static uint8_t s_pack_blob_count;
static nt_resource_t s_atlas_res;
static uint32_t s_vpack_counter;
static const uint8_t s_white_pixel[4] = {255, 255, 255, 255};

static nt_resource_t register_test_atlas(uint64_t atlas_rid) {
    uint8_t atlas_blob[1024];
    uint32_t atlas_blob_size = build_test_atlas(atlas_blob, sizeof(atlas_blob));

    uint32_t pack_total = 0;
    uint8_t *pack_blob = build_pack_blob(atlas_rid, atlas_blob, atlas_blob_size, &pack_total);
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
     * publishes those newly requested slots before tests draw. */
    nt_resource_step();
    TEST_ASSERT_TRUE(nt_resource_is_ready(atlas));
    return atlas;
}

static nt_material_t create_test_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "sprite_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "sprite_fs"});

    char vs_name[64];
    char fs_name[64];
    char pack_name[64];
    (void)snprintf(vs_name, sizeof(vs_name), "test_er_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof(fs_name), "test_er_fs_%u", s_vpack_counter);
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
    desc.label = "test_emit_region_material";

    nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
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

    /* Pre-register a page-0 texture in a high-priority pack so the atlas
     * post-resolve hooks up a real page texture handle (sprite renderer
     * needs nt_resource_get(page) != 0 to write into a cmd). */
    nt_hash32_t page_pid = nt_hash32_str("emit_region_pages");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(page_pid, 100));
    nt_texture_t page0 = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = s_white_pixel, .label = "page0"});
    TEST_ASSERT_TRUE(page0.id != 0);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(page_pid, (nt_hash64_t){FIXTURE_PAGE0_RID}, NT_ASSET_TEXTURE, page0.id));

    nt_material_init(&(nt_material_desc_t){.max_materials = 32});

    /* Begin frame/pass so flush's draw_indexed doesn't trip the gfx-stub
     * assert (mirrors test_sprite_renderer setUp). */
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

/* ---- Test 1: direct call writes vertex_count verts ---- */

static void test_emit_region_direct_call(void) {
    nt_sprite_renderer_desc_t rd = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&rd));

    s_atlas_res = register_test_atlas(0xA1ULL);
    nt_material_t mat = create_test_material();

    nt_sprite_renderer_set_material(mat);

    /* Identity scale 32 / translate (10, 20) screen mat4, row-major.
     * mat4 layout used by the renderer: column-major in memory, columns 0..3.
     * emit_region_resolved reads m[0/4/12] (col0) / m[1/5/13] / m[2/6/14] /
     * — same convention as transform_comp.world_matrices. We pass the same
     * shape: m[0]=scale_x, m[5]=scale_y, m[12]=tx, m[13]=ty, m[15]=1. */
    const float m[16] = {
        32.0F, 0.0F, 0.0F, 0.0F, 0.0F, 32.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 10.0F, 20.0F, 0.0F, 1.0F,
    };
    nt_sprite_renderer_emit_region(s_atlas_res, FIXTURE_WHITE_REGION_IDX, m, 0.0F, 0.0F, 0xFFFFFFFFU, /*flip_bits=*/0U);

    /* Probe captured BEFORE flush resets vertex_count. */
    TEST_ASSERT_EQUAL_UINT32(4, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(6, nt_sprite_renderer_test_last_emit_index_count());

    nt_sprite_renderer_flush();
}

/* ---- Test 2: capacity guard auto-flush + reopen ---- */

static void test_emit_region_capacity_guard(void) {
    nt_sprite_renderer_desc_t rd = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&rd));

    s_atlas_res = register_test_atlas(0xA2ULL);
    nt_material_t mat = create_test_material();
    nt_sprite_renderer_set_material(mat);

    /* Identity mat4 (col0=1, col1=1, w=1). */
    const float m[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };

    /* Emit enough quads to overflow staging at least once. 16384/4=4096
     * quads fit before the capacity guard trips. Emit 4097 to force exactly
     * one auto-flush+reopen. */
    const uint32_t quad_capacity = NT_SPRITE_RENDERER_MAX_VERTICES / 4U;
    const uint32_t emit_count = quad_capacity + 2U;
    for (uint32_t i = 0; i < emit_count; ++i) {
        nt_sprite_renderer_emit_region(s_atlas_res, FIXTURE_WHITE_REGION_IDX, m, 0.0F, 0.0F, 0xFFFFFFFFU, 0U);
    }
    /* Final explicit flush so the per-renderer counter captures the trailing chunk. */
    nt_sprite_renderer_flush();

    /* At least 2 draw calls must have happened — one from the capacity-
     * triggered auto-flush mid-loop + one from the final flush. */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2U, nt_sprite_renderer_test_draw_call_count());
}

/* ---- Test 3: polygon-hull vertex_count preserved ---- */

static void test_emit_region_polygon_hull_vertex_count_preserved(void) {
    nt_sprite_renderer_desc_t rd = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&rd));

    s_atlas_res = register_test_atlas(0xA3ULL);
    nt_material_t mat = create_test_material();
    nt_sprite_renderer_set_material(mat);

    const float m[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    nt_sprite_renderer_emit_region(s_atlas_res, FIXTURE_POLYGON_REGION_IDX, m, 0.5F, 0.5F, 0xFFFFFFFFU, 0U);

    TEST_ASSERT_EQUAL_UINT32(6, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(12, nt_sprite_renderer_test_last_emit_index_count());

    nt_sprite_renderer_flush();
}

/* ---- Test 4: set_material auto-flush on change ---- */

static void test_set_material_auto_flush_on_change(void) {
    nt_sprite_renderer_desc_t rd = nt_sprite_renderer_desc_defaults();
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&rd));

    s_atlas_res = register_test_atlas(0xA4ULL);
    nt_material_t mat_a = create_test_material();
    nt_material_t mat_b = create_test_material();
    TEST_ASSERT_NOT_EQUAL(mat_a.id, mat_b.id);

    const float m[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };

    nt_sprite_renderer_set_material(mat_a);
    nt_sprite_renderer_emit_region(s_atlas_res, FIXTURE_WHITE_REGION_IDX, m, 0.0F, 0.0F, 0xFFFFFFFFU, 0U);

    /* Same handle re-binding does NOT flush (current_mat still .id of mat_a,
     * cmd_count > 0 after the emit above so the no-op branch fires). */
    const uint32_t calls_before_same = nt_sprite_renderer_test_draw_call_count();
    nt_sprite_renderer_set_material(mat_a);
    nt_sprite_renderer_emit_region(s_atlas_res, FIXTURE_WHITE_REGION_IDX, m, 0.0F, 0.0F, 0xFFFFFFFFU, 0U);
    TEST_ASSERT_EQUAL_UINT32(calls_before_same, nt_sprite_renderer_test_draw_call_count());

    /* Different .id triggers auto-flush (one extra draw call recorded). */
    nt_sprite_renderer_set_material(mat_b);
    const uint32_t calls_after_change = nt_sprite_renderer_test_draw_call_count();
    TEST_ASSERT_EQUAL_UINT32(calls_before_same + 1U, calls_after_change);

    /* Subsequent emit on mat_b still works (cmd reopened). */
    nt_sprite_renderer_emit_region(s_atlas_res, FIXTURE_WHITE_REGION_IDX, m, 0.0F, 0.0F, 0xFFFFFFFFU, 0U);
    TEST_ASSERT_EQUAL_UINT32(4, nt_sprite_renderer_test_last_emit_vertex_count());

    nt_sprite_renderer_flush();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_emit_region_direct_call);
    RUN_TEST(test_emit_region_capacity_guard);
    RUN_TEST(test_emit_region_polygon_hull_vertex_count_preserved);
    RUN_TEST(test_set_material_auto_flush_on_change);
    return UNITY_END();
}
