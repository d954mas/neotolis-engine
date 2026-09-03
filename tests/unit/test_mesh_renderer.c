#include "test_helpers/nt_gfx_fake.h"
/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <string.h>

/* clang-format off */
/* NT_TEST_ACCESS defined via CMake target_compile_definitions */
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

static uint32_t s_program_warnings;

static void capture_program_warning(nt_log_level_t level, const char *domain, const char *message, void *user) {
    (void)user;
    if (level == NT_LOG_LEVEL_WARN && strcmp(domain, "mesh_renderer") == 0 && strstr(message, "program is not ready") != NULL) {
        s_program_warnings++;
    }
}

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

static nt_program_t create_test_sampler_program(const char *const *names, uint8_t count) { return nt_gfx_fake_make_program(names, count); }

static nt_program_t create_test_tex_program(void) { return create_test_sampler_program((const char *const[]){"u_tex"}, 1); }

static nt_material_t create_test_material_with_attr(nt_program_t program, nt_color_mode_t color_mode, const char *stream_name, uint8_t location, nt_blend_state_t blend) {

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
    desc.color_mode = color_mode;
    desc.label = "test_material";

    nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

static nt_material_t create_test_material_ex(nt_color_mode_t color_mode) { return create_test_material_with_attr(create_test_program(), color_mode, "position", 0, nt_blend_opaque()); }

static nt_material_t create_test_material(void) { return create_test_material_ex(NT_COLOR_MODE_NONE); }

static nt_material_t create_test_material_with_blend(nt_blend_state_t blend) { return create_test_material_with_attr(create_test_program(), NT_COLOR_MODE_NONE, "position", 0, blend); }

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
    nt_material_step();
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
    nt_drawable_comp_set_color(e, 1.0F, 1.0F, 1.0F, 1.0F);

    return e;
}

/* ---- Unity setUp / tearDown ---- */

void setUp(void) {
    s_program_warnings = 0;
    memset(s_test_tex_res, 0, sizeof(s_test_tex_res));
    s_test_tex_pack_created = false;
    nt_log_add_sink(capture_program_warning, NULL);
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&(nt_gfx_desc_t){
        .max_shaders = 32,
        .max_programs = 64,
        .max_pipelines = 64,
        .max_buffers = 256,
        .max_textures = 32,
        .max_meshes = 32,
        .max_vertex_inputs = TEST_MAX_VERTEX_INPUTS,
        .max_render_targets = 16,
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
}

void tearDown(void) {
    nt_log_remove_sink(capture_program_warning, NULL);
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

void test_init_retries_after_buffer_creation_failure(void) {
    nt_mesh_renderer_shutdown();
    nt_mesh_renderer_desc_t desc = {.max_instances = 2, .max_pipelines = 2, .max_mesh_layouts = 2};
    nt_gfx_fake_fail_buffer_creates(1);
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_mesh_renderer_init(&desc));
    TEST_ASSERT_FALSE(nt_mesh_renderer_test_initialized());
    TEST_ASSERT_EQUAL(NT_OK, nt_mesh_renderer_restore_gpu());
    TEST_ASSERT_FALSE(nt_mesh_renderer_test_initialized());

    TEST_ASSERT_EQUAL(NT_OK, nt_mesh_renderer_init(&desc));
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t entity = create_test_entity(mesh, mat);
    nt_render_item_t item = {.entity = entity.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};
    nt_mesh_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
}

/* ---- Test 2: draw_list with count=0 is a no-op ---- */

void test_draw_list_empty(void) {
    nt_mesh_renderer_draw_list(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_draw_call_count());
}

void test_draw_list_null_items_asserts_when_nonempty(void) { NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(NULL, 1)); }

void test_unready_program_warns_once_and_rearms_after_pipeline_creation(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_with_attr(NT_PROGRAM_INVALID, NT_COLOR_MODE_NONE, "position", 0, nt_blend_opaque());
    nt_entity_t entity = create_test_entity(mesh, mat);
    nt_render_item_t item = {.entity = entity.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};

    nt_mesh_renderer_draw_list(&item, 1);
    nt_mesh_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, s_program_warnings);

    nt_program_t program = create_test_program();
    nt_material_set_program(mat, program);
    nt_mesh_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, s_program_warnings);

    nt_gfx_destroy_program(program);
    nt_mesh_renderer_draw_list(&item, 1);
    nt_mesh_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(2, s_program_warnings);
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

    nt_mesh_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_instance_total());
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

    nt_mesh_renderer_draw_list(&item, 1);

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

    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_instance_total());
}

