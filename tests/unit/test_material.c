/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "material/nt_material.h"
#include "resource/nt_resource.h"
#include "hash/nt_hash.h"
#include "nt_pack_format.h"
#include "unity.h"
/* clang-format on */

/* ---- Virtual pack ID counter (unique per test) ---- */

static uint32_t s_vpack_counter;

/* ---- Unity setUp / tearDown ---- */

static nt_result_t init_material_defaults(void) {
    nt_material_desc_t d = nt_material_desc_defaults();
    return nt_material_init(&d);
}

void setUp(void) {
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    init_material_defaults();
    s_vpack_counter = 0;
}

void tearDown(void) {
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_hash_shutdown();
}

/* ---- Helper: create virtual pack and register a resource with a given handle ---- */

static nt_resource_t register_test_resource(const char *name, uint8_t asset_type, uint32_t runtime_handle) {
    /* Create a uniquely-named virtual pack for this resource */
    char pack_name[64];
    (void)snprintf(pack_name, sizeof(pack_name), "vp_%s_%u", name, s_vpack_counter++);
    nt_hash32_t pid = nt_hash32_str(pack_name);
    nt_hash64_t rid = nt_hash64_str(name);

    nt_resource_create_pack(pid, 0);
    nt_resource_register(pid, rid, asset_type, runtime_handle);

    return nt_resource_request(rid, asset_type);
}

/* ---- Helper: build a basic material descriptor ---- */

static nt_material_create_desc_t make_test_desc(void) {
    nt_material_create_desc_t d;
    memset(&d, 0, sizeof(d));
    d.vs = (nt_resource_t){.id = 1};
    d.fs = (nt_resource_t){.id = 2};
    d.textures[0].name = "u_albedo";
    d.textures[0].resource = (nt_resource_t){.id = 3};
    d.texture_count = 1;
    d.params[0].name = "u_roughness";
    d.params[0].value[0] = 0.5F;
    d.params[0].value[1] = 0.0F;
    d.params[0].value[2] = 0.0F;
    d.params[0].value[3] = 0.0F;
    d.param_count = 1;
    d.attr_map[0].stream_name = "position";
    d.attr_map[0].location = 0;
    d.attr_map_count = 1;
    d.depth_test = true;
    d.depth_write = true;
    d.cull_mode = NT_CULL_BACK;
    return d;
}

/* ---- Test 1: init/shutdown lifecycle ---- */

void test_init_shutdown(void) {
    /* tearDown calls shutdown, so re-init to test return values */
    nt_material_shutdown();
    nt_result_t r = init_material_defaults();
    TEST_ASSERT_EQUAL(NT_OK, r);

    /* Shutdown and re-init should succeed */
    nt_material_shutdown();
    nt_result_t r2 = init_material_defaults();
    TEST_ASSERT_EQUAL(NT_OK, r2);
}

/* ---- Test 2: create returns valid handle ---- */

void test_create_basic(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    TEST_ASSERT_TRUE(mat.id != 0);
}

/* ---- Test 3: texture count stored correctly ---- */

void test_create_stores_texture_count(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT8(1, info->tex_count);
}

/* ---- Test 4: param count stored correctly ---- */

void test_create_stores_param_count(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT8(1, info->param_count);
}

/* ---- Test 5: param values stored correctly ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_create_stores_param_values(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    /* Compare as raw uint32 to avoid float assertion issues */
    uint32_t expected_bits;
    uint32_t actual_bits;
    float expected_val = 0.5F;
    memcpy(&expected_bits, &expected_val, sizeof(uint32_t));
    memcpy(&actual_bits, &info->params[0][0], sizeof(uint32_t));
    TEST_ASSERT_EQUAL_UINT32(expected_bits, actual_bits);

    float zero = 0.0F;
    uint32_t zero_bits;
    memcpy(&zero_bits, &zero, sizeof(uint32_t));
    memcpy(&actual_bits, &info->params[0][1], sizeof(uint32_t));
    TEST_ASSERT_EQUAL_UINT32(zero_bits, actual_bits);
    memcpy(&actual_bits, &info->params[0][2], sizeof(uint32_t));
    TEST_ASSERT_EQUAL_UINT32(zero_bits, actual_bits);
    memcpy(&actual_bits, &info->params[0][3], sizeof(uint32_t));
    TEST_ASSERT_EQUAL_UINT32(zero_bits, actual_bits);
}

