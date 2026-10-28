#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/nt_gfx_test_desc.h"
#include "test_helpers/nt_gfx_test_frame.h"
#include "test_helpers/nt_gfx_test_notes.h"

#include <string.h>

/* clang-format off */
#include "color/nt_color.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "nt_vertex_locations.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "log/nt_log.h"
#include "renderers/nt_mesh_renderer.h"
#include "resource/nt_resource.h"
#include "skin_comp/nt_skin_comp.h"
#include "transform_comp/nt_transform_comp.h"
#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"
/* clang-format on */

static nt_instance_layout_t skinned_layout(void) { return nt_gfx_make_instance_layout(&nt_skinned_mesh_instance_layout); }

static nt_mesh_t make_mesh_at(uint8_t location) {
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 36];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *header = (NtMeshAssetHeader *)blob;
    header->magic = NT_MESH_MAGIC;
    header->version = NT_MESH_VERSION;
    header->stream_count = 1;
    header->vertex_count = 3;
    header->vertex_data_size = 36;

    NtStreamDesc *stream = (NtStreamDesc *)(blob + sizeof(*header));
    stream->type = NT_STREAM_FLOAT32;
    stream->count = 3;
    stream->location = location;

    float *vertices = (float *)(blob + sizeof(*header) + sizeof(*stream));
    vertices[3] = 1.0F;
    vertices[7] = 1.0F;
    return (nt_mesh_t){.id = nt_gfx_activate_mesh(blob, sizeof(blob))};
}

static nt_mesh_t make_mesh(void) { return make_mesh_at(NT_LOC_POSITION); }

static nt_mesh_t make_indexed_mesh(void) {
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 36 + 6];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *header = (NtMeshAssetHeader *)blob;
    header->magic = NT_MESH_MAGIC;
    header->version = NT_MESH_VERSION;
    header->stream_count = 1;
    header->index_type = 1;
    header->vertex_count = 3;
    header->index_count = 3;
    header->vertex_data_size = 36;
    header->index_data_size = 6;

    NtStreamDesc *stream = (NtStreamDesc *)(blob + sizeof(*header));
    stream->type = NT_STREAM_FLOAT32;
    stream->count = 3;
    stream->location = NT_LOC_POSITION;

    uint16_t *indices = (uint16_t *)(blob + sizeof(*header) + sizeof(*stream) + 36);
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    return (nt_mesh_t){.id = nt_gfx_activate_mesh(blob, sizeof(blob))};
}

static nt_texture_t make_deformation_texture(void) {
    static const float identity[12] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
    };
    return nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 3,
        .height = 1,
        .data = identity,
        .format = NT_TEXTURE_FORMAT_RGBA32F,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .label = "skin_test_deformation",
    });
}

static bool s_surface_pack_created;

static nt_resource_t make_surface_resource(void) {
    const nt_hash32_t pack_id = nt_hash32_str("skinned_renderer_test_pack");
    const nt_hash64_t resource_id = nt_hash64_str("skinned_renderer_surface");
    if (!s_surface_pack_created) {
        nt_resource_create_pack(pack_id, 2);
        s_surface_pack_created = true;
    }
    static const uint8_t white[4] = {255, 255, 255, 255};
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 1,
        .height = 1,
        .data = white,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .label = "skin_test_surface",
    });
    nt_resource_register(pack_id, resource_id, NT_ASSET_TEXTURE, texture.id);
    nt_resource_t resource = nt_resource_request(resource_id, NT_ASSET_TEXTURE);
    nt_resource_step();
    return resource;
}

/* u_skin_matrices is a draw texture: materials declare only their own textures. */
static nt_material_t make_material_ex(nt_program_t program, nt_resource_t surface) {
    nt_material_create_desc_t desc = {
        .program = program,
        .depth_test = true,
        .depth_write = true,
        .cull_mode = NT_CULL_BACK,
        .label = "skin_test_material",
    };
    if (surface.id != 0) {
        desc.textures[0] = (nt_material_texture_desc_t){.name = "u_surface", .resource = surface};
        desc.texture_count = 1;
    }
    return nt_material_create(&desc);
}

static nt_material_t make_material(nt_program_t program) { return make_material_ex(program, NT_RESOURCE_INVALID); }

static nt_entity_t make_entity(nt_mesh_t mesh, nt_material_t material, nt_deformation_binding_t binding) {
    nt_entity_t entity = nt_entity_create();
    TEST_ASSERT_TRUE(nt_transform_comp_add(entity));
    TEST_ASSERT_TRUE(nt_mesh_comp_add(entity));
    TEST_ASSERT_TRUE(nt_material_comp_add(entity));
    TEST_ASSERT_TRUE(nt_drawable_comp_add(entity));
    TEST_ASSERT_TRUE(nt_skin_comp_add(entity));
    *nt_mesh_comp_handle(entity) = mesh;
    *nt_material_comp_handle(entity) = material;
    *nt_skin_comp_handle(entity) = binding;
    nt_drawable_comp_set_color(entity, 0xFFFFFFFFU);
    nt_transform_comp_update();
    return entity;
}

