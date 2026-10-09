#define NT_LOG_DOMAIN "mesh_renderer_test"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/nt_gfx_test_desc.h"
#include "test_helpers/nt_gfx_test_frame.h"
/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <string.h>

/* clang-format off */
/* NT_TEST_ACCESS defined via CMake target_compile_definitions */
#include "renderers/nt_mesh_renderer.h"
#include "renderers/nt_renderer_shared.h"
#include "graphics/nt_gfx.h"
#include "entity/nt_entity.h"
#include "transform_comp/nt_transform_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "material_comp/nt_material_comp.h"
#include "color/nt_color.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "material/nt_material.h"
#include "resource/nt_resource.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "render/nt_render_items.h"
#include "render/nt_render_defs.h"
#include "graphics/nt_gfx_internal.h"
#include "test_helpers/nt_assert_trap.h"
#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "unity.h"
/* clang-format on */

#define TEST_MAX_VERTEX_INPUTS 160

/* ---- Virtual pack counter (unique per test) ---- */

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

/* Non-indexed variant: 3 vertices, no index data (index_type NONE). */
static nt_mesh_t create_test_mesh_nonindexed(void) {
    uint32_t vdata_size = 3 * 3 * (uint32_t)sizeof(float);
    uint32_t blob_size = (uint32_t)sizeof(NtMeshAssetHeader) + (uint32_t)sizeof(NtStreamDesc) + vdata_size;
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 36];
    memset(blob, 0, sizeof(blob));

    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 0; /* none */
    hdr->vertex_count = 3;
    hdr->index_count = 0;
    hdr->vertex_data_size = vdata_size;
    hdr->index_data_size = 0;

    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->name_hash = nt_hash32_str("position").value;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;

    float *verts = (float *)(blob + sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc));
    verts[3] = 1.0F;
    verts[7] = 1.0F;

    uint32_t handle = nt_gfx_activate_mesh(blob, blob_size);
    return (nt_mesh_t){.id = handle};
}

/* Two streams (position, normal), non-indexed: for tests that map a subset. */
static nt_mesh_t create_test_mesh_two_streams(void) {
    uint32_t vdata_size = 3 * 6 * (uint32_t)sizeof(float);
    uint32_t blob_size = (uint32_t)sizeof(NtMeshAssetHeader) + (2 * (uint32_t)sizeof(NtStreamDesc)) + vdata_size;
    uint8_t blob[sizeof(NtMeshAssetHeader) + (2 * sizeof(NtStreamDesc)) + 72];
    memset(blob, 0, sizeof(blob));

    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 2;
    hdr->index_type = 0;
    hdr->vertex_count = 3;
    hdr->index_count = 0;
    hdr->vertex_data_size = vdata_size;
    hdr->index_data_size = 0;

    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd[0].name_hash = nt_hash32_str("position").value;
    sd[0].type = NT_STREAM_FLOAT32;
    sd[0].count = 3;
    sd[1].name_hash = nt_hash32_str("normal").value;
    sd[1].type = NT_STREAM_FLOAT32;
    sd[1].count = 3;

    uint32_t handle = nt_gfx_activate_mesh(blob, blob_size);
    return (nt_mesh_t){.id = handle};
}

/* ---- Helper: link a real GFX program, then create a material on it ---- */

static nt_program_t create_test_program(void) { return nt_gfx_fake_make_program(NULL, 0); }

static nt_program_t create_test_tex_program(void) { return nt_gfx_fake_make_program((const char *const[]){"u_tex"}, 1); }

static nt_material_t create_test_material_with_attr(nt_program_t program, const char *stream_name, uint8_t location, nt_blend_state_t blend) {

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = program;
    desc.attr_map[0].stream_name = stream_name;
    desc.attr_map[0].location = location;
    desc.attr_map_count = 1;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.blend = blend;
    desc.cull_mode = NT_CULL_BACK;
    desc.label = "test_material";

    nt_material_t mat = nt_material_create(&desc);
    return mat;
}

static nt_material_t create_test_material(void) { return create_test_material_with_attr(create_test_program(), "position", 0, nt_blend_opaque()); }

static nt_material_t create_test_material_with_blend(nt_blend_state_t blend) { return create_test_material_with_attr(create_test_program(), "position", 0, blend); }

/* Two textures shared by the textured test materials: materials on the same one
 * measure binding deltas, materials on different ones measure slot changes. */
#define TEST_TEXTURE_COUNT 2
static nt_resource_t s_test_tex_res[TEST_TEXTURE_COUNT];
static bool s_test_tex_pack_created;

static nt_resource_t test_texture(uint32_t index) {
    TEST_ASSERT_LESS_THAN_UINT32(TEST_TEXTURE_COUNT, index);
    if (s_test_tex_res[index].id == 0) {
        static const uint8_t white[4] = {255, 255, 255, 255};
        char name[32];
        (void)snprintf(name, sizeof(name), "mesh_renderer_tex%u", index);
        nt_hash32_t pid = nt_hash32_str("mesh_renderer_tex_pack");
        nt_hash64_t rid = nt_hash64_str(name);
        if (!s_test_tex_pack_created) {
            nt_resource_create_pack(pid, 4);
            s_test_tex_pack_created = true;
        }
        nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = white, .format = NT_TEXTURE_FORMAT_RGBA8, .label = "mesh_tex"});
        nt_resource_register(pid, rid, NT_ASSET_TEXTURE, tex.id);
        s_test_tex_res[index] = nt_resource_request(rid, NT_ASSET_TEXTURE);
        nt_resource_step();
    }
    return s_test_tex_res[index];
}

/* Textured + one vec4 param: the existing test materials declare neither, so
 * uniform and texture-slot counts would be vacuous without this. */
static nt_material_t create_test_material_on_texture(nt_program_t program, nt_blend_state_t blend, nt_sampler_t override_sampler, uint32_t tex_index) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = program;
    desc.attr_map[0].stream_name = "position";
    desc.attr_map[0].location = 0;
    desc.attr_map_count = 1;
    desc.textures[0].name = "u_tex";
    desc.textures[0].resource = test_texture(tex_index);
    desc.textures[0].sampler = override_sampler;
    desc.texture_count = 1;
    desc.params[0].name = "u_tint";
    desc.params[0].value[0] = 1.0F;
    desc.param_count = 1;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.blend = blend;
    desc.cull_mode = NT_CULL_BACK;
    desc.label = "test_material_textured";
    nt_material_t mat = nt_material_create(&desc);
    return mat;
}

static nt_material_t create_test_material_textured(nt_program_t program, nt_blend_state_t blend, nt_sampler_t override_sampler) {
    return create_test_material_on_texture(program, blend, override_sampler, 0);
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
    nt_drawable_comp_set_color(e, 0xFFFFFFFFU);

    return e;
}

/* Leaves the pass the tests draw in closed and opens the next frame with empty frame storage. */
static void begin_storage_frame(void) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
}

static uint32_t s_draw_mark; /* fake draw trace length when the last draw_list began */

static void mark_draws(void) { s_draw_mark = nt_gfx_fake_draw_trace_count(); }

/* Counts come from the backend trace, so a run the renderer skips is not counted. */
static uint32_t drawn_calls(void) {
    TEST_ASSERT_FALSE(nt_gfx_fake_draw_trace_overflowed());
    return nt_gfx_fake_draw_trace_count() - s_draw_mark;
}

static uint32_t drawn_instances(void) {
    uint32_t total = 0;
    for (uint32_t i = s_draw_mark; i < nt_gfx_fake_draw_trace_count(); i++) {
        total += nt_gfx_fake_draw_trace_at(i).instance_count;
    }
    return total;
}

/* Records a single list in a fresh frame's pass; the frame stays open. */
static void record_list(const nt_render_item_t *items, uint32_t count) {
    nt_test_frame_next();
    mark_draws();
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, items, count);
}

/* One gfx frame drawing a single list in one pass, executed before it returns. */
static void draw_list(const nt_render_item_t *items, uint32_t count) {
    record_list(items, count);
    nt_test_frame_next();
}

/* ---- Unity setUp / tearDown ---- */

