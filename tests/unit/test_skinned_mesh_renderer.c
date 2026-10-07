#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/nt_gfx_test_desc.h"

#include <string.h>

/* clang-format off */
#include "renderers/nt_skinned_mesh_renderer.h"
#include "color/nt_color.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
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

static nt_mesh_t make_mesh(void) {
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 36];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *header = (NtMeshAssetHeader *)blob;
    header->magic = NT_MESH_MAGIC;
    header->version = NT_MESH_VERSION;
    header->stream_count = 1;
    header->vertex_count = 3;
    header->vertex_data_size = 36;

    NtStreamDesc *stream = (NtStreamDesc *)(blob + sizeof(*header));
    stream->name_hash = nt_hash32_str("position").value;
    stream->type = NT_STREAM_FLOAT32;
    stream->count = 3;

    float *vertices = (float *)(blob + sizeof(*header) + sizeof(*stream));
    vertices[3] = 1.0F;
    vertices[7] = 1.0F;
    return (nt_mesh_t){.id = nt_gfx_activate_mesh(blob, sizeof(blob))};
}

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
    stream->name_hash = nt_hash32_str("position").value;
    stream->type = NT_STREAM_FLOAT32;
    stream->count = 3;

    uint16_t *indices = (uint16_t *)(blob + sizeof(*header) + sizeof(*stream) + 36);
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    return (nt_mesh_t){.id = nt_gfx_activate_mesh(blob, sizeof(blob))};
}