static nt_render_item_t make_item(nt_entity_t entity, nt_material_t material, nt_mesh_t mesh) {
    return (nt_render_item_t){
        .entity = entity.id,
        .batch_key = nt_mesh_renderer_batch_key(material, mesh),
    };
}

static uint32_t s_draw_mark; /* fake draw trace length when the last draw began */

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

/* Leaves the pass the tests draw in closed and opens the next frame with empty frame storage. */
static void begin_storage_frame(void) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
}

/* Records a single list in a fresh frame's pass; the frame stays open. */
static void record_skinned_list(const nt_render_item_t *items, uint32_t count) {
    nt_test_frame_next();
    s_draw_mark = nt_gfx_fake_draw_trace_count();
    nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, items, count);
}

/* One gfx frame drawing a single list in one pass, executed before it returns. */
static void skinned_draw_list(const nt_render_item_t *items, uint32_t count) {
    record_skinned_list(items, count);
    nt_test_frame_next();
}

void setUp(void) {
    nt_gfx_fake_reset();
    s_surface_pack_created = false;
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&NT_GFX_TEST_DESC(.capture_capacity = 4096U, .max_shaders = 8, .max_programs = 8, .max_pipelines = 16, .max_buffers = 32, .max_textures = 16, .max_meshes = 8,
                                  /* 48 non-mesh inputs + the per-(mesh, stream) vertex inputs of 8 meshes. */
                                  .max_vertex_inputs = 96, .max_render_targets = 4));
    nt_gfx_begin_frame();
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 16});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 16});
    nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = 16});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 16});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 16});
    nt_skin_comp_init(&(nt_skin_comp_desc_t){.capacity = 16});
    nt_material_init(&(nt_material_desc_t){.max_materials = 16});
    nt_mesh_renderer_init();
    nt_test_frame_begin_pass();
    nt_gfx_fake_draw_trace_reset(true);
    s_draw_mark = 0;
}

void tearDown(void) {
    nt_test_frame_teardown();
    nt_mesh_renderer_shutdown();
    nt_material_shutdown();
    nt_skin_comp_shutdown();
    nt_drawable_comp_shutdown();
    nt_material_comp_shutdown();
    nt_mesh_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    nt_resource_shutdown();
    nt_gfx_shutdown();
    nt_hash_shutdown();
}

void test_one_compatible_item_draws_with_its_deformation_texture(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1);
    nt_material_t material = make_material(program);
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = {
        .entity = entity.id,
        .batch_key = nt_mesh_renderer_batch_key(material, mesh),
    };

    skinned_draw_list(&item, 1);

    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(0).instance_count);
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, drawn_instances());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(texture), nt_gfx_fake_bound_texture_at(0));
}

void test_compatible_items_share_one_instanced_draw(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_render_item_t items[3];
    for (uint32_t i = 0; i < 3; i++) {
        nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture, .x0 = (uint16_t)i, .alpha = (float)i * 0.25F});
        items[i] = make_item(entity, material, mesh);
    }

    skinned_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_draw_trace_at(0).instance_count);
}

void test_deformation_texture_splits_runs_but_origins_do_not(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture_a = make_deformation_texture();
    nt_texture_t texture_b = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity_a0 = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_a, .x0 = 1, .y0 = 2});
    nt_entity_t entity_a1 = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_a, .x0 = 7, .y0 = 9, .alpha = 0.5F});
    nt_entity_t entity_b = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_b});
    nt_render_item_t items[3] = {
        make_item(entity_a0, material, mesh),
        make_item(entity_a1, material, mesh),
        make_item(entity_b, material, mesh),
    };

    skinned_draw_list(items, 3);

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_at(0).instance_count);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(1).instance_count);
}

void test_a_b_a_textures_reapply_complete_set(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture_a = make_deformation_texture();
    nt_texture_t texture_b = make_deformation_texture();
    nt_resource_t surface = make_surface_resource();
    const char *samplers[] = {"u_surface", "u_skin_matrices"};
    nt_material_t material = make_material_ex(nt_gfx_fake_make_program(samplers, 2), surface);
    nt_entity_t entity_a0 = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_a});
    nt_entity_t entity_b = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_b});
    nt_entity_t entity_a1 = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_a});
    nt_render_item_t items[3] = {
        make_item(entity_a0, material, mesh),
        make_item(entity_b, material, mesh),
        make_item(entity_a1, material, mesh),
    };

    nt_gfx_fake_reset();
    skinned_draw_list(items, 3);

    const uint32_t surface_backend = nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(surface)});
    const uint32_t a_backend = nt_gfx_test_texture_backend_id(texture_a);
    const uint32_t b_backend = nt_gfx_test_texture_backend_id(texture_b);
    /* Every run applies the complete set; gfx records only the units that changed in the pass. */
    TEST_ASSERT_EQUAL_UINT32(3, g_nt_gfx.last_frame.accepted[NT_GFX_OP_TEXTURE_SET]);
    const uint32_t expected[4] = {surface_backend, a_backend, b_backend, a_backend};
    TEST_ASSERT_EQUAL_UINT32(4, nt_gfx_fake_bound_texture_count());
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT32(expected[i], nt_gfx_fake_bound_texture_at(i));
    }
    const uint32_t default_backend = nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(texture_a));
    TEST_ASSERT_EQUAL_UINT32(default_backend, nt_gfx_fake_last_sampler(1));
}