/* Skipping an unready run must still advance the instance offset for the next ready run. */
void test_draw_list_skips_a_not_ready_run_and_offsets_the_next(void) {
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

    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_fake_last_update_buffer_offset() + NT_INSTANCE_STRIDE_NONE, nt_gfx_fake_last_instance_offset());
}

/* The restore window: the game destroyed its program and the material still
 * names it. The gate asks liveness, not assignment, so the run is skipped --
 * asking assignment here would reach make_pipeline's readiness assert. */
void test_draw_list_skips_a_run_whose_program_was_destroyed(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_gfx_destroy_program(nt_material_get_info(mat)->program);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

    nt_mesh_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_draw_call_count());
}

/* Same reset contract as the sprite renderer: a cached pipeline borrows the
 * material's program, so it must not survive into the next epoch. */
void test_reset_drops_cached_pipelines(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());

    nt_mesh_renderer_restore_gpu();
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

    nt_gfx_test_draw_trace_reset(true);
    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_draw_call_count());
    /* Creating B's pipeline mid-list must not disturb the state A already bound. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_gfx_test_draw_trace_at(0).pipeline.id, nt_gfx_test_draw_trace_at(1).pipeline.id);
    nt_gfx_test_draw_trace_reset(false);
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
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

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
    items[0].batch_key = nt_mesh_renderer_batch_key(mat_a, mesh);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_mesh_renderer_batch_key(mat_b, mesh);

    nt_mesh_renderer_draw_list(items, 2);

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
    nt_material_step();

    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    nt_gfx_test_draw_trace_reset(true);
    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(p0.id, nt_gfx_test_draw_trace_at(0).program.id);
    TEST_ASSERT_EQUAL_UINT32(p1.id, nt_gfx_test_draw_trace_at(1).program.id);
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
    nt_material_step();

    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_EQUAL_UINT32(0, info->resolved_tex[0]);

    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(items, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "resolved_tex"));
}

/* The declared name is what selects the unit, so a material whose program has no such
 * sampler simply skips the slot -- and the program's empty interface stays covered. */
void test_declared_sampler_unknown_to_the_program_is_ignored(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_gfx_fake_reset();
    nt_mesh_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
}

/* A material that leaves one of its program's samplers undeclared would sample
 * whatever the previous material left on that unit. */
void test_material_missing_a_program_sampler_asserts(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t two = create_test_sampler_program((const char *const[]){"u_tex", "u_second"}, 2);
    nt_material_t mat = create_test_material_textured(two, nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(items, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "sampler_mask"));
}

/* The texture goes to the unit the link assigned, not to the material slot index. */
void test_texture_lands_on_the_program_sampler_unit(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t second_unit = create_test_sampler_program((const char *const[]){"u_other", "u_tex"}, 2);
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
    nt_material_step();

    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_gfx_fake_reset();
    nt_mesh_renderer_draw_list(items, 1);

    /* Slot 0 names u_tex, which the program put on unit 1; slot 1 names u_other, unit 0. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(test_texture(0))}), nt_gfx_fake_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_slot_at(0));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(test_texture(1))}), nt_gfx_fake_bound_texture_at(1));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_slot_at(1));
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
    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bind_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_sampler_count());
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
    nt_mesh_renderer_draw_list(items, 3);

    /* Same program + same render state => one pipeline; same derived layout => one VI. */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_uniform_vec4_count());
    /* One bind per material transition; the backend GL cache drops the repeats. */
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bind_sampler_count());
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
    nt_gfx_test_draw_trace_reset(true);
    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_pipeline_create_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_test_draw_trace_count());
    const uint32_t pip_a = nt_gfx_test_draw_trace_at(0).pipeline.id;
    const uint32_t pip_b = nt_gfx_test_draw_trace_at(1).pipeline.id;
    TEST_ASSERT_NOT_EQUAL_UINT32(pip_a, pip_b);
    TEST_ASSERT_EQUAL_UINT32(pip_a, nt_gfx_test_draw_trace_at(2).pipeline.id);
    nt_gfx_test_draw_trace_reset(false);
}

