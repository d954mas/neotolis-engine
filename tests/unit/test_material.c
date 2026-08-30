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
    d.program = (nt_program_t){.id = 1};
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

/* ---- Assert-catching helper (setjmp/longjmp via hookable nt_assert_handler) ---- */

static jmp_buf s_assert_jmp;

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)expr;
    (void)file;
    (void)line;
    longjmp(s_assert_jmp, 1);
}

static void assert_blend_rgb(nt_blend_state_t blend, nt_blend_factor_t src, nt_blend_factor_t dst, nt_blend_op_t op) {
    TEST_ASSERT_TRUE(blend.enabled);
    TEST_ASSERT_EQUAL(src, blend.src_rgb);
    TEST_ASSERT_EQUAL(dst, blend.dst_rgb);
    TEST_ASSERT_EQUAL(op, blend.op_rgb);
}

static void assert_blend_preserves_alpha(nt_blend_state_t blend) {
    TEST_ASSERT_EQUAL(NT_BLEND_ZERO, blend.src_alpha);
    TEST_ASSERT_EQUAL(NT_BLEND_ONE, blend.dst_alpha);
    TEST_ASSERT_EQUAL(NT_BLEND_OP_ADD, blend.op_alpha);
}

static void assert_blend_source_over_alpha(nt_blend_state_t blend) {
    TEST_ASSERT_EQUAL(NT_BLEND_ONE, blend.src_alpha);
    TEST_ASSERT_EQUAL(NT_BLEND_ONE_MINUS_SRC_ALPHA, blend.dst_alpha);
    TEST_ASSERT_EQUAL(NT_BLEND_OP_ADD, blend.op_alpha);
}

void test_blend_opaque_disables_blending(void) {
    nt_blend_state_t blend = nt_blend_opaque();
    TEST_ASSERT_FALSE(blend.enabled);
}

void test_blend_alpha_uses_straight_source(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    assert_blend_rgb(blend, NT_BLEND_SRC_ALPHA, NT_BLEND_ONE_MINUS_SRC_ALPHA, NT_BLEND_OP_ADD);
    assert_blend_source_over_alpha(blend);
}

void test_blend_alpha_premultiplied_uses_source_as_is(void) {
    nt_blend_state_t blend = nt_blend_alpha_premultiplied();
    assert_blend_rgb(blend, NT_BLEND_ONE, NT_BLEND_ONE_MINUS_SRC_ALPHA, NT_BLEND_OP_ADD);
    assert_blend_source_over_alpha(blend);
}

void test_blend_additive_presets_preserve_destination_alpha(void) {
    nt_blend_state_t straight = nt_blend_additive();
    assert_blend_rgb(straight, NT_BLEND_SRC_ALPHA, NT_BLEND_ONE, NT_BLEND_OP_ADD);
    assert_blend_preserves_alpha(straight);

    nt_blend_state_t premultiplied = nt_blend_additive_premultiplied();
    assert_blend_rgb(premultiplied, NT_BLEND_ONE, NT_BLEND_ONE, NT_BLEND_OP_ADD);
    assert_blend_preserves_alpha(premultiplied);
}

void test_blend_subtractive_presets_preserve_destination_alpha(void) {
    nt_blend_state_t straight = nt_blend_subtractive();
    assert_blend_rgb(straight, NT_BLEND_SRC_ALPHA, NT_BLEND_ONE, NT_BLEND_OP_REVERSE_SUBTRACT);
    assert_blend_preserves_alpha(straight);

    nt_blend_state_t premultiplied = nt_blend_subtractive_premultiplied();
    assert_blend_rgb(premultiplied, NT_BLEND_ONE, NT_BLEND_ONE, NT_BLEND_OP_REVERSE_SUBTRACT);
    assert_blend_preserves_alpha(premultiplied);
}