void setUp(void) {
    memset(s_test_tex_res, 0, sizeof(s_test_tex_res));
    s_test_tex_pack_created = false;
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&NT_GFX_TEST_DESC(.max_shaders = 32, .max_programs = 64, .max_pipelines = 64, .max_buffers = 256, .max_textures = 32, .max_meshes = 32, .max_vertex_inputs = TEST_MAX_VERTEX_INPUTS,
                                  .max_render_targets = 16));
    nt_gfx_begin_frame();
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 64});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 64});
    nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = 64});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 64});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 64});
    nt_material_init(&(nt_material_desc_t){.max_materials = 64});

    nt_mesh_renderer_desc_t desc = nt_mesh_renderer_desc_defaults();
    nt_mesh_renderer_init(&desc);

    nt_gfx_fake_draw_trace_reset(true);
    s_draw_mark = 0;
    /* Enter frame/pass so draw calls don't assert */
    nt_test_frame_begin_pass();
}

void tearDown(void) {
    nt_test_frame_teardown();
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

/* ---- Test 2: an empty list reserves nothing and draws nothing ---- */

void test_draw_list_empty(void) {
    draw_list(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_update_buffer_count());
}

void test_draw_list_null_items_asserts_when_nonempty(void) { NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, NULL, 1)); }

void test_unready_program_skips_until_a_ready_program_is_assigned(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_with_attr(NT_PROGRAM_INVALID, "position", 0, nt_blend_opaque());
    nt_entity_t entity = create_test_entity(mesh, mat);
    nt_render_item_t item = {.entity = entity.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};

    draw_list(&item, 1);
    draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());

    nt_program_t program = create_test_program();
    nt_material_set_program(mat, program);
    draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());

    nt_test_frame_close();
    nt_gfx_destroy_program(program);
    nt_test_frame_open();
    draw_list(&item, 1);
    draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());
}

void test_batch_key_packs_material_and_mesh_slots(void) {
    nt_material_t material = {.id = 0x00010001U};
    nt_mesh_t mesh = {.id = 0x00020001U};

    TEST_ASSERT_EQUAL_HEX32(0x00010001U, nt_mesh_renderer_batch_key(material, mesh));
}

void test_batch_key_ignores_generation_bits(void) {
    nt_material_t material_a = {.id = 0x00010001U};
    nt_material_t material_b = {.id = 0xABCD0001U};
    nt_mesh_t mesh_a = {.id = 0x00020002U};
    nt_mesh_t mesh_b = {.id = 0xDCBA0002U};

    TEST_ASSERT_EQUAL_HEX32(nt_mesh_renderer_batch_key(material_a, mesh_a), nt_mesh_renderer_batch_key(material_b, mesh_b));
}

void test_batch_key_distinguishes_old_hash_collision(void) {
    nt_material_t material_a = {.id = 0x00010001U};
    nt_mesh_t mesh_a = {.id = 0x00020001U};
    nt_material_t material_b = {.id = 0x002D00CDU};
    nt_mesh_t mesh_b = {.id = 0x0003009DU};

    TEST_ASSERT_EQUAL_HEX32(0x00010001U, nt_mesh_renderer_batch_key(material_a, mesh_a));
    TEST_ASSERT_EQUAL_HEX32(0x00CD009DU, nt_mesh_renderer_batch_key(material_b, mesh_b));
    TEST_ASSERT_NOT_EQUAL(nt_mesh_renderer_batch_key(material_a, mesh_a), nt_mesh_renderer_batch_key(material_b, mesh_b));
}

void test_batch_key_supports_max_slots(void) {
    nt_material_t material = {.id = 0x1234FFFFU};
    nt_mesh_t mesh = {.id = 0x5678FFFFU};

    TEST_ASSERT_EQUAL_HEX32(UINT32_MAX, nt_mesh_renderer_batch_key(material, mesh));
}

/* ---- Test 3: single item produces 1 draw call ---- */

void test_draw_list_single_item(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

    draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, drawn_instances());
}

void test_mesh_renderer_forwards_material_blend_state(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.constant_color[0] = 0.25F;
    blend.src_rgb = NT_BLEND_CONSTANT_COLOR;
    blend.dst_rgb = NT_BLEND_ONE_MINUS_DST_COLOR;
    blend.src_alpha = NT_BLEND_SRC_ALPHA_SATURATE;
    blend.dst_alpha = NT_BLEND_ONE_MINUS_DST_ALPHA;
    blend.op_rgb = NT_BLEND_OP_SUBTRACT;
    blend.op_alpha = NT_BLEND_OP_MAX;
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_with_blend(blend);
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t item = {.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};

    draw_list(&item, 1);

    nt_blend_state_t actual = nt_gfx_fake_last_pipeline_blend();
    TEST_ASSERT_EQUAL_MEMORY(&blend, &actual, sizeof(blend));
}

/* ---- Test 4: 3 items with same material+mesh -> 1 draw call, 3 instances ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_draw_list_same_material_mesh_batching(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();

    nt_entity_t e0 = create_test_entity(mesh, mat);
    nt_entity_t e1 = create_test_entity(mesh, mat);
    nt_entity_t e2 = create_test_entity(mesh, mat);

    uint32_t bk = nt_mesh_renderer_batch_key(mat, mesh);
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

    draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(3, drawn_instances());
}

/* An unready run is neither packed nor drawn: the next ready run starts the list. */
void test_draw_list_skips_a_not_ready_run_without_packing_it(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t not_ready = create_test_material();
    nt_material_t ready = create_test_material();
    nt_material_set_program(not_ready, NT_PROGRAM_INVALID);

    nt_entity_t e0 = create_test_entity(mesh, not_ready);
    nt_entity_t e1 = create_test_entity(mesh, ready);

    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(not_ready, mesh);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_mesh_renderer_batch_key(ready, mesh);

    draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_fake_last_update_buffer_offset(), nt_gfx_fake_last_instance_offset());
    TEST_ASSERT_EQUAL_UINT32(sizeof(nt_mesh_instance_t), nt_gfx_fake_last_update_buffer_size());
}

/* The restore window: the game destroyed its program and the material still
 * names it. The gate asks liveness, not assignment, so the run is skipped --
 * asking assignment here would reach make_pipeline's readiness assert. */
void test_draw_list_skips_a_run_whose_program_was_destroyed(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_test_frame_close();
    nt_gfx_destroy_program(nt_material_get_info(mat)->program);
    nt_test_frame_open();

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

    draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());
}

/* A linking program is skipped like an invalid one: no pipeline, no bind, until a begin_frame finishes it. */
void test_draw_list_skips_a_linking_program_until_its_link_finishes(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t entity = create_test_entity(mesh, mat);
    nt_render_item_t item = {.entity = entity.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};

    nt_gfx_fake_hold_program_links(true);
    nt_material_set_program(mat, create_test_program());
    mark_draws();
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, &item, 1); /* the frame that made the program */
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());
    draw_list(&item, 1); /* later frames, link still held */
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_pipeline_cache_count());

    nt_gfx_fake_hold_program_links(false);
    draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
}

/* Same reset contract as the sprite renderer: a cached pipeline borrows the
 * material's program, so it must not survive into the next epoch. */
void test_reset_drops_cached_pipelines(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());

    nt_test_frame_close();
    nt_mesh_renderer_restore_gpu();
    nt_test_frame_open();
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_pipeline_cache_count());

    nt_material_set_program(mat, NT_PROGRAM_INVALID);
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
    items[0].batch_key = nt_mesh_renderer_batch_key(mat_a, mesh);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_mesh_renderer_batch_key(mat_b, mesh);

    nt_gfx_fake_draw_trace_reset(true);
    draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    /* Creating B's pipeline mid-list must not disturb the state A already bound. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_gfx_fake_draw_trace_at(0).pipeline.id, nt_gfx_fake_draw_trace_at(1).pipeline.id);
    nt_gfx_fake_draw_trace_reset(false);
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

    uint32_t bk_a = nt_mesh_renderer_batch_key(mat_a, mesh);
    uint32_t bk_b = nt_mesh_renderer_batch_key(mat_b, mesh);
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

    draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(3, drawn_calls());
}

/* ---- Test 7: pipeline cache reuse across draw_list calls ---- */

void test_pipeline_cache_reuse(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

    /* First draw_list call */
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());

    /* Second draw_list call with same material+mesh */
    draw_list(items, 1);
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
    items[0].batch_key = nt_mesh_renderer_batch_key(mat_a, mesh);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_mesh_renderer_batch_key(mat_b, mesh);

    draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_pipeline_cache_count());
}