void test_separate_draw_lists_replay_supplied_texture_set(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = make_item(entity, material, mesh);

    skinned_draw_list(&item, 1);
    nt_gfx_fake_reset();
    skinned_draw_list(&item, 1);

    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(texture), nt_gfx_fake_bound_texture_at(0));
}

void test_packed_instances_keep_each_entity_world_and_binding(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_deformation_binding_t first = {.texture = texture, .x0 = 1, .y0 = 2, .x1 = 3, .y1 = 4, .alpha = 0.25F};
    nt_deformation_binding_t second = {.texture = texture, .x0 = 11, .y0 = 12, .x1 = 13, .y1 = 14, .alpha = 0.75F};
    nt_entity_t entity0 = make_entity(mesh, material, first);
    nt_entity_t entity1 = make_entity(mesh, material, second);
    nt_transform_comp_set_position(entity0, 5.0F, 0.0F, 0.0F);
    nt_transform_comp_set_position(entity1, 9.0F, 0.0F, 0.0F);
    nt_transform_comp_update();
    nt_render_item_t items[2] = {make_item(entity1, material, mesh), make_item(entity0, material, mesh)};

    skinned_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());

    const uint8_t *bytes = (const uint8_t *)nt_gfx_fake_last_update_buffer_data();
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_UINT32(2U * sizeof(nt_skinned_mesh_instance_t), nt_gfx_fake_last_update_buffer_size());
    uint32_t world_x_bits[2];
    uint16_t origins[2][4];
    const uint16_t expected_origins[2][4] = {{11, 12, 13, 14}, {1, 2, 3, 4}};
    uint32_t alpha_bits[2];
    memcpy(&world_x_bits[0], bytes + 12, sizeof(uint32_t));
    memcpy(&origins[0], bytes + 48, sizeof(origins[0]));
    memcpy(&alpha_bits[0], bytes + 56, sizeof(uint32_t));
    memcpy(&world_x_bits[1], bytes + sizeof(nt_skinned_mesh_instance_t) + 12, sizeof(uint32_t));
    memcpy(&origins[1], bytes + sizeof(nt_skinned_mesh_instance_t) + 48, sizeof(origins[1]));
    memcpy(&alpha_bits[1], bytes + sizeof(nt_skinned_mesh_instance_t) + 56, sizeof(uint32_t));
    TEST_ASSERT_EQUAL_HEX32(0x41100000U, world_x_bits[0]); /* 9.0f */
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected_origins[0], origins[0], 4);
    TEST_ASSERT_EQUAL_HEX32(0x3F400000U, alpha_bits[0]);   /* 0.75f */
    TEST_ASSERT_EQUAL_HEX32(0x40A00000U, world_x_bits[1]); /* 5.0f */
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected_origins[1], origins[1], 4);
    TEST_ASSERT_EQUAL_HEX32(0x3E800000U, alpha_bits[1]); /* 0.25f */
}

/* Two runs in one list, with the transform and drawable dense indices apart (a transform-only
 * entity comes first): each instance takes its own world, binding and color, and the second run
 * starts right after the first run's instances. */
void test_runs_of_one_list_pack_each_entity_fields(void) {
    nt_entity_t filler = nt_entity_create();
    TEST_ASSERT_TRUE(nt_transform_comp_add(filler));
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1);
    nt_material_t materials[2] = {make_material(program), make_material(program)};
    nt_render_item_t items[4];
    for (uint16_t i = 0; i < 4; i++) {
        nt_entity_t entity = make_entity(mesh, materials[i / 2], (nt_deformation_binding_t){.texture = texture, .x0 = (uint16_t)(20U + i)});
        nt_transform_comp_set_position(entity, (float)(i + 1), 0.0F, 0.0F);
        nt_drawable_comp_set_color(entity, 0x10203000U + i);
        items[i] = make_item(entity, materials[i / 2], mesh);
    }
    nt_transform_comp_update();

    record_skinned_list(items, 4);
    nt_test_frame_close(); /* frame storage keeps the frame's bytes until the next begin_frame */
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(4U * sizeof(nt_skinned_mesh_instance_t), g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    TEST_ASSERT_EQUAL_UINT32(2U * sizeof(nt_skinned_mesh_instance_t), nt_gfx_fake_last_instance_offset());
    for (uint32_t i = 0; i < 4; i++) {
        const nt_skinned_mesh_instance_t *instance = (const nt_skinned_mesh_instance_t *)g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].staging + i;
        TEST_ASSERT_EQUAL_HEX32(0x10203000U + i, instance->color);
        TEST_ASSERT_EQUAL_UINT16(20U + i, instance->skin_origins[0]);
        TEST_ASSERT_TRUE(instance->world_rows[0][3] == (float)(i + 1)); /* NOLINT -- exact small integer */
    }
    nt_test_frame_open();
}