void test_state_runtime_set_param_between_calls(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1];
    fill_items(items, &e, &mat, &mesh, 1);

    nt_gfx_fake_reset();
    nt_mesh_renderer_draw_list(items, 1);

    const float tint2[4] = {2.0F, 3.0F, 4.0F, 5.0F};
    nt_material_set_param(mat, "u_tint", tint2);
    nt_mesh_renderer_draw_list(items, 1);

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
    nt_gfx_test_draw_trace_reset(true);
    nt_mesh_renderer_draw_list(items, 1);

    nt_program_t p2 = create_test_tex_program();
    nt_material_set_program(mat, p2);
    nt_mesh_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_test_draw_trace_count());
    const nt_gfx_test_draw_t first = nt_gfx_test_draw_trace_at(0);
    const nt_gfx_test_draw_t second = nt_gfx_test_draw_trace_at(1);
    TEST_ASSERT_EQUAL_UINT32(p2.id, second.program.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(first.pipeline.id, second.pipeline.id);
    /* Bound state is call-scoped, so the new program's unit is filled again. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    nt_gfx_test_draw_trace_reset(false);
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
    nt_mesh_renderer_draw_list(items, 3);

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
    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_draw_call_count());
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
    nt_mesh_renderer_draw_list(items, 3);

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
    nt_gfx_test_draw_trace_reset(true);
    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_draw_trace_at(0).pipeline.id, nt_gfx_test_draw_trace_at(1).pipeline.id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());
    nt_gfx_test_draw_trace_reset(false);
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
    nt_mesh_renderer_draw_list(items, 1);
    nt_gfx_fake_reset();
    nt_gfx_test_draw_trace_reset(true);
    nt_gfx_fake_fail_next_pipeline_create();
    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_draw_trace_at(0).pipeline.id, nt_gfx_test_draw_trace_at(1).pipeline.id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());
    nt_gfx_test_draw_trace_reset(false);
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
    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bind_sampler_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_uniform_vec4_count());
}

/* A chunk split must not replay material or vertex-input state for a run that
 * continues across the boundary. */
void test_state_chunk_boundary_same_run(void) {
    nt_mesh_renderer_shutdown();
    nt_mesh_renderer_desc_t rdesc = nt_mesh_renderer_desc_defaults();
    rdesc.max_instances = 2;
    TEST_ASSERT_EQUAL(NT_OK, nt_mesh_renderer_init(&rdesc));

    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_textured(create_test_tex_program(), nt_blend_opaque(), NT_SAMPLER_DEFAULT);
    nt_material_t mats[3] = {mat, mat, mat};
    nt_mesh_t meshes[3] = {mesh, mesh, mesh};
    nt_entity_t entities[3] = {create_test_entity(mesh, mat), create_test_entity(mesh, mat), create_test_entity(mesh, mat)};

    nt_render_item_t items[3];
    fill_items(items, entities, mats, meshes, 3);

    nt_gfx_fake_reset();
    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());
}
// #endregion

/* Caching a failed pipeline would prevent a later frame from retrying creation. */
void test_pipeline_cache_skips_failed_pipeline(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_gfx_fake_fail_next_pipeline_create();
    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_draw_call_count());

    /* Next frame retries and succeeds. */
    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
}

/* Manual dedup is the whole point of an explicit program: two materials on one
 * program, same layout and state, must collapse to a single pipeline. */
void test_pipeline_cache_shared_program_collapses(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t shared = create_test_program();
    nt_material_t mat_a = create_test_material_with_attr(shared, NT_COLOR_MODE_NONE, "position", 0, nt_blend_opaque());
    nt_material_t mat_b = create_test_material_with_attr(shared, NT_COLOR_MODE_NONE, "position", 0, nt_blend_opaque());
    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
}

/* color_mode is vertex-input identity (it selects the instance layout), not
 * pipeline identity: one program with three color modes is one pipeline. */
void test_color_modes_on_one_program_share_a_pipeline_and_split_vertex_inputs(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t shared = create_test_program();
    nt_material_t mats[3] = {
        create_test_material_with_attr(shared, NT_COLOR_MODE_NONE, "position", 0, nt_blend_opaque()),
        create_test_material_with_attr(shared, NT_COLOR_MODE_RGBA8, "position", 0, nt_blend_opaque()),
        create_test_material_with_attr(shared, NT_COLOR_MODE_FLOAT4, "position", 0, nt_blend_opaque()),
    };
    nt_render_item_t items[3];
    for (uint32_t i = 0; i < 3; i++) {
        nt_entity_t e = create_test_entity(mesh, mats[i]);
        items[i] = (nt_render_item_t){.sort_key = i, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mats[i], mesh)};
    }

    nt_gfx_test_draw_trace_reset(true);
    nt_mesh_renderer_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_draw_trace_at(0).pipeline.id, nt_gfx_test_draw_trace_at(2).pipeline.id);
}