/* Programs come out of the pool in creation order, so two shader pairs get
 * neighbouring ids; one cull step on the neighbour must not land on the same key. */
void test_neighbouring_programs_one_cull_step_apart_get_their_own_pipelines(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t p0 = create_test_program();
    nt_program_t p1 = create_test_program();
    TEST_ASSERT_EQUAL_UINT32(p0.id + 1, p1.id);

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.attr_map[0].stream_name = "position";
    desc.attr_map[0].location = 0;
    desc.attr_map_count = 1;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.blend = nt_blend_opaque();
    desc.program = p0;
    desc.cull_mode = NT_CULL_BACK;
    nt_material_t mat_a = nt_material_create(&desc);
    desc.program = p1;
    desc.cull_mode = NT_CULL_NONE;
    nt_material_t mat_b = nt_material_create(&desc);

    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    nt_gfx_fake_draw_trace_reset(true);
    draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(p0.id, nt_gfx_fake_draw_trace_at(0).program.id);
    TEST_ASSERT_EQUAL_UINT32(p1.id, nt_gfx_fake_draw_trace_at(1).program.id);
}

/* An unresolved slot would leave the previous material's texture on the unit, so the
 * placeholder contract is asserted instead. */
void test_declared_sampler_without_a_resolved_texture_asserts(void) {
    nt_mesh_t mesh = create_test_mesh();

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = create_test_tex_program();
    desc.textures[0].name = "u_tex";
    desc.textures[0].resource = nt_resource_request(nt_hash64_str("never_registered"), NT_ASSET_TEXTURE);
    desc.texture_count = 1;
    desc.attr_map[0].stream_name = "position";
    desc.attr_map_count = 1;
    desc.label = "unresolved_tex_material";
    nt_material_t mat = nt_material_create(&desc);

    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_EQUAL_UINT32(0, nt_resource_get(info->tex_resources[0]));

    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    NT_TEST_EXPECT_ASSERT(draw_list(items, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "texture_pool"));
}

/* Textures resolve at the material transition, so a slot published after the material
 * was created binds on its first draw, and a republished id replaces it on the next. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_texture_published_after_material_create_binds_at_next_draw(void) {
    nt_mesh_t mesh = create_test_mesh();

    /* Own pack: test_texture() latches "mesh_renderer_tex_pack" for its own slots. */
    const nt_hash32_t pid = nt_hash32_str("mesh_renderer_late_pack");
    const nt_hash64_t rid = nt_hash64_str("mesh_renderer_late_tex");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));

    /* Requested before anything registers it: the slot publishes 0. */
    nt_resource_t res = nt_resource_request(rid, NT_ASSET_TEXTURE);
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(0, nt_resource_get(res));

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = create_test_tex_program();
    desc.textures[0].name = "u_tex";
    desc.textures[0].resource = res;
    desc.texture_count = 1;
    desc.attr_map[0].stream_name = "position";
    desc.attr_map[0].location = 0;
    desc.attr_map_count = 1;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.blend = nt_blend_opaque();
    desc.cull_mode = NT_CULL_BACK;
    desc.label = "late_tex_material";
    nt_material_t mat = nt_material_create(&desc);

    static const uint8_t white[4] = {255, 255, 255, 255};
    nt_texture_t tex1 = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = white, .format = NT_TEXTURE_FORMAT_RGBA8, .label = "late_tex1"});
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, tex1.id));
    nt_resource_step();

    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_gfx_fake_reset();
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(tex1), nt_gfx_fake_bound_texture_at(0));

    /* Re-registering the same (pack, rid) republishes; the next transition follows it. */
    nt_texture_t tex2 = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = white, .format = NT_TEXTURE_FORMAT_RGBA8, .label = "late_tex2"});
    TEST_ASSERT_NOT_EQUAL_UINT32(tex1.id, tex2.id);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, rid, NT_ASSET_TEXTURE, tex2.id));
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(tex2.id, nt_resource_get(res));

    nt_gfx_fake_reset();
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(tex2), nt_gfx_fake_bound_texture_at(0));
}

/* The declared name is what selects the unit, so a material whose program has no such
 * sampler simply skips the slot -- and the program's empty interface stays covered. */
void test_declared_sampler_unknown_to_the_program_is_ignored(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_gfx_fake_reset();
    draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
}

/* A material that leaves one of its program's samplers undeclared would sample
 * whatever the previous material left on that unit. */
void test_material_missing_a_program_sampler_asserts(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t two = nt_gfx_fake_make_program((const char *const[]){"u_tex", "u_second"}, 2);
    nt_material_t mat = create_test_material_textured(two, nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    NT_TEST_EXPECT_ASSERT(draw_list(items, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "coverage is incomplete"));
}

/* The texture goes to the unit the link assigned, not to the material slot index. */
void test_texture_lands_on_the_program_sampler_unit(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t second_unit = nt_gfx_fake_make_program((const char *const[]){"u_other", "u_tex"}, 2);
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = second_unit;
    desc.textures[0].name = "u_tex";
    desc.textures[0].resource = test_texture(0);
    desc.textures[1].name = "u_other";
    desc.textures[1].resource = test_texture(1);
    desc.texture_count = 2;
    desc.attr_map[0].stream_name = "position";
    desc.attr_map_count = 1;
    desc.label = "swapped_slot_material";
    nt_material_t mat = nt_material_create(&desc);

    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_gfx_fake_reset();
    draw_list(items, 1);

    /* Backend application is canonical unit order: u_other/0, then u_tex/1. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(test_texture(1))}), nt_gfx_fake_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_slot_at(0));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(test_texture(0))}), nt_gfx_fake_bound_texture_at(1));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_slot_at(1));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
}

// #region state transitions
/* The mesh renderer splits pipeline / vertex-input / material transitions, so a
 * run that changes only the mesh must issue no material work at all. */

static void fill_items(nt_render_item_t *items, const nt_entity_t *entities, const nt_material_t *mats, const nt_mesh_t *meshes, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        items[i].sort_key = (uint16_t)i;
        items[i].entity = entities[i].id;
        items[i].batch_key = nt_mesh_renderer_batch_key(mats[i], meshes[i]);
    }
}

void test_state_same_material_three_meshes(void) {
    nt_mesh_t meshes[3] = {create_test_mesh(), create_test_mesh(), create_test_mesh()};
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_t mats[3] = {mat, mat, mat};
    nt_entity_t entities[3] = {create_test_entity(meshes[0], mat), create_test_entity(meshes[1], mat), create_test_entity(meshes[2], mat)};

    nt_render_item_t items[3];
    fill_items(items, entities, mats, meshes, 3);

    nt_gfx_fake_reset();
    draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(3, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bind_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.last_frame.accepted[NT_GFX_OP_TEXTURE_SET]); /* mesh-only changes skip material work */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_sampler_count());
}

/* A list packs into the stream it names and binds that stream's instances. */
void test_draw_list_packs_into_its_stream(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_t mats[2] = {mat, mat};
    nt_mesh_t meshes[2] = {mesh, mesh};
    nt_entity_t entities[2] = {create_test_entity(mesh, mat), create_test_entity(mesh, mat)};
    nt_render_item_t items[2];
    fill_items(items, entities, mats, meshes, 2);

    nt_test_frame_next();
    const uint32_t general = g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used;
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX + 1, items, 2);
    TEST_ASSERT_EQUAL_UINT32(general, g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    TEST_ASSERT_EQUAL_UINT32(2U * sizeof(nt_mesh_instance_t), g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX + 1].used);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_last_instance_clone());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_instance_offset());
}

/* The core draw reads instances the game allocated in the named stream this frame. */
void test_core_draw_asserts_on_instances_outside_the_stream(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_test_frame_next();
    uint32_t offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX + 1, sizeof(nt_mesh_instance_t), 4, &offset), 0, sizeof(nt_mesh_instance_t));
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw(mesh, mat, NT_GFX_FRAME_VERTEX + 1, offset, 2));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "outside this frame's allocations"));
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw(mesh, mat, NT_GFX_FRAME_INDEX, 0, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "not a frame vertex stream"));
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(NT_GFX_FRAME_UNIFORM, NULL, 0));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "not a frame vertex stream"));
    nt_mesh_renderer_draw(mesh, mat, NT_GFX_FRAME_VERTEX + 1, offset, 1);
    nt_test_frame_next();
}