static nt_mesh_t make_skin_stream_mesh(void) {
    enum { VERTEX_BYTES = 3 * 20 };
    uint8_t blob[sizeof(NtMeshAssetHeader) + (3 * sizeof(NtStreamDesc)) + VERTEX_BYTES];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *header = (NtMeshAssetHeader *)blob;
    header->magic = NT_MESH_MAGIC;
    header->version = NT_MESH_VERSION;
    header->stream_count = 3;
    header->vertex_count = 3;
    header->vertex_data_size = VERTEX_BYTES;

    NtStreamDesc *streams = (NtStreamDesc *)(blob + sizeof(*header));
    streams[0] = (NtStreamDesc){.name_hash = nt_hash32_str("position").value, .type = NT_STREAM_FLOAT32, .count = 3};
    streams[1] = (NtStreamDesc){.name_hash = nt_hash32_str("joints").value, .type = NT_STREAM_UINT8, .count = 4};
    streams[2] = (NtStreamDesc){.name_hash = nt_hash32_str("weights").value, .type = NT_STREAM_UINT8, .count = 4, .normalized = 1};
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

static nt_material_t make_material_ex(nt_program_t program, nt_resource_t surface, nt_sampler_t skin_override) {
    nt_material_create_desc_t desc = {
        .program = program,
        .attr_map = {{.stream_name = "position", .location = 0}},
        .attr_map_count = 1,
        .depth_test = true,
        .depth_write = true,
        .cull_mode = NT_CULL_BACK,
        .label = "skin_test_material",
    };
    if (surface.id != 0) {
        desc.textures[0] = (nt_material_texture_desc_t){.name = "u_surface", .resource = surface};
        desc.textures[1] = (nt_material_texture_desc_t){.name = "u_skin_matrices", .sampler = skin_override};
        desc.texture_count = 2;
    } else {
        desc.textures[0] = (nt_material_texture_desc_t){.name = "u_skin_matrices", .sampler = skin_override};
        desc.texture_count = 1;
    }
    return nt_material_create(&desc);
}

static nt_material_t make_material(nt_program_t program) { return make_material_ex(program, NT_RESOURCE_INVALID, NT_SAMPLER_DEFAULT); }

static nt_material_t make_material_without_skin(nt_program_t program) {
    return nt_material_create(&(nt_material_create_desc_t){
        .program = program,
        .attr_map = {{.stream_name = "position", .location = 0}},
        .attr_map_count = 1,
        .depth_test = true,
        .depth_write = true,
        .cull_mode = NT_CULL_BACK,
        .label = "skin_test_material_without_skin",
    });
}

static nt_material_t make_material_with_skin_streams(nt_program_t program, uint8_t joints_location, uint8_t weights_location) {
    nt_material_create_desc_t desc = {
        .program = program,
        .attr_map =
            {
                {.stream_name = "position", .location = 0},
                {.stream_name = "joints", .location = joints_location},
                {.stream_name = "weights", .location = weights_location},
            },
        .attr_map_count = 3,
        .textures = {{.name = "u_skin_matrices"}},
        .texture_count = 1,
        .depth_test = true,
        .depth_write = true,
        .cull_mode = NT_CULL_BACK,
        .label = "skin_test_stream_material",
    };
    return nt_material_create(&desc);
}

static uint32_t s_program_warnings;

static void capture_program_warning(nt_log_level_t level, const char *domain, const char *message, void *user) {
    (void)user;
    if (level == NT_LOG_LEVEL_WARN && strcmp(domain, "skinned_mesh_renderer") == 0 && strstr(message, "program is not ready") != NULL) {
        s_program_warnings++;
    }
}

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

/* One gfx frame drawing a single list, ending inside the pass the tests draw in. */
static void skinned_draw_list(const nt_render_item_t *items, uint32_t count) {
    begin_storage_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    s_draw_mark = nt_gfx_fake_draw_trace_count();
    nt_skinned_mesh_renderer_draw_list(items, count);
}

static void mesh_draw_list(const nt_render_item_t *items, uint32_t count) {
    begin_storage_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    s_draw_mark = nt_gfx_fake_draw_trace_count();
    nt_mesh_renderer_draw_list(items, count);
}

void setUp(void) {
    s_surface_pack_created = false;
    s_program_warnings = 0;
    nt_log_add_sink(capture_program_warning, NULL);
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&NT_GFX_TEST_DESC(.max_shaders = 8, .max_programs = 8, .max_pipelines = 16, /* skinned 8 + static 2 + headroom */
                                  .max_buffers = 32, .max_textures = 16, .max_meshes = 8,
                                  /* 48 non-mesh inputs + 8 meshes * (2 static + 4 skinned layouts). */
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
    nt_skinned_mesh_renderer_desc_t desc = nt_skinned_mesh_renderer_desc_defaults();
    desc.max_pipelines = 8;
    TEST_ASSERT_EQUAL(NT_OK, nt_skinned_mesh_renderer_init(&desc));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_fake_draw_trace_reset(true);
    s_draw_mark = 0;
}

void tearDown(void) {
    nt_log_remove_sink(capture_program_warning, NULL);
    nt_gfx_end_pass();
    nt_skinned_mesh_renderer_shutdown();
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

void test_one_compatible_item_draws_with_supplied_deformation_texture(void) {
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

void test_a_b_a_textures_reapply_complete_set_and_ignore_skin_override(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture_a = make_deformation_texture();
    nt_texture_t texture_b = make_deformation_texture();
    nt_resource_t surface = make_surface_resource();
    nt_sampler_t override = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .label = "skin_override_must_be_ignored",
    });
    const char *samplers[] = {"u_surface", "u_skin_matrices"};
    nt_material_t material = make_material_ex(nt_gfx_fake_make_program(samplers, 2), surface, override);
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
    TEST_ASSERT_EQUAL_UINT32(3, g_nt_gfx.counters.accepted[NT_GFX_OP_TEXTURE_SET]);
    const uint32_t expected[4] = {surface_backend, a_backend, b_backend, a_backend};
    TEST_ASSERT_EQUAL_UINT32(4, nt_gfx_fake_bound_texture_count());
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT32(expected[i], nt_gfx_fake_bound_texture_at(i));
    }
    const uint32_t default_backend = nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(texture_a));
    TEST_ASSERT_EQUAL_UINT32(default_backend, nt_gfx_fake_last_sampler(1));
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_gfx_test_sampler_backend_id(override), nt_gfx_fake_last_sampler(1));
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
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls()); /* executes the stream: uploads the instances */

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

    skinned_draw_list(items, 4);
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(4U * sizeof(nt_skinned_mesh_instance_t), g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    TEST_ASSERT_EQUAL_UINT32(2U * sizeof(nt_skinned_mesh_instance_t), nt_gfx_fake_last_instance_offset());
    for (uint32_t i = 0; i < 4; i++) {
        const nt_skinned_mesh_instance_t *instance = (const nt_skinned_mesh_instance_t *)g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].staging + i;
        TEST_ASSERT_EQUAL_HEX32(0x10203000U + i, instance->color);
        TEST_ASSERT_EQUAL_UINT16(20U + i, instance->skin_origins[0]);
        TEST_ASSERT_TRUE(instance->world_rows[0][3] == (float)(i + 1)); /* NOLINT -- exact small integer */
    }
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
    NT_TEST_EXPECT_ASSERT(nt_skinned_mesh_renderer_draw_list(&item, 1));
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
    NT_TEST_EXPECT_ASSERT(nt_skinned_mesh_renderer_draw_list(&item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "no transform component"));
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
    nt_material_t material_a = make_material_ex(program, surface, NT_SAMPLER_DEFAULT);
    nt_material_t material_b = make_material_ex(program, surface, NT_SAMPLER_DEFAULT);
    nt_entity_t entity_a = make_entity(mesh, material_a, (nt_deformation_binding_t){.texture = texture});
    nt_entity_t entity_b = make_entity(mesh, material_b, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t items[2] = {make_item(entity_a, material_a, mesh), make_item(entity_b, material_b, mesh)};

    nt_gfx_fake_reset();
    skinned_draw_list(items, 2);

    /* The transition applies the complete set again; its unit binds equal the pass state and are dropped. */
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.counters.accepted[NT_GFX_OP_TEXTURE_SET]);
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

    nt_material_t rgba8 = make_material_ex(program, NT_RESOURCE_INVALID, NT_SAMPLER_DEFAULT);
    nt_entity_t rgba8_entity = make_entity(mesh, rgba8, binding);
    nt_drawable_comp_set_color(rgba8_entity, NT_RGBA8(0, 128, 255, 64));
    nt_render_item_t item = make_item(rgba8_entity, rgba8, mesh);
    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls()); /* executes the stream: uploads the instances */
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