void test_blend_multiply_multiplies_rgb_and_preserves_destination_alpha(void) {
    nt_blend_state_t blend = nt_blend_multiply();
    assert_blend_rgb(blend, NT_BLEND_DST_COLOR, NT_BLEND_ZERO, NT_BLEND_OP_ADD);
    assert_blend_preserves_alpha(blend);
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
    d.blend = nt_blend_multiply();
    d.blend.constant_color[0] = 0.25F;
    d.blend.constant_color[1] = 0.5F;
    d.blend.constant_color[2] = 0.75F;
    d.blend.constant_color[3] = 1.0F;
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->depth_test);
    TEST_ASSERT_TRUE(info->depth_write);
    TEST_ASSERT_EQUAL(NT_CULL_BACK, info->cull_mode);
    TEST_ASSERT_EQUAL_MEMORY(&d.blend, &info->blend, sizeof(d.blend));
}

void test_disabled_blend_uses_canonical_render_state_hash(void) {
    nt_material_create_desc_t opaque_desc = make_test_desc();
    nt_material_create_desc_t disabled_alpha_desc = make_test_desc();
    opaque_desc.blend = nt_blend_opaque();
    disabled_alpha_desc.blend = nt_blend_alpha();
    disabled_alpha_desc.blend.enabled = false;
    disabled_alpha_desc.blend.constant_color[0] = 0.5F;

    nt_material_t opaque = nt_material_create(&opaque_desc);
    nt_material_t disabled_alpha = nt_material_create(&disabled_alpha_desc);
    const nt_material_info_t *opaque_info = nt_material_get_info(opaque);
    const nt_material_info_t *disabled_alpha_info = nt_material_get_info(disabled_alpha);

    TEST_ASSERT_EQUAL_UINT64(opaque_info->render_state_hash, disabled_alpha_info->render_state_hash);
}

void test_enabled_blend_changes_render_state_hash(void) {
    nt_material_create_desc_t alpha_desc = make_test_desc();
    nt_material_create_desc_t additive_desc = make_test_desc();
    alpha_desc.blend = nt_blend_alpha();
    additive_desc.blend = nt_blend_additive();

    nt_material_t alpha = nt_material_create(&alpha_desc);
    nt_material_t additive = nt_material_create(&additive_desc);
    const nt_material_info_t *alpha_info = nt_material_get_info(alpha);
    const nt_material_info_t *additive_info = nt_material_get_info(additive);

    TEST_ASSERT_NOT_EQUAL_UINT64(alpha_info->render_state_hash, additive_info->render_state_hash);
}

void test_blend_reserved_byte_is_canonicalized(void) {
    nt_material_create_desc_t clean_desc = make_test_desc();
    nt_material_create_desc_t dirty_desc = make_test_desc();
    clean_desc.blend = nt_blend_alpha();
    dirty_desc.blend = nt_blend_alpha();
    dirty_desc.blend._reserved = UINT8_MAX;

    nt_material_t clean = nt_material_create(&clean_desc);
    nt_material_t dirty = nt_material_create(&dirty_desc);
    const nt_material_info_t *clean_info = nt_material_get_info(clean);
    const nt_material_info_t *dirty_info = nt_material_get_info(dirty);

    TEST_ASSERT_EQUAL_UINT8(0, dirty_info->blend._reserved);
    TEST_ASSERT_EQUAL_UINT64(clean_info->render_state_hash, dirty_info->render_state_hash);
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

/* ---- Test 15: create stores the program handle ---- */

void test_create_stores_program(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.program = (nt_program_t){.id = 77};
    nt_material_t mat = nt_material_create(&d);

    const nt_material_info_t *info = nt_material_get_info(mat);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT32(77, info->program.id);
}

/* ---- Program assignment ---- */

/* A material may be created without a program and pick one up later -- the
 * first assignment and every later one are the same operation. */
void test_set_program_assigns_from_invalid(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.program = NT_PROGRAM_INVALID;
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);

    TEST_ASSERT_EQUAL_UINT32(0, info->program.id);
    nt_material_set_program(mat, (nt_program_t){.id = 9});
    TEST_ASSERT_EQUAL_UINT32(9, info->program.id);
}