void test_state_three_materials_same_mesh(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t program = create_test_tex_program();
    nt_material_t mats[3] = {
        create_test_material_textured(program, nt_blend_opaque(), NT_SAMPLER_DEFAULT),
        create_test_material_textured(program, nt_blend_opaque(), NT_SAMPLER_DEFAULT),
        create_test_material_textured(program, nt_blend_opaque(), NT_SAMPLER_DEFAULT),
    };
    nt_mesh_t meshes[3] = {mesh, mesh, mesh};
    nt_entity_t entities[3] = {create_test_entity(mesh, mats[0]), create_test_entity(mesh, mats[1]), create_test_entity(mesh, mats[2])};

    nt_render_item_t items[3];
    fill_items(items, entities, mats, meshes, 3);

    nt_gfx_fake_reset();
    draw_list(items, 3);

    /* Same program + same render state => one pipeline; same derived layout => one VI, bound
     * once per run because each run has its own instance offset. */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bind_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_uniform_vec4_count());
    /* Every material transition applies its set; gfx drops the unchanged unit binds of the pass. */
    TEST_ASSERT_EQUAL_UINT32(3, g_nt_gfx.last_frame.accepted[NT_GFX_OP_TEXTURE_SET]);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_sampler_count());
}

void test_state_render_state_split(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t program = create_test_tex_program();
    nt_material_t mat_a = create_test_material_textured(program, nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_t mat_b = create_test_material_textured(program, nt_blend_alpha(), NT_SAMPLER_DEFAULT);
    nt_material_t mats[3] = {mat_a, mat_b, mat_a};
    nt_mesh_t meshes[3] = {mesh, mesh, mesh};
    nt_entity_t entities[3] = {create_test_entity(mesh, mat_a), create_test_entity(mesh, mat_b), create_test_entity(mesh, mat_a)};

    nt_render_item_t items[3];
    fill_items(items, entities, mats, meshes, 3);

    nt_gfx_fake_reset();
    nt_gfx_fake_draw_trace_reset(true);
    draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_pipeline_create_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_draw_trace_count());
    const uint32_t pip_a = nt_gfx_fake_draw_trace_at(0).pipeline.id;
    const uint32_t pip_b = nt_gfx_fake_draw_trace_at(1).pipeline.id;
    TEST_ASSERT_NOT_EQUAL_UINT32(pip_a, pip_b);
    TEST_ASSERT_EQUAL_UINT32(pip_a, nt_gfx_fake_draw_trace_at(2).pipeline.id);
    nt_gfx_fake_draw_trace_reset(false);
}

void test_state_runtime_set_param_between_calls(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1];
    fill_items(items, &e, &mat, &mesh, 1);

    nt_gfx_fake_reset();
    draw_list(items, 1);

    const float tint2[4] = {2.0F, 3.0F, 4.0F, 5.0F};
    nt_material_set_param(mat, "u_tint", tint2);
    draw_list(items, 1);

    /* Bound state is call-scoped, so the second call replays the material. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_uniform_vec4_count());
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("u_tint").value, nt_gfx_fake_uniform_vec4_hash_at(1));
    float last[4];
    nt_gfx_fake_uniform_vec4_value_at(1, last);
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT32((int32_t)tint2[i], (int32_t)last[i]);
    }
}

void test_state_program_replaced_between_calls(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1];
    fill_items(items, &e, &mat, &mesh, 1);

    nt_gfx_fake_reset();
    nt_gfx_fake_draw_trace_reset(true);
    draw_list(items, 1);

    nt_program_t p2 = create_test_tex_program();
    nt_material_set_program(mat, p2);
    draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    const nt_gfx_fake_draw_t first = nt_gfx_fake_draw_trace_at(0);
    const nt_gfx_fake_draw_t second = nt_gfx_fake_draw_trace_at(1);
    TEST_ASSERT_EQUAL_UINT32(p2.id, second.program.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(first.pipeline.id, second.pipeline.id);
    /* Bound state is call-scoped, so the new program's unit is filled again. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    nt_gfx_fake_draw_trace_reset(false);
}

/* A material without a sampler override must restore the texture's asset default
 * even when the previous material left an override on the unit. */
void test_state_texture_sampler_transitions(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t program = create_test_tex_program();
    nt_sampler_t override = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
    });
    TEST_ASSERT_TRUE(override.id != 0);

    nt_material_t mat_a = create_test_material_textured(program, nt_blend_opaque(), override);
    nt_material_t mat_b = create_test_material_textured(program, nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_t mats[3] = {mat_a, mat_b, mat_a};
    nt_mesh_t meshes[3] = {mesh, mesh, mesh};
    nt_entity_t entities[3] = {create_test_entity(mesh, mat_a), create_test_entity(mesh, mat_b), create_test_entity(mesh, mat_a)};

    nt_render_item_t items[3];
    fill_items(items, entities, mats, meshes, 3);

    nt_gfx_fake_reset();
    draw_list(items, 3);

    /* Texture and sampler travel together, so each of A / B / A is one bind of
     * each: override, default, override. The texture repeats, and only the GL
     * cache absorbs that -- the fake counts every call. */
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bind_sampler_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_sampler_backend_id(override), nt_gfx_fake_last_sampler(0));
}

/* An override costs one sampler bind per texture change, never a default bind
 * followed by the override, and repeats on the slot cost nothing. */
void test_state_override_binds_one_sampler_per_texture_change(void) {
    nt_mesh_t meshes[3] = {create_test_mesh(), create_test_mesh(), create_test_mesh()};
    nt_sampler_t override = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
    });
    TEST_ASSERT_TRUE(override.id != 0);

    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), override);
    nt_material_t mats[3] = {mat, mat, mat};
    nt_entity_t entities[3] = {create_test_entity(meshes[0], mat), create_test_entity(meshes[1], mat), create_test_entity(meshes[2], mat)};

    nt_render_item_t items[3];
    fill_items(items, entities, mats, meshes, 3);

    nt_gfx_fake_reset();
    draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(3, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_sampler_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_sampler_backend_id(override), nt_gfx_fake_last_sampler(0));
}

/* Distinct textures on one program: the slot rebinds every time the material
 * changes, and the sampler unit is rewritten with it. */
void test_state_distinct_textures_a_b_a(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t program = create_test_tex_program();
    nt_material_t mat_a = create_test_material_on_texture(program, nt_blend_opaque(), NT_SAMPLER_DEFAULT, 0);
    nt_material_t mat_b = create_test_material_on_texture(program, nt_blend_opaque(), NT_SAMPLER_DEFAULT, 1);
    const uint32_t tex_x = nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(test_texture(0))});
    const uint32_t tex_y = nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(test_texture(1))});
    TEST_ASSERT_NOT_EQUAL_UINT32(tex_x, tex_y);

    nt_material_t mats[3] = {mat_a, mat_b, mat_a};
    nt_mesh_t meshes[3] = {mesh, mesh, mesh};
    nt_entity_t entities[3] = {create_test_entity(mesh, mat_a), create_test_entity(mesh, mat_b), create_test_entity(mesh, mat_a)};

    nt_render_item_t items[3];
    fill_items(items, entities, mats, meshes, 3);

    nt_gfx_fake_reset();
    draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(tex_x, nt_gfx_fake_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32(tex_y, nt_gfx_fake_bound_texture_at(1));
    TEST_ASSERT_EQUAL_UINT32(tex_x, nt_gfx_fake_bound_texture_at(2));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
}

/* An unready run issues no GL call, so it neither breaks the next run nor
 * invalidates what is bound: the following run on the same material draws
 * through the same pipeline without replaying its uniforms. */
void test_state_skip_mid_list_resolves_next_run(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat_a = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_t not_ready = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_set_program(not_ready, NT_PROGRAM_INVALID);

    nt_material_t mats[3] = {mat_a, not_ready, mat_a};
    nt_mesh_t meshes[3] = {mesh, mesh, mesh};
    nt_entity_t entities[3] = {create_test_entity(mesh, mat_a), create_test_entity(mesh, not_ready), create_test_entity(mesh, mat_a)};

    nt_render_item_t items[3];
    fill_items(items, entities, mats, meshes, 3);

    nt_gfx_fake_reset();
    nt_gfx_fake_draw_trace_reset(true);
    draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_fake_draw_trace_at(0).pipeline.id, nt_gfx_fake_draw_trace_at(1).pipeline.id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());
    nt_gfx_fake_draw_trace_reset(false);
}