/* The vertex-input key carries a presence bit per stream: mapping one more
 * stream splits the vertex input even though every mapped location is unchanged. */
void test_mapping_an_extra_stream_splits_vertex_inputs_not_pipelines(void) {
    nt_mesh_t mesh = create_test_mesh_two_streams();
    nt_program_t shared = create_test_program();
    nt_material_t mat_a = create_test_material_with_attr(shared, NT_COLOR_MODE_NONE, "position", 0, nt_blend_opaque());

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
    nt_material_t mat_b = nt_material_create(&desc);
    nt_material_step();

    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_vertex_input_count());
}

/* Equal program/state share a pipeline; distinct attr_maps derive separate VIs. */
void test_pipeline_cache_different_material_attr_maps(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t shared = create_test_program();
    nt_material_t mat_a = create_test_material_with_attr(shared, NT_COLOR_MODE_NONE, "position", 0, nt_blend_opaque());
    nt_material_t mat_b = create_test_material_with_attr(shared, NT_COLOR_MODE_NONE, "position", 1, nt_blend_opaque());
    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    nt_mesh_renderer_draw_list(items, 2);

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
    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_vertex_input_create_count());
    nt_mesh_renderer_draw_list(items, 1);
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

    nt_mesh_renderer_draw_list(items, 2);

    /* One material state: one pipeline; two meshes: two vertex inputs. */
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_vertex_input_count());
}

/* An attr_map entry matching none of the mesh's streams must not split the
 * vertex input: the DERIVED layout (streams x attr_map) is the identity. */
void test_vertex_input_shared_for_same_derived_layout(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_program_t shared = create_test_program();
    nt_material_t mat_a = create_test_material_with_attr(shared, NT_COLOR_MODE_NONE, "position", 0, nt_blend_opaque());

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
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.label = "extra_attr_material";
    nt_material_t mat_b = nt_material_create(&desc);
    nt_material_step();

    nt_entity_t e0 = create_test_entity(mesh, mat_a);
    nt_entity_t e1 = create_test_entity(mesh, mat_b);
    nt_render_item_t items[2] = {
        {.sort_key = 0, .entity = e0.id, .batch_key = nt_mesh_renderer_batch_key(mat_a, mesh)},
        {.sort_key = 1, .entity = e1.id, .batch_key = nt_mesh_renderer_batch_key(mat_b, mesh)},
    };

    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
}

/* A material mapping none of the mesh's streams derives an empty layout: the
 * attribute-less gl_VertexID path draws through a vertex input with no VBO. */
void test_vertex_input_empty_derived_layout_draws(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_with_attr(create_test_program(), NT_COLOR_MODE_NONE, "not_a_mesh_stream", 0, nt_blend_opaque());
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)}};

    nt_mesh_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
}

/* Bufferless versions survive buffer destruction and need row-wide cleanup. */
void test_bufferless_vertex_input_purged_on_mesh_slot_reuse(void) {
    nt_program_t program = create_test_program();
    nt_material_t mat = create_test_material_with_attr(program, NT_COLOR_MODE_NONE, "not_a_mesh_stream", 0, nt_blend_opaque());
    nt_material_t colored = create_test_material_with_attr(program, NT_COLOR_MODE_RGBA8, "not_a_mesh_stream", 0, nt_blend_opaque());
    nt_mesh_t mesh = create_test_mesh_nonindexed();
    nt_entity_t e = create_test_entity(mesh, mat);
    nt_entity_t e_colored = create_test_entity(mesh, colored);
    nt_mesh_t neighbor = create_test_mesh_nonindexed();
    nt_entity_t e_neighbor = create_test_entity(neighbor, mat);
    nt_render_item_t neighbor_item = {.entity = e_neighbor.id, .batch_key = nt_mesh_renderer_batch_key(mat, neighbor)};
    nt_gfx_fake_reset();
    nt_mesh_renderer_draw_list(&neighbor_item, 1);

    for (int cycle = 0; cycle < 6; cycle++) {
        nt_render_item_t items[2] = {
            {.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)},
            {.entity = e_colored.id, .batch_key = nt_mesh_renderer_batch_key(colored, mesh)},
        };
        nt_mesh_renderer_draw_list(items, 1);
        TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_vertex_input_count());
        nt_mesh_renderer_draw_list(items, 2);
        nt_mesh_renderer_draw_list(&neighbor_item, 1);
        TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_vertex_input_count());
        TEST_ASSERT_EQUAL_UINT32(1 + (2 * (cycle + 1)), nt_gfx_fake_vertex_input_create_count());
        const uint32_t old_id = mesh.id;
        nt_gfx_deactivate_mesh(mesh.id);
        mesh = create_test_mesh_nonindexed(); /* reuses the freed pool slot */
        TEST_ASSERT_EQUAL_UINT32(nt_pool_slot_index(old_id), nt_pool_slot_index(mesh.id));
        TEST_ASSERT_NOT_EQUAL(old_id, mesh.id);
        *nt_mesh_comp_handle(e) = mesh;
        *nt_mesh_comp_handle(e_colored) = mesh;
    }

    /* Two cached versions and the neighbor own three slots; all others must be free. */
    for (uint32_t i = 3; i < TEST_MAX_VERTEX_INPUTS; i++) {
        nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
        TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    }
}

