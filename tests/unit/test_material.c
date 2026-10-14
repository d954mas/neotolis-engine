/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "resource/nt_resource.h"
#include "hash/nt_hash.h"
#include "nt_pack_format.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/nt_gfx_test_desc.h"
#include "test_helpers/nt_gfx_test_frame.h"
#include "unity.h"
/* clang-format on */

/* ---- Virtual pack ID counter (unique per test) ---- */

static uint32_t s_vpack_counter;

/* ---- Unity setUp / tearDown ---- */

static void init_material_defaults(void) {
    nt_material_desc_t d = nt_material_desc_defaults();
    nt_material_init(&d);
}

void setUp(void) {
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_gfx_fake_reset();
    nt_gfx_init(&NT_GFX_TEST_DESC(.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 32, .max_textures = 8, .max_meshes = 4, .max_vertex_inputs = 4, .max_render_targets = 4));
    nt_gfx_begin_frame();
    init_material_defaults();
    s_vpack_counter = 0;
}

void tearDown(void) {
    nt_material_shutdown();
    nt_gfx_end_frame();
    nt_gfx_shutdown();
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

/* The `Material` block of the test shaders: one vec4. */
static const float s_params[4] = {0.5F, 0.0F, 0.0F, 0.0F};

static nt_material_create_desc_t make_test_desc(void) {
    nt_material_create_desc_t d;
    memset(&d, 0, sizeof(d));
    d.program = (nt_program_t){.id = 1};
    d.textures[0].name = "u_albedo";
    d.textures[0].resource = (nt_resource_t){.id = 3};
    d.texture_count = 1;
    d.params = s_params;
    d.params_size = sizeof(s_params);
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
    /* Re-init restores a usable material pool. */
    nt_material_shutdown();
    init_material_defaults();
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    TEST_ASSERT_TRUE(nt_material_valid(mat));

    /* Shutdown and re-init should succeed */
    nt_material_shutdown();
    init_material_defaults();
    mat = nt_material_create(&d);
    TEST_ASSERT_TRUE(nt_material_valid(mat));
}

/* ---- Test 2: create returns valid handle ---- */

void test_create_basic(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    TEST_ASSERT_TRUE(mat.id != 0);
}

/* ---- Params: one uniform buffer per material ---- */

void test_create_makes_a_uniform_buffer_with_the_params(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    TEST_ASSERT_TRUE(nt_material_valid(mat));
    const nt_buffer_desc_t buffer = nt_gfx_fake_last_buffer_desc();
    TEST_ASSERT_EQUAL(NT_BUFFER_UNIFORM, buffer.type);
    TEST_ASSERT_EQUAL(NT_USAGE_DYNAMIC, buffer.usage);
    TEST_ASSERT_EQUAL_UINT32(sizeof(s_params), buffer.size);
    TEST_ASSERT_EQUAL_MEMORY(s_params, buffer.data, sizeof(s_params));
}

void test_material_without_params_makes_no_buffer(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.params = NULL;
    d.params_size = 0;
    nt_gfx_fake_reset();
    TEST_ASSERT_TRUE(nt_material_valid(nt_material_create(&d)));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_buffer_desc().size);
}

/* The bytes mirror a std140 block of 16-byte members, so the size is a multiple of 16. */
void test_create_asserts_bad_params(void) {
    static const float big[(NT_MATERIAL_MAX_PARAM_BYTES / 4) + 4] = {0};
    nt_material_create_desc_t d = make_test_desc();
    d.params_size = 20;
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));
    d.params = big;
    d.params_size = sizeof(big);
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));
    d.params = NULL;
    d.params_size = 16;
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));
    d.params = s_params;
    d.params_size = 0;
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));
}

/* Equal bytes write nothing; changed bytes update the buffer. */
void test_set_params_writes_the_buffer_only_on_change(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    const uint32_t updates = nt_gfx_fake_update_buffer_count();
    nt_material_set_params(mat, s_params, sizeof(s_params));
    TEST_ASSERT_EQUAL_UINT32(updates, nt_gfx_fake_update_buffer_count());
    const float changed[4] = {0.8F, 0.0F, 0.0F, 1.0F};
    nt_material_set_params(mat, changed, sizeof(changed));
    TEST_ASSERT_EQUAL_UINT32(updates + 1U, nt_gfx_fake_update_buffer_count());
    TEST_ASSERT_EQUAL_UINT32(sizeof(changed), nt_gfx_fake_last_update_buffer_size());
    TEST_ASSERT_EQUAL_MEMORY(changed, nt_gfx_fake_last_update_buffer_data(), sizeof(changed));
    const float last_changed[4] = {0.8F, 0.0F, 0.0F, 0.5F}; /* only the last member differs */
    nt_material_set_params(mat, last_changed, sizeof(last_changed));
    TEST_ASSERT_EQUAL_UINT32(updates + 2U, nt_gfx_fake_update_buffer_count());
}