/* Every render item needs a drawable: its color is instance data. */
void test_draw_list_asserts_on_an_item_without_drawable(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = nt_entity_create();
    TEST_ASSERT_TRUE(nt_transform_comp_add(entity));
    TEST_ASSERT_TRUE(nt_mesh_comp_add(entity));
    TEST_ASSERT_TRUE(nt_material_comp_add(entity));
    TEST_ASSERT_TRUE(nt_skin_comp_add(entity));
    *nt_mesh_comp_handle(entity) = mesh;
    *nt_material_comp_handle(entity) = material;
    *nt_skin_comp_handle(entity) = (nt_deformation_binding_t){.texture = texture};
    nt_render_item_t item = make_item(entity, material, mesh);

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, &item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "no drawable component"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

/* The world matrix comes from the transform view: an item without a transform asserts. */
void test_draw_list_asserts_on_an_item_without_transform(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = nt_entity_create();
    TEST_ASSERT_TRUE(nt_mesh_comp_add(entity));
    TEST_ASSERT_TRUE(nt_material_comp_add(entity));
    TEST_ASSERT_TRUE(nt_drawable_comp_add(entity));
    TEST_ASSERT_TRUE(nt_skin_comp_add(entity));
    *nt_mesh_comp_handle(entity) = mesh;
    *nt_material_comp_handle(entity) = material;
    *nt_skin_comp_handle(entity) = (nt_deformation_binding_t){.texture = texture};
    nt_render_item_t item = make_item(entity, material, mesh);

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, &item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "no transform component"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

/* Skin bindings come from the skin view. After e0's skin is removed and re-added, skin dense order
 * is [e3, e1, e2, e0] while drawable order stays [e0, e1, e2, e3], and the list order matches neither:
 * a read by drawable index, transform index or list position takes another entity's binding. */
void test_skin_view_reads_each_binding_after_a_skin_reorder(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t textures[2] = {make_deformation_texture(), make_deformation_texture()};
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entities[4];
    nt_deformation_binding_t bindings[4];
    for (uint16_t i = 0; i < 4; i++) {
        bindings[i] = (nt_deformation_binding_t){.texture = textures[i == 2 ? 1 : 0], .x0 = (uint16_t)(30U + i), .alpha = 0.25F * (float)i};
        entities[i] = make_entity(mesh, material, bindings[i]);
        nt_drawable_comp_set_color(entities[i], 0x50607000U + i);
    }
    nt_skin_comp_remove(entities[0]); /* e3's binding moves to skin dense 0 */
    TEST_ASSERT_TRUE(nt_skin_comp_add(entities[0]));
    *nt_skin_comp_handle(entities[0]) = bindings[0]; /* e0 now at skin dense 3 */
    const uint16_t order[4] = {1, 3, 0, 2};
    nt_render_item_t items[4];
    for (uint32_t i = 0; i < 4; i++) {
        items[i] = make_item(entities[order[i]], material, mesh);
    }

    record_skinned_list(items, 4);
    nt_test_frame_close();                      /* frame storage keeps the frame's bytes until the next begin_frame */
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls()); /* e1, e3, e0 share texture 0; e2 has texture 1 */
    TEST_ASSERT_EQUAL_UINT32(4, drawn_instances());
    TEST_ASSERT_EQUAL_UINT32(4U * sizeof(nt_skinned_mesh_instance_t), g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    TEST_ASSERT_EQUAL_UINT32(3U * sizeof(nt_skinned_mesh_instance_t), nt_gfx_fake_last_instance_offset());
    for (uint32_t i = 0; i < 4; i++) {
        const nt_skinned_mesh_instance_t *instance = (const nt_skinned_mesh_instance_t *)g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].staging + i;
        TEST_ASSERT_EQUAL_HEX32(0x50607000U + order[i], instance->color);
        TEST_ASSERT_EQUAL_UINT16(30U + order[i], instance->skin_origins[0]);
        TEST_ASSERT_TRUE(instance->skin_alpha == 0.25F * (float)order[i]); /* NOLINT -- exact quarter steps */
    }
    nt_test_frame_open();
}

/* A destroyed leader whose slot a new skinned entity took still asserts: the leader's accessor checks liveness. */
void test_draw_list_asserts_on_a_stale_leader_in_a_reused_slot(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t stale = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = make_item(stale, material, mesh);
    nt_entity_destroy(stale);
    nt_entity_t reused = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    TEST_ASSERT_EQUAL_UINT16(nt_entity_index(stale), nt_entity_index(reused));

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, &item, 1));
    TEST_ASSERT_EQUAL_STRING("nt_entity_is_alive(entity)", nt_test_assert_last_expr);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

