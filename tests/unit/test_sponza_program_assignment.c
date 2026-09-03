#include "test_helpers/nt_gfx_fake.h"
#include <stdint.h>
#include <string.h>

#include "material/nt_material.h"

static void count_material_assignment(nt_material_t material, nt_program_t program);

/* Keep the production loader and link gate in the same translation unit. */
#define main nt_sponza_example_main
#define nt_material_set_program count_material_assignment
// NOLINTNEXTLINE(bugprone-suspicious-include): exercise the real static loader and link gate.
#include "../../examples/sponza/main.c"
#undef nt_material_set_program
#undef main

#include "graphics/nt_gfx_internal.h"
#include "nt_blob_format.h"
#include "nt_crc32.h"
#include "nt_shader_format.h"
#include "unity.h"

#define TEST_NODE_COUNT 4U
#define TEST_PACK_COUNT 7U
#define TEST_PACK_BYTES 1024U

static uint32_t s_assignment_count;
static uint32_t s_test_pack_count;
static uint8_t s_test_pack_blobs[TEST_PACK_COUNT][TEST_PACK_BYTES];
static const uint8_t s_expected_types[TEST_NODE_COUNT] = {SPONZA_SHADER_FULL, SPONZA_SHADER_DIFFUSE, SPONZA_SHADER_ALPHA, SPONZA_SHADER_DIFFUSE};

static const nt_hash64_t s_vertex_ids[3] = {
    ASSET_SHADER_ASSETS_SHADERS_SPONZA_FULL_VERT,
    ASSET_SHADER_ASSETS_SHADERS_SPONZA_DIFFUSE_VERT,
    ASSET_SHADER_ASSETS_SHADERS_SPONZA_ALPHA_VERT,
};
static const nt_hash64_t s_fragment_ids[3] = {
    ASSET_SHADER_ASSETS_SHADERS_SPONZA_FULL_FRAG,
    ASSET_SHADER_ASSETS_SHADERS_SPONZA_DIFFUSE_FRAG,
    ASSET_SHADER_ASSETS_SHADERS_SPONZA_ALPHA_FRAG,
};

static void count_material_assignment(nt_material_t material, nt_program_t program) {
    s_assignment_count++;
    nt_material_set_program(material, program);
}

static void load_asset(nt_hash64_t rid, uint8_t type, uint16_t version, const void *data, uint32_t size) {
    const uint32_t header_size = (uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry));
    const uint32_t total_size = header_size + size;
    TEST_ASSERT_LESS_THAN_UINT32(TEST_PACK_COUNT, s_test_pack_count);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(TEST_PACK_BYTES, total_size);
    uint8_t *blob = s_test_pack_blobs[s_test_pack_count];
    memset(blob, 0, TEST_PACK_BYTES);
    NtPackHeader *header = (NtPackHeader *)blob;
    *header = (NtPackHeader){
        .magic = NT_PACK_MAGIC,
        .version = NT_PACK_VERSION,
        .asset_count = 1,
        .header_size = header_size,
        .total_size = total_size,
    };
    NtAssetEntry *entry = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    *entry = (NtAssetEntry){.resource_id = rid.value, .offset = header_size, .size = size, .format_version = version, .asset_type = type};
    memcpy(blob + header_size, data, size);
    header->checksum = nt_crc32(blob + header_size, size);

    const nt_hash32_t pack = {.value = ++s_test_pack_count};
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pack, blob, total_size));
}