void test_set_params_asserts_size_or_handle(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    static const float two[8] = {0};
    NT_TEST_EXPECT_ASSERT(nt_material_set_params(mat, two, sizeof(two)));
    NT_TEST_EXPECT_ASSERT(nt_material_set_params(NT_MATERIAL_INVALID, s_params, sizeof(s_params)));
    d.params = NULL;
    d.params_size = 0;
    nt_material_t no_params = nt_material_create(&d);
    NT_TEST_EXPECT_ASSERT(nt_material_set_params(no_params, s_params, sizeof(s_params)));
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
    TEST_ASSERT_EQUAL_MEMORY(&clean_info->blend, &dirty_info->blend, sizeof(clean_info->blend));
}

/* Lane inputs of the pipeline / vertex-input keys are rejected at the API boundary,
 * not on the first draw. */
void test_create_asserts_out_of_range_key_lanes(void) {
    nt_material_create_desc_t d = make_test_desc();
    const int bad_cull = NT_CULL_FRONT + 1; /* memcpy: an enum cast of a literal trips the analyzer */
    memcpy(&d.cull_mode, &bad_cull, sizeof(d.cull_mode));
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));

    d = make_test_desc();
    d.blend = nt_blend_alpha();
    d.blend.src_rgb = 16;
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));

    d = make_test_desc();
    d.blend = nt_blend_alpha();
    d.blend.op_alpha = 8;
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));

    d = make_test_desc();
    d.attr_map[0].location = 16;
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));

    /* Ranges hold for a disabled blend too: garbage is a bug, not "don't care". */
    d = make_test_desc();
    d.blend = nt_blend_opaque();
    d.blend.src_rgb = 16;
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));
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

/* ---- Uniform names are mandatory ---- */

void test_create_asserts_null_texture_name(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.textures[0].name = NULL;
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));
}

/* Two slots on one sampler name would fight over its unit at every draw. */
void test_create_asserts_duplicate_texture_name(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.textures[1].name = d.textures[0].name;
    d.textures[1].resource = (nt_resource_t){.id = 4};
    d.texture_count = 2;
    NT_TEST_EXPECT_ASSERT(nt_material_create(&d));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "same sampler uniform"));
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

/* ---- Apply ---- */

static nt_texture_t make_test_texture(uint8_t marker) {
    uint8_t pixels[4 * 4 * 4] = {marker};
    return nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .data = pixels, .format = NT_TEXTURE_FORMAT_RGBA8});
}

/* A program sampling u_albedo (the material's) and u_page (a draw texture). */
static nt_pipeline_t begin_apply_pass(void) {
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_albedo", "u_page"}, 2);
    nt_test_gfx_link_wait(program);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_test_frame_begin_pass();
    nt_gfx_bind_pipeline(pipeline);
    return pipeline;
}

/* The params buffer binds at the Material slot; the set is the material's resolved textures
 * followed by the draw textures. */
void test_apply_binds_the_params_buffer_and_the_complete_texture_set(void) {
    const nt_texture_t albedo = make_test_texture(1);
    const nt_texture_t page = make_test_texture(2);
    nt_resource_t albedo_res = register_test_resource("albedo", NT_ASSET_TEXTURE, albedo.id);
    nt_resource_step();
    nt_material_create_desc_t d = make_test_desc();
    d.textures[0].resource = albedo_res;
    nt_material_t mat = nt_material_create(&d);
    begin_apply_pass();
    const nt_gfx_texture_binding_t draw = {.name = nt_hash32_str("u_page"), .texture = page, .sampler = NT_SAMPLER_DEFAULT};
    nt_gfx_fake_reset();
    nt_material_apply(mat, &draw, 1);
    nt_material_apply(mat, &draw, 1); /* equal: CACHE in gfx */
    nt_test_frame_close();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_ubo_bind_count());
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_SLOT_MATERIAL, nt_gfx_fake_ubo_bind_at(0).slot);
    TEST_ASSERT_EQUAL_UINT32(sizeof(s_params), nt_gfx_fake_ubo_bind_at(0).size);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    nt_gfx_begin_frame();
}