void test_active_skin_sampler_must_be_declared_by_material(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material_without_skin(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = make_item(entity, material, mesh);

    NT_TEST_EXPECT_ASSERT(skinned_draw_list(&item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "coverage is incomplete"));
}

void test_zero_deformation_texture_asserts(void) {
    nt_mesh_t mesh = make_mesh();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){0});
    nt_render_item_t item = make_item(entity, material, mesh);

    NT_TEST_EXPECT_ASSERT(skinned_draw_list(&item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "requires a deformation texture"));
}

void test_skinned_mesh_stream_cannot_overlap_the_color_location(void) {
    nt_texture_t texture = make_deformation_texture();
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1);
    nt_mesh_t mesh = make_skin_stream_mesh();
    nt_material_t material = make_material_with_skin_streams(program, 13, 9);
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = make_item(entity, material, mesh);

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_skinned_mesh_renderer_draw_list(&item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "attribute location used twice"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

void test_static_mesh_stream_cannot_overlap_the_color_location(void) {
    nt_mesh_renderer_desc_t desc = {.max_pipelines = 4, .max_mesh_layouts = 2};
    TEST_ASSERT_EQUAL(NT_OK, nt_mesh_renderer_init(&desc));
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_mesh_t mesh = make_mesh();
    nt_material_t material = nt_material_create(&(nt_material_create_desc_t){
        .program = program,
        .attr_map = {{.stream_name = "position", .location = 7}},
        .attr_map_count = 1,
        .label = "static_test_active_color_overlap",
    });
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){0});
    nt_render_item_t item = make_item(entity, material, mesh);

    begin_storage_frame();
    NT_TEST_EXPECT_ASSERT(nt_mesh_renderer_draw_list(&item, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "attribute location used twice"));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_mesh_renderer_shutdown();
}

void test_static_mesh_renderer_ignores_unmapped_skin_streams(void) {
    nt_mesh_renderer_desc_t desc = {.max_pipelines = 2, .max_mesh_layouts = 2};
    TEST_ASSERT_EQUAL(NT_OK, nt_mesh_renderer_init(&desc));
    nt_mesh_t mesh = make_skin_stream_mesh();
    nt_material_t material = make_material_without_skin(nt_gfx_fake_make_program(NULL, 0));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){0});
    nt_render_item_t item = make_item(entity, material, mesh);

    mesh_draw_list(&item, 1);

    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
    nt_mesh_renderer_shutdown();
}

