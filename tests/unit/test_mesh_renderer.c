/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <string.h>

/* clang-format off */
/* NT_MESH_RENDERER_TEST_ACCESS defined via CMake target_compile_definitions */
#include "renderers/nt_mesh_renderer.h"
#include "graphics/nt_gfx.h"
#include "entity/nt_entity.h"
#include "transform_comp/nt_transform_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "material_comp/nt_material_comp.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "material/nt_material.h"
#include "resource/nt_resource.h"
#include "hash/nt_hash.h"
#include "render/nt_render_items.h"
#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "unity.h"
/* clang-format on */

/* ---- Virtual pack counter (unique per test) ---- */

static uint32_t s_vpack_counter;

/* ---- Helper: build a minimal mesh blob and activate it via nt_gfx ---- */

static nt_mesh_t create_test_mesh(void) {
    /* header + 1 stream desc + 3 vertices (3 floats each = 36B) + 3 uint16 indices (6B) */
    uint32_t streams_size = (uint32_t)sizeof(NtStreamDesc);
    uint32_t vdata_size = 3 * 3 * (uint32_t)sizeof(float); /* 3 vertices, 3 floats */
    uint32_t idata_size = 3 * (uint32_t)sizeof(uint16_t);  /* 3 indices */
    uint32_t blob_size = (uint32_t)sizeof(NtMeshAssetHeader) + streams_size + vdata_size + idata_size;
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 36 + 6];
    memset(blob, 0, sizeof(blob));

    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1; /* uint16 */
    hdr->vertex_count = 3;
    hdr->index_count = 3;
    hdr->vertex_data_size = vdata_size;
    hdr->index_data_size = idata_size;

    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->name_hash = nt_hash32_str("position").value;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;

    /* Vertex data: 3 positions */
    float *verts = (float *)(blob + sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc));
    verts[0] = 0.0F;
    verts[1] = 0.0F;
    verts[2] = 0.0F;
    verts[3] = 1.0F;
    verts[4] = 0.0F;
    verts[5] = 0.0F;
    verts[6] = 0.0F;
    verts[7] = 1.0F;
    verts[8] = 0.0F;

    /* Index data: triangle (0,1,2) */
    uint16_t *indices = (uint16_t *)(blob + sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + vdata_size);
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;

    uint32_t handle = nt_gfx_activate_mesh(blob, blob_size);
    return (nt_mesh_t){.id = handle};
}

/* ---- Helper: create a real GFX shader and register it as a resource, then create material ---- */

static nt_material_t create_test_material(void) {
    /* Create actual GFX shader handles so pipeline creation validates them */
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_VERTEX,
        .source = "void main(){}",
        .label = "test_vs",
    });
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_FRAGMENT,
        .source = "void main(){}",
        .label = "test_fs",
    });

    char vs_name[64];
    char fs_name[64];
    (void)snprintf(vs_name, sizeof(vs_name), "test_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof(fs_name), "test_fs_%u", s_vpack_counter);

    /* Create virtual pack and register shader resources with the real GFX handles */
    char pack_name[64];
    (void)snprintf(pack_name, sizeof(pack_name), "mat_pack_%u", s_vpack_counter++);
    nt_hash32_t pid = nt_hash32_str(pack_name);
    nt_hash64_t vs_rid = nt_hash64_str(vs_name);
    nt_hash64_t fs_rid = nt_hash64_str(fs_name);

    nt_resource_create_pack(pid, 0);
    nt_resource_register(pid, vs_rid, NT_ASSET_SHADER_CODE, vs.id);
    nt_resource_register(pid, fs_rid, NT_ASSET_SHADER_CODE, fs.id);

    nt_resource_t vs_res = nt_resource_request(vs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_t fs_res = nt_resource_request(fs_rid, NT_ASSET_SHADER_CODE);

    nt_resource_step(); /* resolve virtual packs immediately */

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.vs = vs_res;
    desc.fs = fs_res;
    desc.attr_map[0].stream_name = "position";
    desc.attr_map[0].location = 0;
    desc.attr_map_count = 1;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.cull_mode = NT_CULL_BACK;
    desc.label = "test_material";

    nt_material_t mat = nt_material_create(&desc);

    nt_material_step(); /* resolve shaders */

    return mat;
}

/* ---- Helper: create a fully-equipped test entity ---- */

static nt_entity_t create_test_entity(nt_mesh_t mesh, nt_material_t mat) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    nt_mesh_comp_add(e);
    nt_material_comp_add(e);
    nt_drawable_comp_add(e);

    /* Set mesh handle */
    *nt_mesh_comp_handle(e) = mesh;

    /* Set material handle */
    *nt_material_comp_handle(e) = mat;

    /* Set identity transform */
    float *pos = nt_transform_comp_position(e);
    pos[0] = 0.0F;
    pos[1] = 0.0F;
    pos[2] = 0.0F;
    nt_transform_comp_update();

    /* Set white color */
    float *color = nt_drawable_comp_color(e);
    color[0] = 1.0F;
    color[1] = 1.0F;
    color[2] = 1.0F;
    color[3] = 1.0F;

    return e;
}