/* A run leader reads its binding through the asserting accessor. */
void test_draw_list_asserts_on_a_leader_without_skin(void) {
    nt_mesh_t mesh = make_mesh();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = make_deformation_texture()});
    nt_skin_comp_remove(entity);
    nt_render_item_t item = make_item(entity, material, mesh);

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, &item, 1));
    TEST_ASSERT_EQUAL_STRING("idx != NT_INVALID_COMP_INDEX", nt_test_assert_last_expr);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

/* Followers are read through the skin view; the run scan asserts their skin component. */
void test_draw_list_asserts_on_a_follower_without_skin(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t leader = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_entity_t follower = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_skin_comp_remove(follower);
    const nt_render_item_t items[2] = {make_item(leader, material, mesh), make_item(follower, material, mesh)};

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, items, 2));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "no skin component"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

/* A destroyed follower whose slot is not reused lost its components with it: the run scan asserts. */
void test_draw_list_asserts_on_a_destroyed_follower(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t leader = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_entity_t follower = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    const nt_render_item_t items[2] = {make_item(leader, material, mesh), make_item(follower, material, mesh)};
    nt_entity_destroy(follower);

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, items, 2));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "no skin component"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

void test_material_or_mesh_change_splits_runs_in_input_order(void) {
    nt_mesh_t mesh_a = make_mesh();
    nt_mesh_t mesh_b = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1);
    nt_material_t material_a = make_material(program);
    nt_material_t material_b = make_material(program);
    nt_entity_t entities[4] = {
        make_entity(mesh_a, material_a, (nt_deformation_binding_t){.texture = texture}),
        make_entity(mesh_a, material_b, (nt_deformation_binding_t){.texture = texture}),
        make_entity(mesh_b, material_b, (nt_deformation_binding_t){.texture = texture}),
        make_entity(mesh_a, material_a, (nt_deformation_binding_t){.texture = texture}),
    };
    nt_render_item_t items[4] = {
        make_item(entities[0], material_a, mesh_a),
        make_item(entities[1], material_b, mesh_a),
        make_item(entities[2], material_b, mesh_b),
        make_item(entities[3], material_a, mesh_a),
    };

    skinned_draw_list(items, 4);

    TEST_ASSERT_EQUAL_UINT32(4, nt_gfx_fake_draw_trace_count());
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(i).instance_count);
    }
}

void test_material_transition_reapplies_complete_surface_and_skin_set(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_resource_t surface = make_surface_resource();
    const char *samplers[] = {"u_surface", "u_skin_matrices"};
    nt_program_t program = nt_gfx_fake_make_program(samplers, 2);
    nt_material_t material_a = make_material_ex(program, surface);
    nt_material_t material_b = make_material_ex(program, surface);
    nt_entity_t entity_a = make_entity(mesh, material_a, (nt_deformation_binding_t){.texture = texture});
    nt_entity_t entity_b = make_entity(mesh, material_b, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t items[2] = {make_item(entity_a, material_a, mesh), make_item(entity_b, material_b, mesh)};

    nt_gfx_fake_reset();
    skinned_draw_list(items, 2);

    /* The transition applies the complete set again; its units equal the pass state, so it ends CACHE. */
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.last_frame.accepted[NT_GFX_OP_TEXTURE_SET]);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_slot_at(0));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_slot_at(1));
}

void test_indexed_and_nonindexed_meshes_use_matching_draw_paths(void) {
    nt_mesh_t indexed = make_indexed_mesh();
    nt_mesh_t nonindexed = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t indexed_entity = make_entity(indexed, material, (nt_deformation_binding_t){.texture = texture});
    nt_entity_t nonindexed_entity = make_entity(nonindexed, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t items[2] = {make_item(indexed_entity, material, indexed), make_item(nonindexed_entity, material, nonindexed)};

    skinned_draw_list(items, 2);

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_draw_trace_at(0).num_indices);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_draw_trace_at(1).num_indices);
}