/* Mesh slot reuse must not alias the stale vertex input: the versions table
 * stores the full generation-checked handle. */
void test_vertex_input_survives_mesh_slot_reuse(void) {
    nt_mesh_t mesh_a = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh_a, mat);
    nt_render_item_t items[1] = {{.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh_a)}};

    nt_gfx_fake_reset();
    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());

    /* Deactivation destroys the mesh buffers; the cascade kills the vi. */
    nt_gfx_deactivate_mesh(mesh_a.id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_vertex_input_count());

    nt_mesh_t mesh_b = create_test_mesh(); /* reuses the freed pool slot */
    TEST_ASSERT_EQUAL_UINT32(nt_pool_slot_index(mesh_a.id), nt_pool_slot_index(mesh_b.id));
    *nt_mesh_comp_handle(e) = mesh_b;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh_b);

    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_vertex_input_create_count()); /* fresh vi, not the stale one */
}

/* Exceeding max_mesh_layouts asserts (crash-early over silent eviction). */
void test_vertex_input_versions_overflow_asserts(void) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_mesh_renderer_shutdown();
    nt_mesh_renderer_desc_t small = {.max_instances = 4, .max_pipelines = 8, .max_mesh_layouts = 2};
    TEST_ASSERT_EQUAL_INT(0, (int)nt_mesh_renderer_init(&small));
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_mesh_t mesh = create_test_mesh();
    nt_program_t shared = create_test_program();
    nt_render_item_t item;
    for (uint8_t loc = 0; loc < 2; loc++) {
        nt_material_t mat = create_test_material_with_attr(shared, NT_COLOR_MODE_NONE, "position", loc, nt_blend_opaque());
        nt_entity_t e = create_test_entity(mesh, mat);
        item = (nt_render_item_t){.sort_key = 0, .entity = e.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};
        nt_mesh_renderer_draw_list(&item, 1);
    }
    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_vertex_input_count());

    nt_material_t mat3 = create_test_material_with_attr(shared, NT_COLOR_MODE_NONE, "position", 2, nt_blend_opaque());
    nt_entity_t e3 = create_test_entity(mesh, mat3);
    item = (nt_render_item_t){.sort_key = 0, .entity = e3.id, .batch_key = nt_mesh_renderer_batch_key(mat3, mesh)};
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(&item, 1));
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

void test_restore_gpu_retries_after_context_loss(void) {
    nt_mesh_renderer_shutdown();
    nt_mesh_renderer_desc_t desc = {.max_instances = 2, .max_pipelines = 2, .max_mesh_layouts = 2};
    TEST_ASSERT_EQUAL(NT_OK, nt_mesh_renderer_init(&desc));
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_ex(NT_COLOR_MODE_FLOAT4);
    nt_entity_t entity = create_test_entity(mesh, mat);
    nt_render_item_t item = {.entity = entity.id, .batch_key = nt_mesh_renderer_batch_key(mat, mesh)};
    nt_mesh_renderer_draw_list(&item, 1);
    TEST_ASSERT_GREATER_THAN_UINT32(0, nt_mesh_renderer_test_ring_cursor());

    nt_gfx_fake_set_context_lost(true);
    nt_result_t result = nt_mesh_renderer_restore_gpu();
    nt_gfx_fake_set_context_lost(false);

    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, result);
    TEST_ASSERT_TRUE(nt_mesh_renderer_test_initialized());
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_ring_cursor());
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_mesh_renderer_test_vertex_input_count());

    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(&item, 1));
    TEST_ASSERT_EQUAL(NT_OK, nt_mesh_renderer_restore_gpu());
    nt_render_item_t items[3] = {item, item, item};
    nt_mesh_renderer_draw_list(items, 3);
    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_instance_total());
    TEST_ASSERT_EQUAL_UINT32(NT_INSTANCE_STRIDE_MAX, nt_mesh_renderer_test_ring_cursor());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_vertex_input_count());
}