void test_unready_program_warns_once_and_rearms_after_success(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(NT_PROGRAM_INVALID);
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = make_item(entity, material, mesh);

    skinned_draw_list(&item, 1);
    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(NT_LOG_MIN_LEVEL <= 1 ? 1U : 0U, s_program_warnings);

    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1);
    nt_material_set_program(material, program);
    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());

    nt_gfx_destroy_program(program);
    skinned_draw_list(&item, 1);
    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(NT_LOG_MIN_LEVEL <= 1 ? 2U : 0U, s_program_warnings);
}

void test_failed_pipeline_and_vertex_input_creation_are_retryable(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = make_item(entity, material, mesh);

    nt_gfx_fake_fail_next_pipeline_create();
    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(0, nt_skinned_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());

    nt_gfx_fake_fail_next_vertex_input_create();
    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_skinned_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_skinned_mesh_renderer_test_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());

    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_skinned_mesh_renderer_test_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());
}

void test_restore_drops_caches_and_the_next_draw_rebuilds_them(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    nt_entity_t entity = make_entity(mesh, material, (nt_deformation_binding_t){.texture = texture});
    nt_render_item_t item = make_item(entity, material, mesh);
    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_skinned_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_skinned_mesh_renderer_test_vertex_input_count());

    nt_skinned_mesh_renderer_restore_gpu();
    TEST_ASSERT_TRUE(nt_skinned_mesh_renderer_test_initialized());
    TEST_ASSERT_EQUAL_UINT32(0, nt_skinned_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_skinned_mesh_renderer_test_vertex_input_count());

    skinned_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(1, nt_skinned_mesh_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_skinned_mesh_renderer_test_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(1, drawn_calls());

    nt_skinned_mesh_renderer_shutdown();
    nt_skinned_mesh_renderer_restore_gpu();
    TEST_ASSERT_FALSE(nt_skinned_mesh_renderer_test_initialized());
    TEST_ASSERT_EQUAL(NT_OK, nt_skinned_mesh_renderer_init(&(nt_skinned_mesh_renderer_desc_t){.max_pipelines = 2, .max_mesh_layouts = 2}));
}

/* The core draws caller-packed instances with no entity component and supplies the given
 * deformation texture; one allocation serves two passes and uploads once. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_core_draw_supplies_the_deformation_texture_across_passes(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture = make_deformation_texture();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    begin_storage_frame();
    nt_gfx_fake_reset(); /* executes the stream: take it before allocating */
    s_draw_mark = nt_gfx_fake_draw_trace_count();
    uint32_t offset = 0;
    nt_skinned_mesh_instance_t *instance = (nt_skinned_mesh_instance_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, sizeof(nt_skinned_mesh_instance_t), 4, &offset);
    *instance = (nt_skinned_mesh_instance_t){.skin_origins = {3, 0, 3, 0}, .color = NT_RGBA8(255, 255, 255, 255)};
    nt_mesh_instance_world_rows(instance->world_rows, world);
    for (int pass = 0; pass < 2; pass++) {
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
        nt_skinned_mesh_renderer_draw(mesh, material, texture, offset, 1);
        nt_gfx_end_pass();
    }
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_update_buffer_count()); /* recording writes no buffer */
    TEST_ASSERT_EQUAL_UINT32(2, drawn_calls());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_update_buffer_count()); /* one upload for both passes */
    TEST_ASSERT_EQUAL_UINT32(offset, nt_gfx_fake_last_instance_offset());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(texture), nt_gfx_fake_bound_texture_at(0));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

void test_core_draw_asserts_on_a_zero_deformation_texture(void) {
    nt_mesh_t mesh = make_mesh();
    nt_material_t material = make_material(nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1));
    NT_TEST_EXPECT_ASSERT(nt_skinned_mesh_renderer_draw(mesh, material, (nt_texture_t){0}, 0, 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "requires a deformation texture"));
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
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.accepted[NT_GFX_OP_TEXTURE_SET]);
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
    skinned_draw_list(&item, 1);
    nt_skin_comp_handle(entity)->texture = texture_b;
    nt_skinned_mesh_renderer_draw_list(&item, 1);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(texture_a), nt_gfx_fake_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(texture_b), nt_gfx_fake_bound_texture_at(1));
}