/* A run whose pipeline could not be created binds nothing, so the run after it
 * still sees the state the run before it left bound. */
void test_state_pipeline_failure_mid_list_rebinds_next_run(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat_a = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_t mat_b = create_test_material_textured(create_test_tex_program(), nt_blend_alpha(), NT_SAMPLER_DEFAULT);

    nt_material_t mats[3] = {mat_a, mat_b, mat_a};
    nt_mesh_t meshes[3] = {mesh, mesh, mesh};
    nt_entity_t entities[3] = {create_test_entity(mesh, mat_a), create_test_entity(mesh, mat_b), create_test_entity(mesh, mat_a)};

    nt_render_item_t items[3];
    fill_items(items, entities, mats, meshes, 3);

    /* A's pipeline is created first, so the failure lands on B. */
    draw_list(items, 1);
    nt_gfx_fake_reset();
    nt_gfx_fake_draw_trace_reset(true);
    nt_gfx_fake_fail_next_pipeline_create();
    draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_fake_draw_trace_at(0).pipeline.id, nt_gfx_fake_draw_trace_at(1).pipeline.id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());
    /* The failed run is not packed either. */
    TEST_ASSERT_EQUAL_UINT32(2 * sizeof(nt_mesh_instance_t), nt_gfx_fake_last_update_buffer_size());
    nt_gfx_fake_draw_trace_reset(false);
}

void test_state_same_tex_same_sampler_diff_params(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t program = create_test_tex_program();
    nt_material_t mat_a = create_test_material_textured(program, nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_t mat_b = create_test_material_textured(program, nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_t mats[2] = {mat_a, mat_b};
    nt_mesh_t meshes[2] = {mesh, mesh};
    nt_entity_t entities[2] = {create_test_entity(mesh, mat_a), create_test_entity(mesh, mat_b)};

    nt_render_item_t items[2];
    fill_items(items, entities, mats, meshes, 2);

    nt_gfx_fake_reset();
    draw_list(items, 2);

    /* Both materials apply their set; same texture and sampler: the second unit bind of the pass is dropped. */
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.last_frame.accepted[NT_GFX_OP_TEXTURE_SET]);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_sampler_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_uniform_vec4_count());
}

// #endregion

/* Caching a failed pipeline would prevent a later frame from retrying creation. */
void test_pipeline_cache_skips_failed_pipeline(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_gfx_fake_fail_next_pipeline_create();
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());

    /* Next frame retries and succeeds. */
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
}

/* Manual dedup is the whole point of an explicit program: two materials on one
 * program, same layout and state, must collapse to a single pipeline. */
void test_pipeline_cache_shared_program_collapses(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t shared = create_test_program();
    nt_material_t mat_a = create_test_material_with_attr(shared, "position", 0, nt_blend_opaque());
    nt_material_t mat_b = create_test_material_with_attr(shared, "position", 0, nt_blend_opaque());
    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
}

/* Two runs in one list, with the transform and drawable dense indices apart (a transform-only
 * entity comes first): each instance takes its own world and color, and the second run starts
 * right after the first run's instances. */
void test_runs_of_one_list_pack_each_entity_world_and_color(void) {
    nt_entity_t filler = nt_entity_create();
    nt_transform_comp_add(filler);
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mats[2] = {create_test_material(), create_test_material()};
    nt_render_item_t items[4];
    for (uint32_t i = 0; i < 4; i++) {
        nt_entity_t e = create_test_entity(mesh, mats[i / 2]);
        nt_transform_comp_set_position(e, (float)(i + 1), 0.0F, 0.0F);
        nt_drawable_comp_set_color(e, 0x10203000U + i);
        items[i] = (nt_render_item_t){.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mats[i / 2], mesh)};
    }
    nt_transform_comp_update();

    record_list(items, 4);
    nt_test_frame_close(); /* frame storage keeps the frame's bytes until the next begin_frame */
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(4U * sizeof(nt_mesh_instance_t), g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    TEST_ASSERT_EQUAL_UINT32(2U * sizeof(nt_mesh_instance_t), nt_gfx_fake_last_instance_offset());
    for (uint32_t i = 0; i < 4; i++) {
        const nt_mesh_instance_t *instance = (const nt_mesh_instance_t *)g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].staging + i;
        TEST_ASSERT_EQUAL_HEX32(0x10203000U + i, instance->color);
        TEST_ASSERT_TRUE(instance->world_rows[0][3] == (float)(i + 1)); /* NOLINT -- exact small integer */
    }
    nt_test_frame_open();
}

/* Every render item needs a drawable: its color is instance data. */
void test_draw_list_asserts_on_an_item_without_drawable(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    nt_mesh_comp_add(e);
    nt_material_comp_add(e);
    *nt_mesh_comp_handle(e) = mesh;
    *nt_material_comp_handle(e) = mat;
    nt_render_item_t item = {.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, &item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "no drawable component"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

/* The world matrix comes from the transform view: an item without a transform asserts. */
void test_draw_list_asserts_on_an_item_without_transform(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = nt_entity_create();
    nt_mesh_comp_add(e);
    nt_material_comp_add(e);
    nt_drawable_comp_add(e);
    *nt_mesh_comp_handle(e) = mesh;
    *nt_material_comp_handle(e) = mat;
    nt_render_item_t item = {.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, &item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "no transform component"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

/* Neighbour test for the mesh vertex-input key: one-step changes of each lane
 * (which stream, its location, the presence bit) are four distinct vertex
 * inputs on one pipeline. {normal->0} vs {position->0} is the presence bit alone. */
void test_mesh_vertex_input_key_one_step_changes_split_vertex_inputs_not_pipelines(void) {
    nt_mesh_t mesh = create_test_mesh_two_streams();
    nt_program_t shared = create_test_program();
    nt_material_t mats[4];
    mats[0] = create_test_material_with_attr(shared, "position", 0, nt_blend_opaque());
    mats[1] = create_test_material_with_attr(shared, "position", 1, nt_blend_opaque());
    mats[2] = create_test_material_with_attr(shared, "normal", 0, nt_blend_opaque());

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = shared;
    desc.attr_map[0].stream_name = "position";
    desc.attr_map[0].location = 0;
    desc.attr_map[1].stream_name = "normal";
    desc.attr_map[1].location = 1;
    desc.attr_map_count = 2;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.blend = nt_blend_opaque();
    desc.cull_mode = NT_CULL_BACK;
    mats[3] = nt_material_create(&desc);

    nt_render_item_t items[4];
    for (uint32_t i = 0; i < 4; i++) {
        nt_entity_t e = create_test_entity(mesh, mats[i]);
        items[i] = (nt_render_item_t){.sort_key = i, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mats[i], mesh)};
    }

    draw_list(items, 4);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(4, nt_mesh_renderer_test_vertex_input_count());
}

/* Equal program/state share a pipeline; distinct attr_maps derive separate VIs. */
void test_pipeline_cache_different_material_attr_maps(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t shared = create_test_program();
    nt_material_t mat_a = create_test_material_with_attr(shared, "position", 0, nt_blend_opaque());
    nt_material_t mat_b = create_test_material_with_attr(shared, "position", 1, nt_blend_opaque());
    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_vertex_input_count());
}

/* ---- Vertex-input versions: reuse, dedup, identity, overflow ---- */

void test_vertex_input_reused_across_draw_list_calls(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_gfx_fake_reset();
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_vertex_input_create_count());
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_vertex_input_create_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
}

void test_vertex_input_distinct_per_mesh(void) {
    nt_mesh_t mesh_a = create_test_mesh();
    nt_mesh_t mesh_b = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e0 = create_test_entity(mesh_a, mat);
    nt_entity_t e1 = create_test_entity(mesh_b, mat);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_a)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_b)},
    };

    draw_list(items, 2);

    /* One material state: one pipeline; two meshes: two vertex inputs. */
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_vertex_input_count());
}

/* An attr_map entry matching none of the mesh's streams must not split the
 * vertex input: the DERIVED layout (streams x attr_map) is the identity. */