/* A material without params unbinds the Material slot, so a program that reads it asserts
 * instead of reading the previous material's values. */
void test_apply_without_params_unbinds_the_material_slot(void) {
    const nt_texture_t page = make_test_texture(2);
    nt_material_create_desc_t d = make_test_desc();
    d.texture_count = 0;
    nt_material_t with_params = nt_material_create(&d);
    d.params = NULL;
    d.params_size = 0;
    nt_material_t without_params = nt_material_create(&d);
    nt_gfx_fake_set_block(NT_GFX_SLOT_MATERIAL, 16);
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_page"}, 1);
    nt_gfx_fake_set_block(NT_GFX_SLOT_MATERIAL, 0);
    nt_test_gfx_link_wait(program);
    nt_test_frame_begin_pass();
    nt_gfx_bind_pipeline(nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program}));
    nt_gfx_bind_vertex_input(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0}));
    const nt_gfx_texture_binding_t draw = {.name = nt_hash32_str("u_page"), .texture = page, .sampler = NT_SAMPLER_DEFAULT};
    nt_material_apply(with_params, &draw, 1);
    nt_gfx_draw(0, 3);
    nt_material_apply(without_params, &draw, 1);
    NT_TEST_EXPECT_ASSERT(nt_gfx_draw(3, 3));
    nt_test_frame_close();
    nt_gfx_begin_frame();
}

void test_apply_asserts_too_many_draw_textures(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t mat = nt_material_create(&d);
    begin_apply_pass();
    const nt_gfx_texture_binding_t draws[NT_GFX_MAX_TEXTURE_SLOTS] = {{0}};
    NT_TEST_EXPECT_ASSERT(nt_material_apply(mat, draws, NT_GFX_MAX_TEXTURE_SLOTS));
    NT_TEST_EXPECT_ASSERT(nt_material_apply(mat, NULL, 1));
    nt_test_frame_close();
    nt_gfx_begin_frame();
}

/* ---- Lifetime and restore ---- */

/* Buffers left for materials after the frame storage streams of NT_GFX_TEST_DESC. */
#define MATERIAL_TEST_FREE_BUFFERS (32U - 4U)

/* The restore rebuilds every params buffer from the CPU copy, including bytes written while the
 * context was lost and a material created then; a second call leaks nothing. */
void test_restore_gpu_rebuilds_params_buffers(void) {
    nt_material_create_desc_t d = make_test_desc();
    d.texture_count = 0;
    nt_material_t before = nt_material_create(&d);
    const float changed[4] = {0.25F, 0.0F, 0.0F, 0.0F};
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_material_set_params(before, changed, sizeof(changed)); /* kept in the copy while lost */
    nt_material_t during = nt_material_create(&d);
    TEST_ASSERT_TRUE(nt_material_valid(during));
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);

    nt_gfx_fake_reset();
    nt_material_restore_gpu();
    TEST_ASSERT_EQUAL(NT_BUFFER_UNIFORM, nt_gfx_fake_last_buffer_desc().type);
    TEST_ASSERT_EQUAL_MEMORY(s_params, nt_gfx_fake_last_buffer_desc().data, sizeof(s_params)); /* `during` comes last */
    nt_material_destroy(during);
    nt_material_restore_gpu(); /* `before` is now the only params material */
    TEST_ASSERT_EQUAL_MEMORY(changed, nt_gfx_fake_last_buffer_desc().data, sizeof(changed));
    during = nt_material_create(&d);
    nt_material_restore_gpu();
    /* Two materials hold buffers: the rest of the pool is still free after three restores. */
    for (uint32_t i = 0; i < MATERIAL_TEST_FREE_BUFFERS - 2U; i++) {
        TEST_ASSERT_TRUE(nt_material_valid(nt_material_create(&d)));
    }
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_test_gfx_link_wait(program);
    nt_test_frame_begin_pass();
    nt_gfx_bind_pipeline(nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program}));
    nt_material_apply(before, NULL, 0);
    nt_material_apply(during, NULL, 0);
    nt_test_frame_close();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_ubo_bind_count());
    nt_gfx_begin_frame();
}

/* Destroy and shutdown release the params buffers: each fills the buffer pool exactly, so one
 * leaked buffer overflows the next fill. */