/* A/B/A deformation textures with one material: each run re-applies the texture set, but the
 * material's uniforms are written once. */
void test_deformation_change_reapplies_textures_not_uniforms(void) {
    nt_mesh_t mesh = make_mesh();
    nt_texture_t texture_a = make_deformation_texture();
    nt_texture_t texture_b = make_deformation_texture();
    nt_material_t material = nt_material_create(&(nt_material_create_desc_t){
        .program = nt_gfx_fake_make_program((const char *const[]){"u_skin_matrices"}, 1),
        .attr_map = {{.stream_name = "position", .location = 0}},
        .attr_map_count = 1,
        .textures = {{.name = "u_skin_matrices"}},
        .texture_count = 1,
        .params = {{.name = "u_tint", .value = {1.0F, 1.0F, 1.0F, 1.0F}}},
        .param_count = 1,
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
    TEST_ASSERT_EQUAL_UINT32(3, g_nt_gfx.counters.accepted[NT_GFX_OP_TEXTURE_SET]);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());
}

void test_empty_list_reserves_nothing(void) {
    skinned_draw_list(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
    TEST_ASSERT_EQUAL_UINT32(0, drawn_calls());
}

/* A loss during restore can leave the deformation texture and meshes 0; draws on a lost context return before checking them. */
static void test_draws_on_a_lost_context_skip_their_handle_checks(void) {
    nt_gfx_fake_set_context_lost(true);
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8}); /* the failed create latches the loss */
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    const nt_render_item_t item = {.entity = 0xFFFFU};
    nt_skinned_mesh_renderer_draw((nt_mesh_t){0}, (nt_material_t){0}, (nt_texture_t){0}, 0, 1);
    nt_skinned_mesh_renderer_draw_list(&item, 1);
    nt_gfx_fake_set_context_lost(false);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_one_compatible_item_draws_with_supplied_deformation_texture);
    RUN_TEST(test_compatible_items_share_one_instanced_draw);
    RUN_TEST(test_deformation_texture_splits_runs_but_origins_do_not);
    RUN_TEST(test_a_b_a_textures_reapply_complete_set_and_ignore_skin_override);
    RUN_TEST(test_separate_draw_lists_replay_supplied_texture_set);
    RUN_TEST(test_packed_instances_keep_each_entity_world_and_binding);
    RUN_TEST(test_runs_of_one_list_pack_each_entity_fields);
    RUN_TEST(test_draw_list_asserts_on_an_item_without_drawable);
    RUN_TEST(test_draw_list_asserts_on_an_item_without_transform);
    RUN_TEST(test_material_or_mesh_change_splits_runs_in_input_order);
    RUN_TEST(test_material_transition_reapplies_complete_surface_and_skin_set);
    RUN_TEST(test_indexed_and_nonindexed_meshes_use_matching_draw_paths);
    RUN_TEST(test_color_keeps_skin_fields_at_their_layout_offsets);
    RUN_TEST(test_active_skin_sampler_must_be_declared_by_material);
    RUN_TEST(test_zero_deformation_texture_asserts);
    RUN_TEST(test_skinned_mesh_stream_cannot_overlap_the_color_location);
    RUN_TEST(test_static_mesh_stream_cannot_overlap_the_color_location);
    RUN_TEST(test_static_mesh_renderer_ignores_unmapped_skin_streams);
    RUN_TEST(test_unready_program_warns_once_and_rearms_after_success);
    RUN_TEST(test_failed_pipeline_and_vertex_input_creation_are_retryable);
    RUN_TEST(test_restore_drops_caches_and_the_next_draw_rebuilds_them);
    RUN_TEST(test_core_draw_supplies_the_deformation_texture_across_passes);
    RUN_TEST(test_core_draw_asserts_on_a_zero_deformation_texture);
    RUN_TEST(test_lists_read_the_deformation_texture_at_the_call);
    RUN_TEST(test_deformation_change_reapplies_textures_not_uniforms);
    RUN_TEST(test_empty_list_reserves_nothing);
    RUN_TEST(test_mesh_only_change_skips_the_texture_set);
    RUN_TEST(test_draws_on_a_lost_context_skip_their_handle_checks);
    return UNITY_END();
}