/* ---- Test 10: stream -> vertex type mapping is total over all stream types ---- */

/* The restore contract is "every ACTIVE renderer". Without the entry guard this
 * re-inits from a zeroed desc and traps on max_instances == 0, so a game that
 * restores all four renderers unconditionally would abort. */
void test_restore_on_inactive_renderer_does_nothing(void) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_mesh_renderer_shutdown();
    TEST_ASSERT_FALSE(nt_mesh_renderer_test_initialized());

    TEST_ASSERT_EQUAL(NT_OK, nt_mesh_renderer_restore_gpu());

    /* The live assertion: init would have set this. The trap on a zeroed desc
     * aborts before ever reaching here, so it cannot be what pins the guard. */
    TEST_ASSERT_FALSE(nt_mesh_renderer_test_initialized());
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_stream_to_vertex_type_total(void) {
    TEST_ASSERT_EQUAL(NT_VERTEX_FLOAT, nt_stream_to_vertex_type(NT_STREAM_FLOAT32));
    TEST_ASSERT_EQUAL(NT_VERTEX_HALF, nt_stream_to_vertex_type(NT_STREAM_FLOAT16));
    TEST_ASSERT_EQUAL(NT_VERTEX_INT16, nt_stream_to_vertex_type(NT_STREAM_INT16));
    TEST_ASSERT_EQUAL(NT_VERTEX_UINT16, nt_stream_to_vertex_type(NT_STREAM_UINT16));
    TEST_ASSERT_EQUAL(NT_VERTEX_INT8, nt_stream_to_vertex_type(NT_STREAM_INT8));
    TEST_ASSERT_EQUAL(NT_VERTEX_UINT8, nt_stream_to_vertex_type(NT_STREAM_UINT8));
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

/* ---- Test: FLOAT4 color mode produces valid draw ---- */

void test_draw_list_color_mode_float4(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_ex(NT_COLOR_MODE_FLOAT4);
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

    nt_mesh_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_instance_total());
}

/* ---- Test: RGBA8 color mode produces valid draw ---- */

void test_draw_list_color_mode_rgba8(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_ex(NT_COLOR_MODE_RGBA8);
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

    nt_mesh_renderer_draw_list(items, 1);

    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_mesh_renderer_test_instance_total());
}

/* ---- Test: different color modes produce different pipeline cache entries ---- */

void test_pipeline_cache_different_color_modes(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat_none = create_test_material_ex(NT_COLOR_MODE_NONE);
    nt_material_t mat_float4 = create_test_material_ex(NT_COLOR_MODE_FLOAT4);

    nt_entity_t e0 = create_test_entity(mesh, mat_none);
    nt_entity_t e1 = create_test_entity(mesh, mat_float4);

    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat_none, mesh);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_mesh_renderer_batch_key(mat_float4, mesh);

    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_pipeline_cache_count());
}