static void load_manifest(void) {
    uint8_t bytes[sizeof(NtBlobAssetHeader) + sizeof(SponzaManifestHeader) + (TEST_NODE_COUNT * sizeof(SponzaManifestNode))] = {0};
    const NtBlobAssetHeader blob_header = {.magic = NT_BLOB_MAGIC, .version = NT_BLOB_VERSION};
    const SponzaManifestHeader manifest_header = {.node_count = TEST_NODE_COUNT};
    memcpy(bytes, &blob_header, sizeof(blob_header));
    memcpy(bytes + sizeof(blob_header), &manifest_header, sizeof(manifest_header));
    for (uint32_t i = 0; i < TEST_NODE_COUNT; i++) {
        SponzaManifestNode node = {.mesh_rid = 100U + i, .base_color = {1.0F, 1.0F, 1.0F, 1.0F}, .shader_type = (i == 3U) ? UINT8_MAX : (uint8_t)i};
        node.transform[0] = 1.0F;
        node.transform[5] = 1.0F;
        node.transform[10] = 1.0F;
        node.transform[15] = 1.0F;
        memcpy(bytes + sizeof(blob_header) + sizeof(manifest_header) + (i * sizeof(node)), &node, sizeof(node));
    }
    load_asset(ASSET_BLOB_SPONZA_MANIFEST, NT_ASSET_BLOB, NT_BLOB_VERSION, bytes, sizeof(bytes));
    nt_resource_step();
    TEST_ASSERT_TRUE(nt_resource_is_ready(s_manifest_handle));
    load_scene_from_manifest();
    TEST_ASSERT_TRUE(s_scene_loaded);
    TEST_ASSERT_EQUAL_UINT32(TEST_NODE_COUNT, s_entity_count);
}

static void load_stage(uint32_t type, nt_shader_stage_t stage) {
    static const char source[] = "void main(){}";
    uint8_t bytes[sizeof(NtShaderCodeHeader) + sizeof(source)];
    const NtShaderCodeHeader header = {.magic = NT_SHADER_CODE_MAGIC, .version = NT_SHADER_CODE_VERSION, .stage = (uint8_t)stage, .code_size = sizeof(source)};
    memcpy(bytes, &header, sizeof(header));
    memcpy(bytes + sizeof(header), source, sizeof(source));
    load_asset(stage == NT_SHADER_STAGE_VERTEX ? s_vertex_ids[type] : s_fragment_ids[type], NT_ASSET_SHADER_CODE, NT_SHADER_CODE_VERSION, bytes, sizeof(bytes));
}

static void load_all_stages(void) {
    for (uint32_t type = 0; type < 3U; type++) {
        load_stage(type, NT_SHADER_STAGE_VERTEX);
        load_stage(type, NT_SHADER_STAGE_FRAGMENT);
    }
    nt_resource_step();
}

static void assert_scene_programs(void) {
    for (uint32_t i = 0; i < TEST_NODE_COUNT; i++) {
        const nt_material_info_t *info = nt_material_get_info(s_materials[i]);
        TEST_ASSERT_NOT_NULL(info);
        TEST_ASSERT_EQUAL_UINT32(s_programs[s_expected_types[i]].program.id, info->program.id);
        TEST_ASSERT_TRUE(nt_gfx_program_ready(info->program));
        TEST_ASSERT_EQUAL_UINT32(s_materials[i].id, nt_material_comp_handle(s_entities[i])->id);
    }
}

static void assert_idle_link(void) {
    s_assignment_count = 0;
    link_programs();
    TEST_ASSERT_EQUAL_UINT32(0, s_assignment_count);
}

void setUp(void) {
    s_assignment_count = 0;
    s_test_pack_count = 0;
    s_entity_count = 0;
    s_scene_loaded = false;
    memset(s_programs, 0, sizeof(s_programs));
    memset(s_materials, 0, sizeof(s_materials));
    memset(s_entities, 0, sizeof(s_entities));
    memset(s_tex_handles, 0, sizeof(s_tex_handles));

    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    gfx.max_programs = 8;
    nt_gfx_init(&gfx);
    nt_http_init();
    nt_fs_init();
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_resource_set_activate_time_budget(0);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);
    nt_material_init(&(nt_material_desc_t){.max_materials = TEST_NODE_COUNT});
    nt_entity_init(&(nt_entity_desc_t){.max_entities = TEST_NODE_COUNT});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = TEST_NODE_COUNT});
    nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = TEST_NODE_COUNT});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = TEST_NODE_COUNT});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = TEST_NODE_COUNT});

    for (uint32_t type = 0; type < 3U; type++) {
        s_programs[type].vs = nt_resource_request(s_vertex_ids[type], NT_ASSET_SHADER_CODE);
        s_programs[type].fs = nt_resource_request(s_fragment_ids[type], NT_ASSET_SHADER_CODE);
    }
    s_manifest_handle = nt_resource_request(ASSET_BLOB_SPONZA_MANIFEST, NT_ASSET_BLOB);
}