/* ---- Test 6: render state stored correctly ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_create_stores_render_state(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->depth_test);
    TEST_ASSERT_TRUE(info->depth_write);
    TEST_ASSERT_EQUAL(NT_CULL_BACK, info->cull_mode);
    TEST_ASSERT_EQUAL(NT_BLEND_MODE_OPAQUE, info->blend_mode);
}

/* ---- Test 7: attr_map stored correctly ---- */

void test_create_stores_attr_map(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT8(1, info->attr_map_count);
    TEST_ASSERT_EQUAL_UINT8(0, info->attr_map_locations[0]);
}

/* ---- Test 8: texture names hashed ---- */

void test_create_hashes_texture_names(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("u_albedo").value, info->tex_name_hashes[0]);
}

/* ---- Test 9: param names hashed ---- */

void test_create_hashes_param_names(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("u_roughness").value, info->param_name_hashes[0]);
}

/* ---- Test 10: valid returns true for live handle ---- */

void test_valid_true_after_create(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    TEST_ASSERT_TRUE(nt_material_valid(mat));
}

/* ---- Test 11: valid returns false after destroy ---- */

void test_valid_false_after_destroy(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    nt_material_destroy(mat);
    TEST_ASSERT_FALSE(nt_material_valid(mat));
}

/* ---- Test 12: valid returns false for invalid handle ---- */

void test_valid_false_for_invalid(void) { TEST_ASSERT_FALSE(nt_material_valid(NT_MATERIAL_INVALID)); }

/* ---- Test 13: destroy stale handle is no-op ---- */

void test_destroy_stale_noop(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    nt_material_destroy(mat);
    /* Destroying the same stale handle again should not crash */
    nt_material_destroy(mat);
    TEST_ASSERT_FALSE(nt_material_valid(mat));
}

/* ---- Test 14: pool full returns invalid ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_pool_full_returns_invalid(void) {
    /* Re-init with a small pool to test pool exhaustion */
    nt_material_shutdown();
    nt_material_init(&(nt_material_desc_t){.max_materials = 4});

    nt_material_create_desc_t d = make_test_desc();
    nt_material_t handles[4];
    for (int i = 0; i < 4; i++) {
        handles[i] = nt_material_create(&d);
        TEST_ASSERT_TRUE(handles[i].id != 0);
    }
    /* One more should fail */
    nt_material_t overflow = nt_material_create(&d);
    TEST_ASSERT_EQUAL_UINT32(0, overflow.id);

    /* Cleanup */
    for (int i = 0; i < 4; i++) {
        nt_material_destroy(handles[i]);
    }
}

/* ---- Test 15: step resolves shaders ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_step_resolves_shaders(void) {
    /* Use virtual packs to register resources with known runtime handles */
    nt_resource_t vs_res = register_test_resource("test_vs", NT_ASSET_SHADER_CODE, 100);
    nt_resource_t fs_res = register_test_resource("test_fs", NT_ASSET_SHADER_CODE, 200);
    nt_resource_step();

    /* Create material referencing those resource handles */
    nt_material_create_desc_t d = make_test_desc();
    d.vs = vs_res;
    d.fs = fs_res;
    nt_material_t mat = nt_material_create(&d);

    nt_material_step();

    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT32(100, info->resolved_vs);
    TEST_ASSERT_EQUAL_UINT32(200, info->resolved_fs);
}

/* ---- Test 16: ready true when both shaders resolved ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_step_ready_true_when_both_shaders(void) {
    nt_resource_t vs_res = register_test_resource("ready_vs", NT_ASSET_SHADER_CODE, 10);
    nt_resource_t fs_res = register_test_resource("ready_fs", NT_ASSET_SHADER_CODE, 20);
    nt_resource_step();

    nt_material_create_desc_t d = make_test_desc();
    d.vs = vs_res;
    d.fs = fs_res;
    nt_material_t mat = nt_material_create(&d);

    nt_material_step();

    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->ready);
}

/* ---- Test 17: ready false when one shader missing ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_step_ready_false_when_missing_shader(void) {
    /* VS is registered (will resolve to handle), FS is just a bare request (no pack entry) */
    nt_resource_t vs_res = register_test_resource("miss_vs", NT_ASSET_SHADER_CODE, 10);
    nt_resource_t fs_res = nt_resource_request(nt_hash64_str("miss_fs"), NT_ASSET_SHADER_CODE);
    nt_resource_step();

    nt_material_create_desc_t d = make_test_desc();
    d.vs = vs_res;
    d.fs = fs_res;
    nt_material_t mat = nt_material_create(&d);

    nt_material_step();

    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_FALSE(info->ready);
}