void test_color_keeps_skin_fields_at_their_layout_offsets(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1);
    const nt_deformation_binding_t binding = {.texture = texture, .x0 = 2, .y0 = 4, .x1 = 6, .y1 = 8, .alpha = 0.5F};

    nt_material_t rgba8 = make_material(program);
    nt_entity_t rgba8_entity = make_entity(mesh, rgba8, binding);
    nt_drawable_comp_set_color(rgba8_entity, NT_RGBA8(0, 128, 255, 64));
    nt_render_item_t item = make_item(rgba8_entity, rgba8, mesh);
    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    const uint8_t *bytes = (const uint8_t *)nt_gfx_fake_last_update_buffer_data();
    TEST_ASSERT_EQUAL_UINT32(sizeof(nt_skinned_mesh_instance_t), nt_gfx_fake_last_update_buffer_size());
    const uint8_t expected_rgba8[4] = {0, 128, 255, 64};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_rgba8, bytes + offsetof(nt_skinned_mesh_instance_t, color), 4);
    uint16_t origins[4];
    const uint16_t expected_origins[4] = {2, 4, 6, 8};
    memcpy(origins, bytes + 48, sizeof(origins));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected_origins, origins, 4);
    uint32_t alpha_bits;
    memcpy(&alpha_bits, bytes + 56, sizeof(alpha_bits));
    TEST_ASSERT_EQUAL_HEX32(0x3F000000U, alpha_bits);
}

void test_zero_deformation_texture_asserts(void) {
    nt_mesh_t mesh = make_mesh();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){0});
    nt_render_item_t item = make_item(entity, material, mesh);

    NT_TEST_EXPECT_ASSERT(skinned_draw_list(&item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "requires a deformation texture"));
}

/* A hand-built mesh may place a stream anywhere below 16; one on the instance color slot asserts at the bind. */
void test_skinned_mesh_stream_cannot_overlap_the_color_location(void) {
    nt_texture_t texture = make_deformation_texture();
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1);
    nt_mesh_t mesh = make_mesh_at(NT_INST_LOC_COLOR);
    nt_material_t material = make_material(program);
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = make_item(entity, material, mesh);

    begin_storage_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, &item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "share a location"));
}

/* The core draws caller-packed skinned instances with no entity component and binds the
 * deformation as its draw texture; one allocation serves two passes and uploads once. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_core_draw_binds_the_deformation_draw_texture_across_passes(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    begin_storage_frame();
    nt_gfx_fake_reset();
    s_draw_mark = nt_gfx_fake_draw_trace_count();
    uint32_t offset = 0;
    nt_skinned_mesh_instance_t *instance = (nt_skinned_mesh_instance_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, sizeof(nt_skinned_mesh_instance_t), 4, &offset);
    *instance = (nt_skinned_mesh_instance_t){.skin_origins = {3, 0, 3, 0}, .color = NT_RGBA8(255, 255, 255, 255)};
    nt_mesh_instance_world_rows(instance->world_rows, world);
    for (int pass = 0; pass < 2; pass++) {
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
        nt_mesh_renderer_draw_skinned(mesh, material, texture, skinned_layout(), nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX), offset, 1);
        nt_gfx_end_pass();
    }
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_update_buffer_count()); /* recording writes no buffer */
    nt_test_frame_end();
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_update_buffer_count()); /* one upload for both passes */
    TEST_ASSERT_EQUAL_UINT32(offset, nt_gfx_fake_last_instance_offset());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(texture), nt_gfx_fake_bound_texture_at(0));
    nt_test_frame_open();
}

/* A list packs into the stream it names and binds that stream's instances; the core draw reads
 * only the named stream's allocations and a frame vertex stream. */
void test_skinned_draws_route_to_their_stream(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = {.entity = entity.id, .batch_key = nt_mesh_renderer_batch_key(material, mesh)};

    nt_test_frame_next();
    const uint32_t general = g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used;
    nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX + 1, &item, 1);
    TEST_ASSERT_EQUAL_UINT32(general, g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    TEST_ASSERT_EQUAL_UINT32(sizeof(nt_skinned_mesh_instance_t), g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX + 1].used);
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_skinned(mesh, material, texture, skinned_layout(), nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 1), 0, 2));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "instances lie outside"));
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_INDEX, &item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "not a frame vertex stream"));
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_buffer_backend_id(nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 1)), nt_gfx_fake_last_instance_buffer());
}

/* Skinned draws use the mesh's vertex input per stream too: two streams make two, each reading
 * its own buffer, and returning to a stream reuses its vertex input. */
void test_skinned_vertex_input_versions_per_stream(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_render_item_t item = {.entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture}).id, .batch_key = nt_mesh_renderer_batch_key(material, mesh)};
    nt_gfx_fake_reset();
    for (uint32_t pass = 0; pass < 2; pass++) {
        for (uint32_t k = 0; k < 2; k++) {
            nt_test_frame_next();
            nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX + k, &item, 1);
            nt_test_frame_next();
            TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_buffer_backend_id(nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + k)), nt_gfx_fake_last_instance_buffer());
        }
    }
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_vertex_input_create_count());
}