void tearDown(void) {
    drop_programs();
    nt_drawable_comp_shutdown();
    nt_material_comp_shutdown();
    nt_mesh_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_gfx_shutdown();
    nt_hash_shutdown();
}

static void test_stages_before_manifest_assigns_at_material_creation(void) {
    load_all_stages();
    link_programs();
    load_manifest();
    assert_scene_programs();
    assert_idle_link();
}

static void test_manifest_before_stages_assigns_all_programs_in_one_step(void) {
    load_manifest();
    for (uint32_t i = 0; i < TEST_NODE_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT32(0, nt_material_get_info(s_materials[i])->program.id);
    }
    load_all_stages();
    s_assignment_count = 0;
    link_programs();
    TEST_ASSERT_EQUAL_UINT32(TEST_NODE_COUNT, s_assignment_count);
    assert_scene_programs();
    assert_idle_link();
}

static void test_shader_pairs_arrive_in_separate_steps(void) {
    load_manifest();
    load_stage(SPONZA_SHADER_FULL, NT_SHADER_STAGE_VERTEX);
    nt_resource_step();
    link_programs();
    TEST_ASSERT_EQUAL_UINT32(0, s_programs[SPONZA_SHADER_FULL].program.id);

    load_stage(SPONZA_SHADER_FULL, NT_SHADER_STAGE_FRAGMENT);
    load_stage(SPONZA_SHADER_DIFFUSE, NT_SHADER_STAGE_VERTEX);
    nt_resource_step();
    link_programs();
    TEST_ASSERT_TRUE(nt_gfx_program_ready(nt_material_get_info(s_materials[0])->program));
    TEST_ASSERT_EQUAL_UINT32(0, nt_material_get_info(s_materials[1])->program.id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_material_get_info(s_materials[2])->program.id);
    assert_idle_link();

    load_stage(SPONZA_SHADER_DIFFUSE, NT_SHADER_STAGE_FRAGMENT);
    load_stage(SPONZA_SHADER_ALPHA, NT_SHADER_STAGE_VERTEX);
    load_stage(SPONZA_SHADER_ALPHA, NT_SHADER_STAGE_FRAGMENT);
    nt_resource_step();
    link_programs();
    assert_scene_programs();
    assert_idle_link();
}

static void test_context_restore_reassigns_existing_material_handles(void) {
    load_manifest();
    load_all_stages();
    link_programs();
    assert_scene_programs();
    nt_material_t materials[TEST_NODE_COUNT];
    nt_program_t programs[3];
    memcpy(materials, s_materials, sizeof(materials));
    for (uint32_t type = 0; type < 3U; type++) {
        programs[type] = s_programs[type].program;
    }

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    drop_programs();
    nt_resource_invalidate(NT_ASSET_SHADER_CODE);
    nt_gfx_end_frame();
    for (uint32_t i = 0; i < TEST_NODE_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT32(materials[i].id, s_materials[i].id);
        TEST_ASSERT_FALSE(nt_gfx_program_ready(nt_material_get_info(s_materials[i])->program));
    }
    assert_idle_link();

    nt_resource_step();
    s_assignment_count = 0;
    link_programs();
    TEST_ASSERT_EQUAL_UINT32(TEST_NODE_COUNT, s_assignment_count);
    for (uint32_t type = 0; type < 3U; type++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(programs[type].id, s_programs[type].program.id);
    }
    assert_scene_programs();
    assert_idle_link();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stages_before_manifest_assigns_at_material_creation);
    RUN_TEST(test_manifest_before_stages_assigns_all_programs_in_one_step);
    RUN_TEST(test_shader_pairs_arrive_in_separate_steps);
    RUN_TEST(test_context_restore_reassigns_existing_material_handles);
    return UNITY_END();
}