void test_vertex_input_shared_for_same_derived_layout(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t shared = create_test_program();
    nt_material_t mat_a = create_test_material_with_attr(shared, "position", 0, nt_blend_opaque());

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = shared;
    desc.attr_map[0].stream_name = "position";
    desc.attr_map[0].location = 0;
    desc.attr_map[1].stream_name = "not_a_mesh_stream";
    desc.attr_map[1].location = 5;
    desc.attr_map_count = 2;
    desc.depth_test = true;
    desc.depth_write = true;
    desc.blend = nt_blend_opaque();
    desc.cull_mode = NT_CULL_BACK;
    desc.label = "extra_attr_material";
    nt_material_t mat_b = nt_material_create(&desc);

    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
}

/* A material mapping none of the mesh's streams derives an empty layout: the
 * attribute-less gl_VertexID path draws through a vertex input with no VBO. */
void test_vertex_input_empty_derived_layout_draws(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_with_attr(create_test_program(), "not_a_mesh_stream", 0, nt_blend_opaque());
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
}

/* The attribute-less version (no VBO, no IBO) escapes the destroy-buffer cascade, so every mesh
 * shares one; a reused mesh slot drops its row without destroying anything. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_bufferless_vertex_input_shared_and_slot_reuse_destroys_nothing(void) {
    nt_program_t program = create_test_program();
    nt_material_t mat = create_test_material_with_attr(program, "not_a_mesh_stream", 0, nt_blend_opaque());
    nt_material_t buffered = create_test_material_with_attr(program, "position", 0, nt_blend_opaque());
    nt_mesh_t mesh = create_test_mesh_nonindexed();
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_entity_t e_buffered = create_test_entity(mesh, buffered);
    nt_mesh_t neighbor = create_test_mesh_nonindexed();
    nt_entity_t e_neighbor = create_test_entity(neighbor, mat);
    nt_render_item_t neighbor_item = {.entity = e_neighbor.id, .batch_key = nt_mesh_renderer_batch_key(mat, neighbor)};
    nt_gfx_fake_reset();
    draw_list(&neighbor_item, 1);

    for (uint32_t cycle = 0; cycle < 6; cycle++) {
        nt_render_item_t items[2] = {
            {.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)},
            {.entity = e_buffered.id, .batch_key = nt_mesh_renderer_batch_key(buffered, mesh)},
        };
        draw_list(items, 2); /* its frame ends before the next opens: last_frame holds only this list */
        TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.last_frame.accepted[NT_GFX_OP_DESTROY]);
        draw_list(&neighbor_item, 1);
        TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_vertex_input_count());
        TEST_ASSERT_EQUAL_UINT32(2 + cycle, nt_gfx_fake_vertex_input_create_count());
        const uint32_t old_id = mesh.id;
        nt_test_frame_close();
        nt_gfx_deactivate_mesh(mesh.id);
        nt_test_frame_open();
        mesh = create_test_mesh_nonindexed(); /* reuses the freed pool slot */
        TEST_ASSERT_EQUAL_UINT32(nt_pool_slot_index(old_id), nt_pool_slot_index(mesh.id));
        TEST_ASSERT_NOT_EQUAL(old_id, mesh.id);
        *nt_mesh_comp_handle(e) = mesh;
        *nt_mesh_comp_handle(e_buffered) = mesh;
    }

    /* Only the shared bufferless version is live; all other slots must be free. */
    for (uint32_t i = 1; i < TEST_MAX_VERTEX_INPUTS; i++) {
        nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
        TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    }
}

/* An empty derived layout on an indexed mesh keeps the mesh's IBO, so it is a per-mesh version
 * that dies with the mesh's buffers. */
void test_empty_layout_with_index_buffer_is_per_mesh(void) {
    nt_material_t mat = create_test_material_with_attr(create_test_program(), "not_a_mesh_stream", 0, nt_blend_opaque());
    nt_mesh_t mesh_a = create_test_mesh();
    nt_mesh_t mesh_b = create_test_mesh();
    nt_render_item_t items[2] = {
        {.entity = create_test_entity(mesh_a, mat).id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_a)},
        {.entity = create_test_entity(mesh_b, mat).id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_b)},
    };
    nt_gfx_fake_reset();
    draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_vertex_input_count());
    nt_test_frame_close();
    nt_gfx_deactivate_mesh(mesh_a.id);
    nt_test_frame_open();
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
}

/* restore_gpu destroys the shared bufferless version once; the next draw makes a new one. */
void test_bufferless_vertex_input_recreated_after_restore(void) {
    nt_material_t mat = create_test_material_with_attr(create_test_program(), "not_a_mesh_stream", 0, nt_blend_opaque());
    nt_mesh_t mesh_a = create_test_mesh_nonindexed();
    nt_mesh_t mesh_b = create_test_mesh_nonindexed();
    nt_render_item_t items[2] = {
        {.entity = create_test_entity(mesh_a, mat).id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_a)},
        {.entity = create_test_entity(mesh_b, mat).id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_b)},
    };
    nt_gfx_fake_reset();
    draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_vertex_input_create_count());
    nt_test_frame_close();
    nt_mesh_renderer_restore_gpu();
    nt_test_frame_open();
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_vertex_input_count());
    draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_vertex_input_create_count());
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
}

/* Mesh slot reuse must not alias the stale vertex input: the versions table
 * stores the full generation-checked handle. */
void test_vertex_input_survives_mesh_slot_reuse(void) {
    nt_mesh_t mesh_a = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh_a, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_a)}};

    nt_gfx_fake_reset();
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());

    /* Deactivation destroys the mesh buffers; the cascade kills the vi. */
    nt_test_frame_close();
    nt_gfx_deactivate_mesh(mesh_a.id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_vertex_input_count());
    nt_test_frame_open();

    nt_mesh_t mesh_b = create_test_mesh(); /* reuses the freed pool slot */
    TEST_ASSERT_EQUAL_UINT32(nt_pool_slot_index(mesh_a.id), nt_pool_slot_index(mesh_b.id));
    *nt_mesh_comp_handle(e) = mesh_b;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh_b);

    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_vertex_input_create_count()); /* fresh vi, not the stale one */
}

/* Exceeding max_mesh_layouts asserts (crash-early over silent eviction). */
void test_vertex_input_versions_overflow_asserts(void) {
    nt_test_frame_close();
    nt_mesh_renderer_shutdown();
    nt_mesh_renderer_desc_t small = {.max_pipelines = 8, .max_mesh_layouts = 2};
    TEST_ASSERT_EQUAL_INT(0, (int)nt_mesh_renderer_init(&small));
    nt_test_frame_open();

    nt_mesh_t mesh = create_test_mesh();
    nt_program_t shared = create_test_program();
    nt_render_item_t item;
    for (uint8_t loc = 0; loc < 2; loc++) {
        nt_material_t mat = create_test_material_with_attr(shared, "position", loc, nt_blend_opaque());
        nt_entity_t e = create_test_entity(mesh, mat);
        item = (nt_render_item_t){.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};
        draw_list(&item, 1);
    }
    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_vertex_input_count());

    nt_material_t mat3 = create_test_material_with_attr(shared, "position", 2, nt_blend_opaque());
    nt_entity_t e3 = create_test_entity(mesh, mat3);
    item = (nt_render_item_t){.sort_key = 0, .entity = e3.id, .batch_key = nt_mesh_renderer_batch_key(mat3, mesh)};
    NT_TEST_EXPECT_ASSERT(draw_list(&item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "versions exhausted"));
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
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

    /* Draw to populate cache */
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());

    /* Restore GPU context */
    nt_test_frame_close();
    nt_mesh_renderer_restore_gpu();
    nt_test_frame_open();
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_pipeline_cache_count());

    /* Subsequent draw should still work (rebuilds cache lazily) */
    draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
}

/* ---- Test 10: stream -> vertex type mapping is total over all stream types ---- */

/* The restore contract is "every ACTIVE renderer": a game restores all
 * renderers unconditionally, including ones it never initialized. */