/* ---- Test 18: version increments on shader change ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_step_version_increments_on_change(void) {
    nt_resource_t vs_res = register_test_resource("ver_vs", NT_ASSET_SHADER_CODE, 10);
    nt_resource_t fs_res = register_test_resource("ver_fs", NT_ASSET_SHADER_CODE, 20);
    nt_resource_step();

    nt_material_create_desc_t d = make_test_desc();
    d.vs = vs_res;
    d.fs = fs_res;
    nt_material_t mat = nt_material_create(&d);

    /* First step: version goes from 0 to 1 (handles change from initial 0 to 10/20) */
    nt_material_step();
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    uint32_t v1 = info->version;
    TEST_ASSERT_TRUE(v1 > 0);

    /* Change the VS shader handle by re-registering with a different handle */
    nt_hash32_t new_pid = nt_hash32_str("ver_vs_new");
    nt_resource_create_pack(new_pid, 10); /* higher priority to override */
    nt_resource_register(new_pid, nt_hash64_str("ver_vs"), NT_ASSET_SHADER_CODE, 99);
    nt_resource_step();
    nt_material_step();

    TEST_ASSERT_TRUE(info->version > v1);
}

/* ---- Test 19: version stable when unchanged ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_step_version_stable_when_unchanged(void) {
    nt_resource_t vs_res = register_test_resource("stab_vs", NT_ASSET_SHADER_CODE, 10);
    nt_resource_t fs_res = register_test_resource("stab_fs", NT_ASSET_SHADER_CODE, 20);
    nt_resource_step();

    nt_material_create_desc_t d = make_test_desc();
    d.vs = vs_res;
    d.fs = fs_res;
    nt_material_t mat = nt_material_create(&d);

    nt_material_step();
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    uint32_t v1 = info->version;

    /* Step again without changing anything */
    nt_material_step();
    TEST_ASSERT_EQUAL_UINT32(v1, info->version);
}

/* ---- Test 20: step resolves textures ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_step_resolves_textures(void) {
    nt_resource_t tex_res = register_test_resource("test_tex", NT_ASSET_TEXTURE, 42);
    nt_resource_step();

    nt_material_create_desc_t d = make_test_desc();
    d.textures[0].name = "u_albedo";
    d.textures[0].resource = tex_res;
    d.texture_count = 1;
    nt_material_t mat = nt_material_create(&d);

    nt_material_step();

    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT32(42, info->resolved_tex[0]);
}

/* ---- Test 21: get_info returns NULL for invalid handle ---- */

void test_get_info_returns_null_for_invalid(void) {
    const nt_material_info_t *info = nt_material_get_info(NT_MATERIAL_INVALID);
    TEST_ASSERT_NULL(info);
}

/* ---- Test 22: entity_param names hashed and stored ---- */

void test_create_stores_entity_params(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.entity_params[0].name = "u_dissolve";
    d.entity_param_count = 1;
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT8(1, info->entity_param_count);
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("u_dissolve").value, info->entity_param_hashes[0]);
}

/* ---- Test: has_param returns true for existing param ---- */

void test_has_param_returns_true(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    TEST_ASSERT_TRUE(nt_material_has_param(mat, "u_roughness"));
}

/* ---- Test: has_param returns false for unknown param ---- */

void test_has_param_returns_false(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    TEST_ASSERT_FALSE(nt_material_has_param(mat, "u_nonexistent"));
}

/* ---- Test: has_param returns false for invalid handle ---- */

void test_has_param_invalid_handle(void) { TEST_ASSERT_FALSE(nt_material_has_param(NT_MATERIAL_INVALID, "u_roughness")); }