/* One material and one deformation texture over two meshes: the mesh change skips material work. */
void test_mesh_only_change_skips_the_texture_set(void) {
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_mesh_t mesh_a = make_mesh();
    nt_mesh_t mesh_b = make_mesh();
    nt_render_item_t items[2] = {
        make_item(make_entity(mesh_a, material, (nt_deformation_binding_t){.texture = texture}), material, mesh_a),
        make_item(make_entity(mesh_b, material, (nt_deformation_binding_t){.texture = texture}), material, mesh_b),
    };

    nt_gfx_fake_reset();
    skinned_draw_list(items, 2);
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.last_frame.accepted[NT_GFX_OP_TEXTURE_SET]);
}

/* draw_list reads the deformation texture at the call, so a rebound entity draws the new one. */
void test_lists_read_the_deformation_texture_at_the_call(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture_a = make_deformation_texture();
    nt_texture_t texture_b = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_a});
    nt_render_item_t item = make_item(entity, material, mesh);

    nt_gfx_fake_reset();
    record_skinned_list(&item, 1);
    nt_skin_comp_handle(entity)->texture = texture_b;
    nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, &item, 1);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(texture_a), nt_gfx_fake_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(texture_b), nt_gfx_fake_bound_texture_at(1));
}

/* A/B/A deformation textures with one material: each run re-applies the texture set, but the
 * material's params bind once. */
void test_deformation_change_reapplies_textures_not_params(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture_a = make_deformation_texture();
    nt_texture_t texture_b = make_deformation_texture();
    nt_material_t material = nt_material_create(&(nt_material_create_desc_t){
        .program = nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1),
        .params = (const float[4]){1.0F, 1.0F, 1.0F, 1.0F},
        .params_size = 4 * sizeof(float),
        .label = "skin_test_material_with_param",
    });
    nt_render_item_t items[3] = {
        make_item(make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_a}), material, mesh),
        make_item(make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_b}), material, mesh),
        make_item(make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_a}), material, mesh),
    };

    nt_gfx_fake_reset();
    skinned_draw_list(items, 3);
    TEST_ASSERT_EQUAL_UINT32(3, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(3, g_nt_gfx.last_frame.accepted[NT_GFX_OP_TEXTURE_SET]);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_ubo_bind_count());
}

void test_empty_list_reserves_nothing(void) {
    record_skinned_list(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());
}

/* A loss during restore can leave the deformation texture and meshes 0; a list on a lost context returns before reading them. */
static void test_list_on_a_lost_context_skips_its_handle_checks(void) {
    nt_gfx_fake_set_context_lost(true);
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8}); /* the failed create latches the loss */
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    const nt_render_item_t item = {.entity = 0xFFFFU};
    nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, &item, 1);
    nt_gfx_fake_set_context_lost(false);
}

/* One pipeline cache: a skinned list and a plain list of one material build one pipeline. */
void test_skinned_and_plain_lists_share_one_pipeline(void) {
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program(NULL, 0));
    nt_mesh_t skinned_mesh = make_mesh();
    nt_mesh_t plain_mesh = make_mesh();
    nt_render_item_t skinned = make_item(make_entity(skinned_mesh, material, (nt_deformation_binding_t){.texture = texture}), material, skinned_mesh);
    nt_render_item_t plain = make_item(make_entity(plain_mesh, material, (nt_deformation_binding_t){0}), material, plain_mesh);

    nt_test_frame_next();
    s_draw_mark = nt_gfx_fake_draw_trace_count();
    nt_mesh_renderer_draw_list_skinned(NT_GFX_FRAME_VERTEX, &skinned, 1);
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, &plain, 1);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_pipeline_create_count());
}

/* Records one skinned instance through the core in its own frame and executes it. */
static void core_draw_frame(nt_mesh_t mesh, nt_material_t material, nt_texture_t deformation) {
    nt_test_frame_next();
    uint32_t offset = 0;
    nt_skinned_mesh_instance_t *instance = (nt_skinned_mesh_instance_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, sizeof(nt_skinned_mesh_instance_t), 4, &offset);
    *instance = (nt_skinned_mesh_instance_t){.color = NT_RGBA8(255, 255, 255, 255)};
    nt_mesh_renderer_draw_skinned(mesh, material, deformation, skinned_layout(), nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX), offset, 1);
    nt_test_frame_next();
}

/* The deformation texture joins the material's set as u_skin_matrices; a material that still
 * declares u_skin_matrices asserts. */