/* Flat replace: A -> B directly, no clearing step in between. Renderers key
 * their caches on the program, so B never selects the entry built on A. */
void test_set_program_replaces_a_with_b(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.program = (nt_program_t){.id = 1};
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);

    nt_material_set_program(mat, (nt_program_t){.id = 2});
    TEST_ASSERT_EQUAL_UINT32(2, info->program.id);
}

/* Clearing is the same operation once more. */
void test_set_program_clears_to_invalid(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.program = (nt_program_t){.id = 1};
    nt_material_t mat = nt_material_create(&d);
    const nt_material_info_t *info = nt_material_get_info(mat);

    nt_material_set_program(mat, NT_PROGRAM_INVALID);
    TEST_ASSERT_EQUAL_UINT32(0, info->program.id);
}

/* ---- Diagnostics: a material that never gets a program ---- */

/* The renderers cannot catch this: they skip a material with no usable program
 * silently, so nothing downstream ever reports it. No threshold and no false
 * positive from a slow load -- the material is gone and never drew. */
void test_warns_when_destroyed_without_ever_being_ready(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.program = NT_PROGRAM_INVALID;
    nt_material_t never = nt_material_create(&d);
    nt_material_destroy(never);
    TEST_ASSERT_EQUAL_UINT32(1, nt_material_test_never_ready_destroy_count());

    /* A material that was ready at some point is not reported, even if its
     * program was cleared before shutdown. Readiness latches on assignment, so
     * no step has to run in between. */
    nt_material_t was_ready = nt_material_create(&(nt_material_create_desc_t){.program = (nt_program_t){.id = 2}});
    nt_material_set_program(was_ready, NT_PROGRAM_INVALID);
    nt_material_destroy(was_ready);
    TEST_ASSERT_EQUAL_UINT32(1, nt_material_test_never_ready_destroy_count());

    /* Same latch through set_program: created without one, given one, gone. */
    nt_material_t late = nt_material_create(&d);
    nt_material_set_program(late, (nt_program_t){.id = 3});
    nt_material_destroy(late);
    TEST_ASSERT_EQUAL_UINT32(1, nt_material_test_never_ready_destroy_count());
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

/* ---- Test: set_program with a stale handle fires NT_ASSERT ---- */

/* A stale handle silently doing nothing is how a material ends up with no
 * program and nobody noticing until the screen is black. */
void test_set_program_on_a_destroyed_material_asserts(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    nt_material_destroy(mat);

    nt_assert_handler = test_assert_handler;
    if (setjmp(s_assert_jmp) == 0) {
        nt_material_set_program(mat, (nt_program_t){.id = 1});
        nt_assert_handler = NULL;
        TEST_FAIL_MESSAGE("Expected NT_ASSERT to fire for a destroyed material");
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
    RUN_TEST(test_blend_opaque_disables_blending);
    RUN_TEST(test_blend_alpha_uses_straight_source);
    RUN_TEST(test_blend_alpha_premultiplied_uses_source_as_is);
    RUN_TEST(test_blend_additive_presets_preserve_destination_alpha);
    RUN_TEST(test_blend_subtractive_presets_preserve_destination_alpha);
    RUN_TEST(test_blend_multiply_multiplies_rgb_and_preserves_destination_alpha);
    RUN_TEST(test_create_stores_render_state);
    RUN_TEST(test_disabled_blend_uses_canonical_render_state_hash);
    RUN_TEST(test_enabled_blend_changes_render_state_hash);
    RUN_TEST(test_blend_reserved_byte_is_canonicalized);
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
    RUN_TEST(test_create_stores_program);
    RUN_TEST(test_set_program_assigns_from_invalid);
    RUN_TEST(test_set_program_replaces_a_with_b);
    RUN_TEST(test_set_program_clears_to_invalid);
    RUN_TEST(test_warns_when_destroyed_without_ever_being_ready);
    RUN_TEST(test_set_program_on_a_destroyed_material_asserts);
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