void test_restore_on_inactive_renderer_does_nothing(void) {
    nt_test_frame_close();
    nt_mesh_renderer_shutdown();
    TEST_ASSERT_FALSE(nt_mesh_renderer_test_initialized());

    nt_mesh_renderer_restore_gpu();

    TEST_ASSERT_FALSE(nt_mesh_renderer_test_initialized());
    nt_test_frame_open();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_stream_to_vertex_type_total(void) {
    TEST_ASSERT_EQUAL(NT_VERTEX_FLOAT, nt_renderer_stream_to_vertex_type(NT_STREAM_FLOAT32));
    TEST_ASSERT_EQUAL(NT_VERTEX_HALF, nt_renderer_stream_to_vertex_type(NT_STREAM_FLOAT16));
    TEST_ASSERT_EQUAL(NT_VERTEX_INT16, nt_renderer_stream_to_vertex_type(NT_STREAM_INT16));
    TEST_ASSERT_EQUAL(NT_VERTEX_UINT16, nt_renderer_stream_to_vertex_type(NT_STREAM_UINT16));
    TEST_ASSERT_EQUAL(NT_VERTEX_INT8, nt_renderer_stream_to_vertex_type(NT_STREAM_INT8));
    TEST_ASSERT_EQUAL(NT_VERTEX_UINT8, nt_renderer_stream_to_vertex_type(NT_STREAM_UINT8));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_vertex_type_sizes(void) {
    TEST_ASSERT_EQUAL_UINT16(4, nt_vertex_type_size(NT_VERTEX_FLOAT));
    TEST_ASSERT_EQUAL_UINT16(2, nt_vertex_type_size(NT_VERTEX_HALF));
    TEST_ASSERT_EQUAL_UINT16(1, nt_vertex_type_size(NT_VERTEX_UINT8));
    TEST_ASSERT_EQUAL_UINT16(1, nt_vertex_type_size(NT_VERTEX_INT8));
    TEST_ASSERT_EQUAL_UINT16(2, nt_vertex_type_size(NT_VERTEX_UINT16));
    TEST_ASSERT_EQUAL_UINT16(2, nt_vertex_type_size(NT_VERTEX_INT16));
}

/* ---- Core draw and draw_list recording ---- */

/* The core draws caller-packed instances with no entity component, and one allocation serves
 * any number of passes (shadow cascades): the frame storage uploads once, before the first draw. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_core_draw_reuses_one_allocation_across_passes(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 7, 8, 9, 1};

    begin_storage_frame();
    mark_draws();
    const uint32_t updates = nt_gfx_fake_update_buffer_count();
    (void)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 16, 4, &(uint32_t){0}); /* another renderer's range: ours does not start at 0 */
    uint32_t offset = 0;
    nt_mesh_instance_t *instances = (nt_mesh_instance_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 2U * sizeof(nt_mesh_instance_t), 4, &offset);
    for (uint32_t i = 0; i < 2; i++) {
        nt_mesh_instance_world_rows(instances[i].world_rows, world);
        instances[i].color = NT_RGBA8(255, 0, 0, 255);
    }
    TEST_ASSERT_EQUAL_UINT32(16, offset);
    for (int pass = 0; pass < 2; pass++) {
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
        nt_mesh_renderer_draw(mesh, mat, NT_GFX_FRAME_VERTEX, offset, 2);
        nt_gfx_end_pass();
    }
    TEST_ASSERT_EQUAL_UINT32(updates, nt_gfx_fake_update_buffer_count()); /* recording writes no buffer */
    nt_test_frame_end();
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(4, drawn_instances());
    TEST_ASSERT_EQUAL_UINT32(offset, nt_gfx_fake_last_instance_offset());
    TEST_ASSERT_EQUAL_UINT32(updates + 1, nt_gfx_fake_update_buffer_count()); /* one upload for both passes */
    nt_test_frame_open();
}

void test_core_draw_asserts_on_zero_count(void) {
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw(create_test_mesh(), create_test_material(), NT_GFX_FRAME_VERTEX, 0, 0));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "count > 0"));
}

/* A core draw records in a pass: outside one, gfx asserts on the first bind. */
void test_core_draw_asserts_outside_a_pass(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    begin_storage_frame();
    uint32_t offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, sizeof(nt_mesh_instance_t), 4, &offset), 0, sizeof(nt_mesh_instance_t));
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw(mesh, mat, NT_GFX_FRAME_VERTEX, offset, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "must be called inside a pass"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

/* The core keeps no state between calls: each call applies its material, so a param changed
 * between two draws of one pass reaches the second. */
void test_core_draw_applies_the_material_every_call(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    begin_storage_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_fake_reset();
    uint32_t offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, sizeof(nt_mesh_instance_t), 4, &offset), 0, sizeof(nt_mesh_instance_t));
    nt_mesh_renderer_draw(mesh, mat, NT_GFX_FRAME_VERTEX, offset, 1);
    const float tint2[4] = {2.0F, 3.0F, 4.0F, 5.0F};
    nt_material_set_param(mat, "u_tint", tint2);
    nt_mesh_renderer_draw(mesh, mat, NT_GFX_FRAME_VERTEX, offset, 1);
    nt_test_frame_next();

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_uniform_vec4_count());
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.last_frame.accepted[NT_GFX_OP_TEXTURE_SET]);
    float last[4];
    nt_gfx_fake_uniform_vec4_value_at(1, last);
    TEST_ASSERT_EQUAL_INT32(5, (int32_t)last[3]);
}

/* The instance rows are the transpose of the affine part of a column-major mat4. */
void test_instance_world_rows_transpose_the_affine_part(void) {
    float world[16];
    for (int i = 0; i < 16; i++) {
        world[i] = (float)i;
    }
    float rows[3][4];
    nt_mesh_instance_world_rows(rows, world);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            TEST_ASSERT_TRUE(rows[r][c] == world[(c * 4) + r]); /* NOLINT -- exact copy */
        }
    }
}

/* A mesh new to its cache slot forces no mid-frame execution: both lists execute and upload
 * once, at end_frame. */
void test_draw_list_of_a_new_mesh_after_a_draw_executes_nothing(void) {
    nt_material_t mat = create_test_material();
    nt_mesh_t mesh_a = create_test_mesh();
    nt_mesh_t mesh_b = create_test_mesh();
    nt_render_item_t a[1] = {{.entity = create_test_entity(mesh_a, mat).id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_a)}};
    nt_render_item_t b[1] = {{.entity = create_test_entity(mesh_b, mat).id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_b)}};

    record_list(a, 1);
    const uint32_t updates = nt_gfx_fake_update_buffer_count();
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, b, 1);
    TEST_ASSERT_EQUAL_UINT32(updates, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(updates + 1, nt_gfx_fake_update_buffer_count());
}

/* A mesh in the slot of a deactivated one finds the cascade-killed vertex inputs in the cache row:
 * dropping them destroys nothing and executes no recorded draw mid-frame. */
void test_draw_list_of_a_mesh_in_a_reused_slot_destroys_nothing(void) {
    nt_material_t mat = create_test_material();
    nt_mesh_t mesh_a = create_test_mesh();
    nt_mesh_t mesh_c = create_test_mesh();
    nt_entity_t e = create_test_entity(mesh_a, mat);
    nt_render_item_t a[1] = {{.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_a)}};
    nt_render_item_t c[1] = {{.entity = create_test_entity(mesh_c, mat).id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_c)}};

    draw_list(a, 1);
    begin_storage_frame();
    nt_gfx_deactivate_mesh(mesh_a.id);
    nt_mesh_t mesh_b = create_test_mesh();
    TEST_ASSERT_EQUAL_UINT32(nt_pool_slot_index(mesh_a.id), nt_pool_slot_index(mesh_b.id));
    *nt_mesh_comp_handle(e) = mesh_b;
    a[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh_b);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, c, 1);
    const uint32_t updates = nt_gfx_fake_update_buffer_count();
    const uint32_t destroys = g_nt_gfx.counters.accepted[NT_GFX_OP_DESTROY];
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, a, 1);
    TEST_ASSERT_EQUAL_UINT32(updates, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_EQUAL_UINT32(destroys, g_nt_gfx.counters.accepted[NT_GFX_OP_DESTROY]);
}

/* draw_list reads the bindings at the call: multipass rebinds the entity's material between
 * two lists of one frame, and each list draws with its own state. */
void test_lists_read_bindings_at_the_call(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t plain = create_test_material();
    nt_material_t relocated = create_test_material_with_attr(create_test_program(), "position", 1, nt_blend_opaque());
    nt_entity_t e = create_test_entity(mesh, plain);
    nt_render_item_t item = {.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(plain, mesh)};

    record_list(&item, 1);
    *nt_material_comp_handle(e) = relocated;
    item.batch_key = nt_mesh_renderer_batch_key(relocated, mesh);
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, &item, 1);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(nt_material_get_info(plain)->program.id, nt_gfx_fake_draw_trace_at(s_draw_mark).program.id);
    TEST_ASSERT_EQUAL_UINT32(nt_material_get_info(relocated)->program.id, nt_gfx_fake_draw_trace_at(s_draw_mark + 1).program.id);
    TEST_ASSERT_EQUAL_UINT32(sizeof(nt_mesh_instance_t), nt_gfx_fake_last_instance_offset()); /* instance blocks align to 4 */
}