/* ---- Test: mixed color modes within a single draw_list call ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_draw_list_mixed_color_modes(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat_none = create_test_material_ex(NT_COLOR_MODE_NONE);
    nt_material_t mat_float4 = create_test_material_ex(NT_COLOR_MODE_FLOAT4);

    nt_entity_t e0 = create_test_entity(mesh, mat_none);
    nt_entity_t e1 = create_test_entity(mesh, mat_float4);

    nt_render_item_t items[2];
    items[0].sort_key = 0;
    items[0].entity = e0.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat_none, mesh);
    items[1].sort_key = 1;
    items[1].entity = e1.id;
    items[1].batch_key = nt_mesh_renderer_batch_key(mat_float4, mesh);

    nt_mesh_renderer_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_mesh_renderer_test_instance_total());
}

/* ---- Test: mixed color modes with multiple instances per run ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_draw_list_mixed_color_modes_multi_instance(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat_none = create_test_material_ex(NT_COLOR_MODE_NONE);
    nt_material_t mat_rgba8 = create_test_material_ex(NT_COLOR_MODE_RGBA8);
    nt_material_t mat_float4 = create_test_material_ex(NT_COLOR_MODE_FLOAT4);

    /* 3 entities per material = 9 total, 3 runs with different strides */
    nt_entity_t entities[9];
    nt_render_item_t items[9];
    uint32_t idx = 0;

    /* Run 0: 3x NONE (stride 48) */
    for (int i = 0; i < 3; i++) {
        entities[idx] = create_test_entity(mesh, mat_none);
        items[idx].sort_key = idx;
        items[idx].entity = entities[idx].id;
        items[idx].batch_key = nt_mesh_renderer_batch_key(mat_none, mesh);
        idx++;
    }
    /* Run 1: 3x RGBA8 (stride 56) */
    for (int i = 0; i < 3; i++) {
        entities[idx] = create_test_entity(mesh, mat_rgba8);
        items[idx].sort_key = idx;
        items[idx].entity = entities[idx].id;
        items[idx].batch_key = nt_mesh_renderer_batch_key(mat_rgba8, mesh);
        idx++;
    }
    /* Run 2: 3x FLOAT4 (stride 64) */
    for (int i = 0; i < 3; i++) {
        entities[idx] = create_test_entity(mesh, mat_float4);
        items[idx].sort_key = idx;
        items[idx].entity = entities[idx].id;
        items[idx].batch_key = nt_mesh_renderer_batch_key(mat_float4, mesh);
        idx++;
    }

    nt_mesh_renderer_draw_list(items, 9);

    TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_draw_call_count());
    TEST_ASSERT_EQUAL_UINT32(9, nt_mesh_renderer_test_instance_total());
    /* 3 programs (one per material) = 3 pipelines */
    TEST_ASSERT_EQUAL_UINT32(3, nt_mesh_renderer_test_pipeline_cache_count());
    /* Last run's bind offset = upload base + preceding runs (3x NONE + 3x RGBA8) */
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_fake_last_update_buffer_offset() + (3 * NT_INSTANCE_STRIDE_NONE) + (3 * NT_INSTANCE_STRIDE_RGBA8), nt_gfx_fake_last_instance_offset());
}

/* ---- Ring allocation: cursor advances per draw_list call, wraps at capacity ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_ring_cursor_advances_and_wraps(void) {
    /* Tiny buffer so wrap is reachable. FLOAT4 material -> stride equals
     * NT_INSTANCE_STRIDE_MAX, so per-call delta divides capacity exactly and
     * the exact-fit boundary (cursor == capacity must NOT wrap early) is hit. */
    nt_mesh_renderer_shutdown();
    nt_mesh_renderer_desc_t small = {.max_instances = 4, .max_pipelines = 8, .max_mesh_layouts = 4};
    TEST_ASSERT_EQUAL_INT(0, (int)nt_mesh_renderer_init(&small));

    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material_ex(NT_COLOR_MODE_FLOAT4);
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

    nt_mesh_renderer_draw_list(items, 1);
    uint32_t delta = nt_mesh_renderer_test_ring_cursor();
    TEST_ASSERT_EQUAL_UINT32(NT_INSTANCE_STRIDE_MAX, delta); /* FLOAT4 packs at max stride */

    uint32_t capacity = 4U * NT_INSTANCE_STRIDE_MAX;
    uint32_t fit = capacity / delta;
    for (uint32_t i = 1; i < fit; i++) {
        nt_mesh_renderer_draw_list(items, 1);
        TEST_ASSERT_EQUAL_UINT32((i + 1) * delta, nt_mesh_renderer_test_ring_cursor());
    }
    /* Exact fit: cursor sits AT capacity without having wrapped */
    TEST_ASSERT_EQUAL_UINT32(capacity, nt_mesh_renderer_test_ring_cursor());
    /* Buffer full: next call wraps to 0 and writes there */
    nt_mesh_renderer_draw_list(items, 1);
    TEST_ASSERT_EQUAL_UINT32(delta, nt_mesh_renderer_test_ring_cursor());

    /* Restore defaults for tearDown */
    nt_mesh_renderer_shutdown();
    nt_mesh_renderer_desc_t desc = nt_mesh_renderer_desc_defaults();
    nt_mesh_renderer_init(&desc);
}

/* ---- Ring upload base and draw base must agree (stub records both) ---- */