void test_destroy_and_shutdown_release_params_buffers(void) {
    nt_material_create_desc_t d = make_test_desc();
    for (uint32_t i = 0; i < 64; i++) {
        nt_material_destroy(nt_material_create(&d));
    }
    for (uint32_t i = 0; i < MATERIAL_TEST_FREE_BUFFERS; i++) {
        TEST_ASSERT_TRUE(nt_material_valid(nt_material_create(&d)));
    }
    nt_material_shutdown();
    init_material_defaults();
    for (uint32_t i = 0; i < MATERIAL_TEST_FREE_BUFFERS; i++) {
        TEST_ASSERT_TRUE(nt_material_valid(nt_material_create(&d)));
    }
}

/* A params buffer follows the gfx frame rule; a material without params has nothing to destroy. */
void test_destroying_a_params_material_in_a_drawn_frame_asserts(void) {
    nt_material_create_desc_t d = make_test_desc();
    nt_material_t with_params = nt_material_create(&d);
    d.params = NULL;
    d.params_size = 0;
    nt_material_t without_params = nt_material_create(&d);
    nt_test_frame_begin_pass();
    NT_TEST_EXPECT_ASSERT(nt_material_destroy(with_params));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "frame rule"));
    nt_material_destroy(without_params);
    nt_test_frame_close();
    nt_gfx_begin_frame();
}

/* A shared restore sequence may name the module before init or after shutdown. */
void test_restore_gpu_ignores_an_uninitialized_module(void) {
    nt_material_shutdown();
    nt_material_restore_gpu();
    init_material_defaults();
}

/* ---- Test 21: get_info returns NULL for invalid handle ---- */

void test_get_info_returns_null_for_invalid(void) {
    const nt_material_info_t *info = nt_material_get_info(NT_MATERIAL_INVALID);
    TEST_ASSERT_NULL(info);
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
    RUN_TEST(test_create_makes_a_uniform_buffer_with_the_params);
    RUN_TEST(test_material_without_params_makes_no_buffer);
    RUN_TEST(test_create_asserts_bad_params);
    RUN_TEST(test_set_params_writes_the_buffer_only_on_change);
    RUN_TEST(test_set_params_asserts_size_or_handle);
    RUN_TEST(test_blend_opaque_disables_blending);
    RUN_TEST(test_blend_alpha_uses_straight_source);
    RUN_TEST(test_blend_alpha_premultiplied_uses_source_as_is);
    RUN_TEST(test_blend_additive_presets_preserve_destination_alpha);
    RUN_TEST(test_blend_subtractive_presets_preserve_destination_alpha);
    RUN_TEST(test_blend_multiply_multiplies_rgb_and_preserves_destination_alpha);
    RUN_TEST(test_create_stores_render_state);
    RUN_TEST(test_create_asserts_out_of_range_key_lanes);
    RUN_TEST(test_blend_reserved_byte_is_canonicalized);
    RUN_TEST(test_create_stores_attr_map);
    RUN_TEST(test_create_asserts_null_texture_name);
    RUN_TEST(test_create_asserts_duplicate_texture_name);

    /* Valid / destroy */
    RUN_TEST(test_valid_true_after_create);
    RUN_TEST(test_valid_false_after_destroy);
    RUN_TEST(test_valid_false_for_invalid);
    RUN_TEST(test_destroy_stale_noop);

    /* Pool exhaustion */
    RUN_TEST(test_pool_full_returns_invalid);

    /* Program storage and assignment */
    RUN_TEST(test_create_stores_program);
    RUN_TEST(test_set_program_assigns_from_invalid);
    RUN_TEST(test_set_program_replaces_a_with_b);
    RUN_TEST(test_set_program_clears_to_invalid);
    RUN_TEST(test_set_program_on_a_destroyed_material_asserts);

    /* Apply, lifetime and restore */
    RUN_TEST(test_apply_binds_the_params_buffer_and_the_complete_texture_set);
    RUN_TEST(test_apply_without_params_unbinds_the_material_slot);
    RUN_TEST(test_apply_asserts_too_many_draw_textures);
    RUN_TEST(test_restore_gpu_rebuilds_params_buffers);
    RUN_TEST(test_destroy_and_shutdown_release_params_buffers);
    RUN_TEST(test_destroying_a_params_material_in_a_drawn_frame_asserts);
    RUN_TEST(test_restore_gpu_ignores_an_uninitialized_module);

    /* Query edge cases */
    RUN_TEST(test_get_info_returns_null_for_invalid);

    return UNITY_END();
}