/* ---- Unity setUp / tearDown ---- */

void setUp(void) {
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&(nt_gfx_desc_t){
        .max_shaders = 32,
        .max_pipelines = 64,
        .max_buffers = 256,
        .max_textures = 32,
        .max_meshes = 32,
    });
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 64});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 64});
    nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = 64});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 64});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 64});
    nt_material_init(&(nt_material_desc_t){.max_materials = 64});

    nt_mesh_renderer_desc_t desc = nt_mesh_renderer_desc_defaults();
    nt_mesh_renderer_init(&desc);

    /* Enter frame/pass so draw calls don't assert */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    s_vpack_counter = 0;
}

void tearDown(void) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_mesh_renderer_shutdown();
    nt_material_shutdown();
    nt_drawable_comp_shutdown();
    nt_material_comp_shutdown();
    nt_mesh_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    nt_resource_shutdown();
    nt_gfx_shutdown();
    nt_hash_shutdown();
}

/* ---- Test 1: init/shutdown lifecycle ---- */

void test_init_shutdown(void) {
    /* Module is initialized in setUp */
    nt_mesh_renderer_shutdown();
    /* Re-init for tearDown to work cleanly */
    nt_mesh_renderer_desc_t desc = nt_mesh_renderer_desc_defaults();
    nt_mesh_renderer_init(&desc);
}

/* ---- Test 2: draw_list with count=0 is a no-op ---- */

void test_draw_list_empty(void) {
    nt_mesh_renderer_draw_list(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_draw_call_count());
}

/* ---- Test 3: single item produces 1 draw call ---- */

void test_draw_list_single_item(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_batch_key(mat.id, mesh.id);

    nt_mesh_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_instance_total());
}

/* ---- Test 4: 3 items with same material+mesh -> 1 draw call, 3 instances ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_draw_list_same_material_mesh_batching(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();

    nt_entity_t e0 = create_test_entity(mesh, mat);
    nt_entity_t e1 = create_test_entity(mesh, mat);
    nt_entity_t e2 = create_test_entity(mesh, mat);

    uint32_t bk = nt_batch_key(mat.id, mesh.id);
    nt_render_item_t items[3];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = bk;
    items[1].sort_key = 0;
    items[1].entity = e1.id;
    items[1].batch_key = bk;
    items[2].sort_key = 0;
    items[2].entity = e2.id;
    items[2].batch_key = bk;

    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_instance_total());
}

/* ---- Test 5: 2 items with different materials -> 2 draw calls ---- */

void test_draw_list_different_materials(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat_a = create_test_material();
    nt_material_t mat_b = create_test_material();

    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);

    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = nt_batch_key(mat_a.id, mesh.id);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_batch_key(mat_b.id, mesh.id);

    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_draw_call_count());
}

/* ---- Test 6: alternating materials -> 3 draw calls (no re-batching) ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_draw_list_alternating_materials(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat_a = create_test_material();
    nt_material_t mat_b = create_test_material();

    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_entity_t e2 = create_test_entity(mesh, mat_a);

    uint32_t bk_a = nt_batch_key(mat_a.id, mesh.id);
    uint32_t bk_b = nt_batch_key(mat_b.id, mesh.id);
    nt_render_item_t items[3];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = bk_a;
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = bk_b;
    items[2].sort_key = 2;
    items[2].entity = e2.id;
    items[2].batch_key = bk_a;

    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_draw_call_count());
}

/* ---- Test 7: pipeline cache reuse across draw_list calls ---- */

void test_pipeline_cache_reuse(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_batch_key(mat.id, mesh.id);

    /* First draw_list call */
    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());

    /* Second draw_list call with same material+mesh */
    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
}

/* ---- Test 8: different shader programs -> different cached pipelines ---- */

void test_pipeline_cache_different_layouts(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat_a = create_test_material();
    nt_material_t mat_b = create_test_material();

    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);

    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = nt_batch_key(mat_a.id, mesh.id);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_batch_key(mat_b.id, mesh.id);

    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_pipeline_cache_count());
}

/* ---- Test 9: restore_gpu clears cache and subsequent draw still works ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_restore_gpu(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_batch_key(mat.id, mesh.id);

    /* Draw to populate cache */
    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());

    /* Restore GPU context */
    nt_mesh_renderer_restore_gpu();
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_pipeline_cache_count());

    /* Subsequent draw should still work (rebuilds cache lazily) */
    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
}

/* ---- main ---- */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_shutdown);
    RUN_TEST(test_draw_list_empty);
    RUN_TEST(test_draw_list_single_item);
    RUN_TEST(test_draw_list_same_material_mesh_batching);
    RUN_TEST(test_draw_list_different_materials);
    RUN_TEST(test_draw_list_alternating_materials);
    RUN_TEST(test_pipeline_cache_reuse);
    RUN_TEST(test_pipeline_cache_different_layouts);
    RUN_TEST(test_restore_gpu);

    return UNITY_END();
}