/* Two lists with a program change in between: the second binds the new program's pipeline.
 * Each call replays its material; the unit bind is dropped because GL units survive a program change. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_list_after_a_program_change_reapplies_the_texture_set(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_program_t p2 = create_test_tex_program();
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1];
    fill_items(items, &e, &mat, &mesh, 1);

    begin_storage_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_fake_reset();
    mark_draws();
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, items, 1);
    nt_material_set_program(mat, p2);
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, items, 1);
    nt_test_frame_next();

    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(p2.id, nt_gfx_fake_draw_trace_at(s_draw_mark + 1).program.id);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.last_frame.accepted[NT_GFX_OP_TEXTURE_SET]);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_uniform_vec4_count());
}

/* Each list reads material params at the call, so a list drawn twice in one pass replays a param changed in between. */
void test_list_drawn_twice_in_one_pass_reads_current_params(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1];
    fill_items(items, &e, &mat, &mesh, 1);

    begin_storage_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_fake_reset();
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, items, 1);
    const float tint2[4] = {2.0F, 3.0F, 4.0F, 5.0F};
    nt_material_set_param(mat, "u_tint", tint2);
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, items, 1);
    nt_test_frame_next();

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_uniform_vec4_count());
    float last[4];
    nt_gfx_fake_uniform_vec4_value_at(1, last);
    TEST_ASSERT_EQUAL_INT32(5, (int32_t)last[3]);
}

/* A run whose program is not ready is skipped before its allocation: frame storage holds only
 * the drawn runs. */
void test_skipped_run_allocates_nothing(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat_a = create_test_material();
    nt_material_t mat_b = create_test_material();
    nt_material_t not_ready = create_test_material();
    nt_material_set_program(not_ready, NT_PROGRAM_INVALID);
    nt_render_item_t items[3] = {
        {.entity = create_test_entity(mesh, not_ready).id, .batch_key = nt_mesh_renderer_batch_key(not_ready, mesh)},
        {.entity = create_test_entity(mesh, mat_a).id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.entity = create_test_entity(mesh, mat_b).id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    record_list(items, 3);
    TEST_ASSERT_EQUAL_UINT32(2U * sizeof(nt_mesh_instance_t), g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
}

/* ---- main ---- */

/* A loss during restore can leave meshes and materials 0; draws on a lost context return before checking them. */
static void test_draws_on_a_lost_context_skip_their_handle_checks(void) {
    nt_gfx_fake_set_context_lost(true);
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8}); /* the failed create latches the loss */
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    const nt_render_item_t item = {.entity = 0xFFFFU};
    nt_mesh_renderer_draw((nt_mesh_t){0}, (nt_material_t){0}, NT_GFX_FRAME_VERTEX, 0, 1);
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, &item, 1);
    nt_gfx_fake_set_context_lost(false);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_shutdown);
    RUN_TEST(test_draw_list_empty);
    RUN_TEST(test_draw_list_null_items_asserts_when_nonempty);
    RUN_TEST(test_unready_program_skips_until_a_ready_program_is_assigned);
    RUN_TEST(test_batch_key_packs_material_and_mesh_slots);
    RUN_TEST(test_batch_key_ignores_generation_bits);
    RUN_TEST(test_batch_key_distinguishes_old_hash_collision);
    RUN_TEST(test_batch_key_supports_max_slots);
    RUN_TEST(test_draw_list_single_item);
    RUN_TEST(test_mesh_renderer_forwards_material_blend_state);
    RUN_TEST(test_draw_list_same_material_mesh_batching);
    RUN_TEST(test_draw_list_skips_a_not_ready_run_without_packing_it);
    RUN_TEST(test_draw_list_skips_a_run_whose_program_was_destroyed);
    RUN_TEST(test_draw_list_skips_a_linking_program_until_its_link_finishes);
    RUN_TEST(test_reset_drops_cached_pipelines);
    RUN_TEST(test_draw_list_different_materials);
    RUN_TEST(test_draw_list_alternating_materials);
    RUN_TEST(test_pipeline_cache_reuse);
    RUN_TEST(test_pipeline_cache_different_layouts);
    RUN_TEST(test_neighbouring_programs_one_cull_step_apart_get_their_own_pipelines);
    RUN_TEST(test_runs_of_one_list_pack_each_entity_world_and_color);
    RUN_TEST(test_draw_list_asserts_on_an_item_without_drawable);
    RUN_TEST(test_draw_list_asserts_on_an_item_without_transform);
    RUN_TEST(test_mesh_vertex_input_key_one_step_changes_split_vertex_inputs_not_pipelines);
    RUN_TEST(test_declared_sampler_without_a_resolved_texture_asserts);
    RUN_TEST(test_texture_published_after_material_create_binds_at_next_draw);
    RUN_TEST(test_declared_sampler_unknown_to_the_program_is_ignored);
    RUN_TEST(test_material_missing_a_program_sampler_asserts);
    RUN_TEST(test_texture_lands_on_the_program_sampler_unit);
    RUN_TEST(test_state_same_material_three_meshes);
    RUN_TEST(test_state_three_materials_same_mesh);
    RUN_TEST(test_state_render_state_split);
    RUN_TEST(test_state_runtime_set_param_between_calls);
    RUN_TEST(test_state_program_replaced_between_calls);
    RUN_TEST(test_state_texture_sampler_transitions);
    RUN_TEST(test_state_override_binds_one_sampler_per_texture_change);
    RUN_TEST(test_state_distinct_textures_a_b_a);
    RUN_TEST(test_state_skip_mid_list_resolves_next_run);
    RUN_TEST(test_state_pipeline_failure_mid_list_rebinds_next_run);
    RUN_TEST(test_state_same_tex_same_sampler_diff_params);
    RUN_TEST(test_pipeline_cache_skips_failed_pipeline);
    RUN_TEST(test_pipeline_cache_shared_program_collapses);
    RUN_TEST(test_pipeline_cache_different_material_attr_maps);
    RUN_TEST(test_vertex_input_reused_across_draw_list_calls);
    RUN_TEST(test_vertex_input_distinct_per_mesh);
    RUN_TEST(test_vertex_input_shared_for_same_derived_layout);
    RUN_TEST(test_vertex_input_empty_derived_layout_draws);
    RUN_TEST(test_bufferless_vertex_input_shared_and_slot_reuse_destroys_nothing);
    RUN_TEST(test_empty_layout_with_index_buffer_is_per_mesh);
    RUN_TEST(test_bufferless_vertex_input_recreated_after_restore);
    RUN_TEST(test_vertex_input_survives_mesh_slot_reuse);
    RUN_TEST(test_vertex_input_versions_overflow_asserts);
    RUN_TEST(test_restore_gpu);
    RUN_TEST(test_restore_on_inactive_renderer_does_nothing);
    /* Stream format mapping */
    RUN_TEST(test_stream_to_vertex_type_total);
    RUN_TEST(test_vertex_type_sizes);

    RUN_TEST(test_core_draw_reuses_one_allocation_across_passes);
    RUN_TEST(test_core_draw_asserts_on_zero_count);
    RUN_TEST(test_instance_world_rows_transpose_the_affine_part);
    RUN_TEST(test_draw_list_of_a_new_mesh_after_a_draw_executes_nothing);
    RUN_TEST(test_draw_list_of_a_mesh_in_a_reused_slot_destroys_nothing);
    RUN_TEST(test_lists_read_bindings_at_the_call);
    RUN_TEST(test_list_after_a_program_change_reapplies_the_texture_set);
    RUN_TEST(test_list_drawn_twice_in_one_pass_reads_current_params);
    RUN_TEST(test_skipped_run_allocates_nothing);
    RUN_TEST(test_core_draw_asserts_outside_a_pass);
    RUN_TEST(test_core_draw_applies_the_material_every_call);
    RUN_TEST(test_draws_on_a_lost_context_skip_their_handle_checks);
    RUN_TEST(test_draw_list_packs_into_its_stream);
    RUN_TEST(test_core_draw_asserts_on_instances_outside_the_stream);
    return UNITY_END();
}