/* ---- Test: set_param updates all four components ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_set_param_updates_all_four(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.params[0].name = "u_color";
    d.params[0].value[0] = 0.0F;
    d.params[0].value[1] = 0.0F;
    d.params[0].value[2] = 0.0F;
    d.params[0].value[3] = 0.0F;
    d.param_count = 1;
    nt_material_t mat = nt_material_create(&d);

    float new_val[4] = {1.0F, 0.5F, 0.25F, 1.0F};
    nt_material_set_param(mat, "u_color", new_val);

    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    for (int c = 0; c < 4; c++) {
        uint32_t expected_bits;
        uint32_t actual_bits;
        memcpy(&expected_bits, &new_val[c], sizeof(uint32_t));
        memcpy(&actual_bits, &info->params[0][c], sizeof(uint32_t));
        TEST_ASSERT_EQUAL_UINT32(expected_bits, actual_bits);
    }
}

/* ---- Test: set_param_component updates only one component ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_set_param_component_updates_one(void) {
    nt_material_create_desc_t d = make_test_desc();
    /* d already has u_roughness = {0.5, 0, 0, 0} */
    nt_material_t mat = nt_material_create(&d);

    nt_material_set_param_component(mat, "u_roughness", 0, 0.8F);

    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);

    /* Component 0 should be 0.8 */
    float expected = 0.8F;
    uint32_t expected_bits;
    uint32_t actual_bits;
    memcpy(&expected_bits, &expected, sizeof(uint32_t));
    memcpy(&actual_bits, &info->params[0][0], sizeof(uint32_t));
    TEST_ASSERT_EQUAL_UINT32(expected_bits, actual_bits);

    /* Components 1-3 should still be 0.0 */
    float zero = 0.0F;
    uint32_t zero_bits;
    memcpy(&zero_bits, &zero, sizeof(uint32_t));
    for (int c = 1; c < 4; c++) {
        memcpy(&actual_bits, &info->params[0][c], sizeof(uint32_t));
        TEST_ASSERT_EQUAL_UINT32(zero_bits, actual_bits);
    }
}

/* ---- Assert-catching helper (setjmp/longjmp via hookable nt_assert_handler) ---- */

static jmp_buf s_assert_jmp;

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)expr;
    (void)file;
    (void)line;
    longjmp(s_assert_jmp, 1);
}

/* ---- Test: set_param with invalid handle fires NT_ASSERT ---- */

void test_set_param_invalid_handle(void) {
    float val[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    nt_assert_handler = test_assert_handler;
    if (setjmp(s_assert_jmp) == 0) {
        nt_material_set_param(NT_MATERIAL_INVALID, "u_roughness", val);
        nt_assert_handler = NULL;
        TEST_FAIL_MESSAGE("Expected NT_ASSERT to fire for invalid handle");
    }
    nt_assert_handler = NULL;
    TEST_PASS();
}

/* ---- main ---- */

int main(void) {
    UNITY_BEGIN();

    /* Init / shutdown */
    RUN_TEST(test_init_shutdown);

    /* Create / query */
    RUN_TEST(test_create_basic);
    RUN_TEST(test_create_stores_texture_count);
    RUN_TEST(test_create_stores_param_count);
    RUN_TEST(test_create_stores_param_values);
    RUN_TEST(test_create_stores_render_state);
    RUN_TEST(test_create_stores_attr_map);
    RUN_TEST(test_create_hashes_texture_names);
    RUN_TEST(test_create_hashes_param_names);
    RUN_TEST(test_create_stores_entity_params);

    /* Valid / destroy */
    RUN_TEST(test_valid_true_after_create);
    RUN_TEST(test_valid_false_after_destroy);
    RUN_TEST(test_valid_false_for_invalid);
    RUN_TEST(test_destroy_stale_noop);

    /* Pool exhaustion */
    RUN_TEST(test_pool_full_returns_invalid);

    /* Step: resolve + change detection */
    RUN_TEST(test_step_resolves_shaders);
    RUN_TEST(test_step_ready_true_when_both_shaders);
    RUN_TEST(test_step_ready_false_when_missing_shader);
    RUN_TEST(test_step_version_increments_on_change);
    RUN_TEST(test_step_version_stable_when_unchanged);
    RUN_TEST(test_step_resolves_textures);

    /* Query edge cases */
    RUN_TEST(test_get_info_returns_null_for_invalid);

    /* set_param API */
    RUN_TEST(test_has_param_returns_true);
    RUN_TEST(test_has_param_returns_false);
    RUN_TEST(test_has_param_invalid_handle);
    RUN_TEST(test_set_param_updates_all_four);
    RUN_TEST(test_set_param_component_updates_one);
    RUN_TEST(test_set_param_invalid_handle);

    return UNITY_END();
}