void test_core_draw_appends_the_deformation_to_the_material_set(void) {
    nt_mesh_t mesh = make_mesh();
    nt_resource_t surface = make_surface_resource();
    nt_texture_t texture = make_deformation_texture();
    const char *samplers[] = {"u_surface", "u_skin_matrices"};
    nt_material_t material = make_material_ex(nt_gfx_fake_make_program(samplers, 2), surface);
    nt_material_t stale = nt_material_create(&(nt_material_create_desc_t){
        .program = nt_gfx_fake_make_program(samplers, 2),
        .textures = {{.name = "u_surface", .resource = surface}, {.name = "u_skin_matrices", .resource = surface}},
        .texture_count = 2,
        .label = "skin_test_stale_material",
    });

    nt_gfx_fake_reset();
    core_draw_frame(mesh, material, texture);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id((nt_texture_t){.id = nt_resource_get(surface)}), nt_gfx_fake_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(texture), nt_gfx_fake_bound_texture_at(1));

    NT_TEST_EXPECT_ASSERT(core_draw_frame(mesh, stale, texture));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "appears more than once"));
}

#if NT_GFX_CAPTURE_ENABLED
/* A deformation-only split notes the same material and mesh for both runs. */
void test_deformation_split_notes_the_same_material_and_mesh(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture_a = make_deformation_texture();
    nt_texture_t texture_b = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity_a = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_a});
    nt_entity_t entity_b = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture_b});
    nt_render_item_t items[2] = {make_item(entity_a, material, mesh), make_item(entity_b, material, mesh)};

    nt_gfx_capture_request();
    record_skinned_list(items, 2);

    const uint32_t notes[2] = {nt_test_note(0, "mesh"), nt_test_note(nt_test_note(0, "mesh") + 1U, "mesh")};
    const uint32_t count = nt_test_capture_latest().count;
    TEST_ASSERT_LESS_THAN_UINT32(count, notes[1]);
    TEST_ASSERT_EQUAL_UINT32(count, nt_test_note(notes[1] + 1U, "mesh"));
    for (uint32_t i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL_UINT32(material.id, nt_test_note_u32(notes[i], "material"));
        TEST_ASSERT_EQUAL_UINT32(mesh.id, nt_test_note_u32(notes[i], "mesh"));
        TEST_ASSERT_EQUAL(NT_GFX_RESULT_ACCEPTED, nt_test_next_draw_result(notes[i]));
    }
    nt_test_frame_next();
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_one_compatible_item_draws_with_its_deformation_texture);
    RUN_TEST(test_compatible_items_share_one_instanced_draw);
    RUN_TEST(test_deformation_texture_splits_runs_but_origins_do_not);
#if NT_GFX_CAPTURE_ENABLED
    RUN_TEST(test_deformation_split_notes_the_same_material_and_mesh);
#endif
    RUN_TEST(test_a_b_a_textures_reapply_complete_set);
    RUN_TEST(test_separate_draw_lists_replay_supplied_texture_set);
    RUN_TEST(test_packed_instances_keep_each_entity_world_and_binding);
    RUN_TEST(test_runs_of_one_list_pack_each_entity_fields);
    RUN_TEST(test_draw_list_asserts_on_an_item_without_drawable);
    RUN_TEST(test_draw_list_asserts_on_an_item_without_transform);
    RUN_TEST(test_skin_view_reads_each_binding_after_a_skin_reorder);
    RUN_TEST(test_draw_list_asserts_on_a_stale_leader_in_a_reused_slot);
    RUN_TEST(test_draw_list_asserts_on_a_leader_without_skin);
    RUN_TEST(test_draw_list_asserts_on_a_follower_without_skin);
    RUN_TEST(test_draw_list_asserts_on_a_destroyed_follower);
    RUN_TEST(test_material_or_mesh_change_splits_runs_in_input_order);
    RUN_TEST(test_material_transition_reapplies_complete_surface_and_skin_set);
    RUN_TEST(test_indexed_and_nonindexed_meshes_use_matching_draw_paths);
    RUN_TEST(test_color_keeps_skin_fields_at_their_layout_offsets);
    RUN_TEST(test_zero_deformation_texture_asserts);
    RUN_TEST(test_skinned_mesh_stream_cannot_overlap_the_color_location);
    RUN_TEST(test_core_draw_binds_the_deformation_draw_texture_across_passes);
    RUN_TEST(test_skinned_draws_route_to_their_stream);
    RUN_TEST(test_skinned_vertex_input_versions_per_stream);
    RUN_TEST(test_lists_read_the_deformation_texture_at_the_call);
    RUN_TEST(test_deformation_change_reapplies_textures_not_params);
    RUN_TEST(test_empty_list_reserves_nothing);
    RUN_TEST(test_mesh_only_change_skips_the_texture_set);
    RUN_TEST(test_list_on_a_lost_context_skips_its_handle_checks);
    RUN_TEST(test_skinned_and_plain_lists_share_one_pipeline);
    RUN_TEST(test_core_draw_appends_the_deformation_to_the_material_set);
    return UNITY_END();
}