void test_ring_upload_and_draw_base_agree(void) {
    nt_mesh_t mesh = create_test_mesh();
    nt_material_t mat = create_test_material();
    nt_entity_t e = create_test_entity(mesh, mat);

    nt_render_item_t items[1];
    items[0].sort_key = 0;
    items[0].entity = e.id;
    items[0].batch_key = nt_mesh_renderer_batch_key(mat, mesh);

    nt_mesh_renderer_draw_list(items, 1);
    uint32_t upload1 = nt_gfx_fake_last_update_buffer_offset();
    TEST_ASSERT_EQUAL_UINT32(upload1, nt_gfx_fake_last_instance_offset());

    /* Second call must advance BOTH bases together, not just the upload */
    nt_mesh_renderer_draw_list(items, 1);
    uint32_t upload2 = nt_gfx_fake_last_update_buffer_offset();
    TEST_ASSERT_TRUE(upload2 > upload1);
    TEST_ASSERT_EQUAL_UINT32(upload2, nt_gfx_fake_last_instance_offset());
}

/* ---- main ---- */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_shutdown);
    RUN_TEST(test_init_retries_after_buffer_creation_failure);
    RUN_TEST(test_draw_list_empty);
    RUN_TEST(test_draw_list_null_items_asserts_when_nonempty);
    RUN_TEST(test_unready_program_warns_once_and_rearms_after_pipeline_creation);
    RUN_TEST(test_batch_key_packs_material_and_mesh_slots);
    RUN_TEST(test_batch_key_ignores_generation_bits);
    RUN_TEST(test_batch_key_distinguishes_old_hash_collision);
    RUN_TEST(test_batch_key_supports_max_slots);
    RUN_TEST(test_draw_list_single_item);
    RUN_TEST(test_mesh_renderer_forwards_material_blend_state);
    RUN_TEST(test_draw_list_same_material_mesh_batching);
    RUN_TEST(test_draw_list_skips_a_not_ready_run_and_offsets_the_next);
    RUN_TEST(test_draw_list_skips_a_run_whose_program_was_destroyed);
    RUN_TEST(test_reset_drops_cached_pipelines);
    RUN_TEST(test_draw_list_different_materials);
    RUN_TEST(test_draw_list_alternating_materials);
    RUN_TEST(test_pipeline_cache_reuse);
    RUN_TEST(test_pipeline_cache_different_layouts);
    RUN_TEST(test_neighbouring_programs_one_cull_step_apart_get_their_own_pipelines);
    RUN_TEST(test_color_modes_on_one_program_share_a_pipeline_and_split_vertex_inputs);
    RUN_TEST(test_mapping_an_extra_stream_splits_vertex_inputs_not_pipelines);
    RUN_TEST(test_declared_sampler_without_a_resolved_texture_asserts);
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
    RUN_TEST(test_state_chunk_boundary_same_run);
    RUN_TEST(test_pipeline_cache_skips_failed_pipeline);
    RUN_TEST(test_pipeline_cache_shared_program_collapses);
    RUN_TEST(test_pipeline_cache_different_material_attr_maps);
    RUN_TEST(test_vertex_input_reused_across_draw_list_calls);
    RUN_TEST(test_vertex_input_distinct_per_mesh);
    RUN_TEST(test_vertex_input_shared_for_same_derived_layout);
    RUN_TEST(test_vertex_input_empty_derived_layout_draws);
    RUN_TEST(test_bufferless_vertex_input_purged_on_mesh_slot_reuse);
    RUN_TEST(test_vertex_input_survives_mesh_slot_reuse);
    RUN_TEST(test_vertex_input_versions_overflow_asserts);
    RUN_TEST(test_restore_gpu);
    RUN_TEST(test_restore_gpu_retries_after_context_loss);
    /* Color mode tests */
    RUN_TEST(test_draw_list_color_mode_float4);
    RUN_TEST(test_draw_list_color_mode_rgba8);
    RUN_TEST(test_pipeline_cache_different_color_modes);
    RUN_TEST(test_draw_list_mixed_color_modes);
    RUN_TEST(test_draw_list_mixed_color_modes_multi_instance);
    RUN_TEST(test_ring_cursor_advances_and_wraps);
    RUN_TEST(test_ring_upload_and_draw_base_agree);
    RUN_TEST(test_restore_on_inactive_renderer_does_nothing);
    /* Stream format mapping */
    RUN_TEST(test_stream_to_vertex_type_total);
    RUN_TEST(test_vertex_type_sizes);

    return UNITY_END();
}
