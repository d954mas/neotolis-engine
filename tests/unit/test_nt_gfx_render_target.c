#include "test_helpers/nt_gfx_fake.h"
/* Render-target API mechanics via the test backend. */

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NO_TEXTURE ((nt_texture_t){0})

/* Zero filter/wrap fields are NEAREST/CLAMP_TO_EDGE, which depth storage requires. */
static nt_texture_t make_attachment(nt_texture_format_t format, uint16_t width, uint16_t height) {
    return nt_gfx_make_texture(&(nt_texture_desc_t){.width = width, .height = height, .format = format});
}

static nt_texture_t make_color(void) { return make_attachment(NT_TEXTURE_FORMAT_RGBA8, 64, 32); }
static nt_texture_t make_depth(void) { return make_attachment(NT_TEXTURE_FORMAT_DEPTH24, 64, 32); }

static nt_render_target_t make_target(nt_texture_t color, nt_texture_t depth) { return nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = color, .depth = depth, .label = "test_rt"}); }

static void lose_context(void) {
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
}

static void restore_context(void) {
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
}

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){
        .max_shaders = 4,
        .max_programs = 4,
        .max_pipelines = 4,
        .max_buffers = 8,
        .max_textures = 8,
        .max_meshes = 4,
        .max_vertex_inputs = 8,
        .max_render_targets = 4,
    });
    nt_gfx_fake_reset();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) { nt_gfx_shutdown(); }

// #region attachments
static void test_color_only_target_borrows_its_texture(void) {
    nt_texture_t color = make_color();
    nt_render_target_t rt = make_target(color, NO_TEXTURE);

    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(rt));
    TEST_ASSERT_EQUAL_UINT32(color.id, nt_gfx_render_target_color(rt).id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_create_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(color), nt_gfx_fake_last_color_texture_backend());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_depth_texture_backend());

    nt_gfx_destroy_render_target(rt);
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(rt));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_destroy_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_color(rt).id);
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(color));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_texture_destroy_count());
}

static void test_depth_only_target_passes_its_size_to_the_pass(void) {
    nt_texture_t depth = make_attachment(NT_TEXTURE_FORMAT_DEPTH32F, 48, 16);
    nt_render_target_t rt = make_target(NO_TEXTURE, depth);

    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(rt));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_color(rt).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_color_texture_backend());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(depth), nt_gfx_fake_last_depth_texture_backend());

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_render_target_backend_id(rt), nt_gfx_fake_last_pass_target());
    TEST_ASSERT_EQUAL_UINT16(48, nt_gfx_fake_last_pass_width());
    TEST_ASSERT_EQUAL_UINT16(16, nt_gfx_fake_last_pass_height());
    nt_gfx_end_pass();
}

static void test_color_depth_target_passes_both_backends(void) {
    nt_texture_t color = make_color();
    nt_texture_t depth = make_depth();
    nt_render_target_t rt = make_target(color, depth);

    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(rt));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(color), nt_gfx_fake_last_color_texture_backend());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(depth), nt_gfx_fake_last_depth_texture_backend());

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    TEST_ASSERT_EQUAL_UINT16(64, nt_gfx_fake_last_pass_width());
    TEST_ASSERT_EQUAL_UINT16(32, nt_gfx_fake_last_pass_height());
    nt_gfx_end_pass();
}

static void test_half_float_color_is_accepted(void) {
    nt_texture_t color = make_attachment(NT_TEXTURE_FORMAT_RGBA16F, 64, 32);
    nt_render_target_t rt = make_target(color, NO_TEXTURE);

    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(rt));
    TEST_ASSERT_EQUAL_INT(NT_TEXTURE_FORMAT_RGBA16F, nt_gfx_texture_format(nt_gfx_render_target_color(rt)));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_make_asserts_on_attachment_misuse(void) {
    nt_texture_t color = make_color();
    nt_texture_t depth = make_depth();
    nt_texture_t small_depth = make_attachment(NT_TEXTURE_FORMAT_DEPTH24, 32, 32);
    nt_texture_t full_float = make_attachment(NT_TEXTURE_FORMAT_RGBA32F, 64, 32);
    uint8_t pixels[4 * 4 * 4] = {0};
    nt_texture_t mipmapped = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8, .data = pixels, .gen_mipmaps = true});
    nt_texture_t destroyed = make_color();
    nt_gfx_destroy_texture(destroyed);

    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(NULL));
    NT_TEST_EXPECT_ASSERT(make_target(NO_TEXTURE, NO_TEXTURE));
    NT_TEST_EXPECT_ASSERT(make_target(color, small_depth));
    NT_TEST_EXPECT_ASSERT(make_target(depth, NO_TEXTURE));
    NT_TEST_EXPECT_ASSERT(make_target(NO_TEXTURE, color));
    NT_TEST_EXPECT_ASSERT(make_target(full_float, NO_TEXTURE));
    NT_TEST_EXPECT_ASSERT(make_target(mipmapped, NO_TEXTURE));
    NT_TEST_EXPECT_ASSERT(make_target(destroyed, NO_TEXTURE));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_render_target_create_count());
}

/* A pool-valid texture that outlived a loss has no GL name to attach. */
static void test_make_asserts_on_texture_husk(void) {
    nt_texture_t color = make_color();
    lose_context();
    restore_context();
    TEST_ASSERT_FALSE(nt_gfx_texture_ready(color));

    NT_TEST_EXPECT_ASSERT(make_target(color, NO_TEXTURE));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_render_target_create_count());
}

/* An incomplete framebuffer (RGBA16F without float rendering) is a runtime
 * failure: creation returns INVALID and the borrowed textures stay the caller's. */
static void test_backend_failure_returns_invalid_and_keeps_textures(void) {
    nt_texture_t color = make_color();
    nt_texture_t depth = make_depth();
    nt_gfx_fake_fail_next_render_target_create();

    TEST_ASSERT_EQUAL_UINT32(0, make_target(color, depth).id);
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(color));
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(depth));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_texture_destroy_count());

    /* setUp capacity is 4 targets: the failed make returned its slot. */
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(nt_gfx_render_target_valid(make_target(color, depth)));
    }
}
// #endregion

// #region lifetime
static void test_shared_depth_texture_serves_two_targets(void) {
    nt_texture_t color_a = make_color();
    nt_texture_t color_b = make_color();
    nt_texture_t depth = make_depth();
    nt_render_target_t a = make_target(color_a, depth);
    nt_render_target_t b = make_target(color_b, depth);

    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(a));
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(b));

    nt_gfx_destroy_texture(depth);
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(a));
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(b));
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_render_target_destroy_count());
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(color_a));
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(color_b));
}

static void test_destroying_a_texture_destroys_only_its_targets(void) {
    nt_texture_t color_a = make_color();
    nt_texture_t color_b = make_color();
    nt_render_target_t a = make_target(color_a, NO_TEXTURE);
    nt_render_target_t b = make_target(color_b, NO_TEXTURE);

    nt_gfx_destroy_texture(color_a);
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(a));
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(b));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_destroy_count());

    /* The freed slot is reused; the stale handle must not match the new target. */
    nt_render_target_t c = make_target(color_b, NO_TEXTURE);
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(a));
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(c));
}

/* Cascades and loss make stale handles routine, so destroy tolerates them. */
static void test_destroy_render_target_tolerates_invalid_and_stale_handles(void) {
    nt_render_target_t rt = make_target(make_color(), NO_TEXTURE);

    nt_gfx_destroy_render_target(NT_RENDER_TARGET_INVALID);
    nt_gfx_destroy_render_target(rt);
    nt_gfx_destroy_render_target(rt);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_destroy_count());
}

static void test_make_and_destroy_reject_active_pass(void) {
    nt_texture_t color = make_color();
    nt_render_target_t rt = make_target(color, NO_TEXTURE);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(make_target(color, NO_TEXTURE));
    NT_TEST_EXPECT_ASSERT(nt_gfx_destroy_render_target(rt));
    /* The pass check runs before the cascade, so the target survives. */
    NT_TEST_EXPECT_ASSERT(nt_gfx_destroy_texture(color));
    nt_gfx_end_pass();

    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(rt));
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(color));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_create_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_render_target_destroy_count());
}

/* A size change destroys the textures (the cascade frees the target) and makes new ones. */
static void test_recreate_at_new_size_without_spare_slots(void) {
    nt_texture_t color = make_color();
    nt_render_target_t targets[4];
    for (int i = 0; i < 4; i++) {
        targets[i] = make_target(color, NO_TEXTURE);
    }
    /* Fill the texture pool too: the new size needs no spare slot. */
    for (int i = 0; i < 7; i++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(0, make_color().id);
    }

    nt_gfx_destroy_texture(color);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_FALSE(nt_gfx_render_target_valid(targets[i]));
    }
    nt_texture_t resized = make_attachment(NT_TEXTURE_FORMAT_RGBA8, 128, 64);
    nt_render_target_t rt = make_target(resized, NO_TEXTURE);
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(rt));

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    TEST_ASSERT_EQUAL_UINT16(128, nt_gfx_fake_last_pass_width());
    TEST_ASSERT_EQUAL_UINT16(64, nt_gfx_fake_last_pass_height());
    nt_gfx_end_pass();
}
// #endregion

// #region context loss
/* Targets are baked objects like vertex inputs: loss frees them and the
 * attachment textures become husks the owner destroys and recreates. */
static void test_context_loss_frees_targets_and_leaves_texture_husks(void) {
    nt_texture_t color = make_color();
    nt_texture_t depth = make_depth();
    nt_render_target_t rt = make_target(color, depth);

    lose_context();
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(rt));
    restore_context();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(rt));
    TEST_ASSERT_FALSE(nt_gfx_texture_ready(color));
    TEST_ASSERT_FALSE(nt_gfx_texture_ready(depth));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_create_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_gpu_caps_probe_count());

    nt_gfx_destroy_render_target(rt);
    nt_gfx_destroy_texture(color);
    nt_gfx_destroy_texture(depth);
    nt_render_target_t remade = make_target(make_color(), make_depth());
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(remade));
}

/* A failed web recreate leaves no context; retrying every frame would only fail again. */
static void test_context_restore_waits_after_a_restore_that_leaves_the_backend_lost(void) {
    lose_context();
    nt_gfx_fake_fail_next_backend_restore_lost();
    nt_gfx_fake_set_context_lost(false);
    for (int i = 0; i < 4; i++) {
        nt_gfx_begin_frame();
    }
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_backend_restore_count());
}

static void test_context_restore_stays_lost_when_the_recreate_meets_a_loss(void) {
    lose_context();
    nt_gfx_fake_lose_context_during_next_restore();
    restore_context();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);

    restore_context();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_backend_restore_count());
}

static void test_context_restore_waits_while_backend_remains_lost(void) {
    nt_gfx_set_scissor_enabled(true);
    TEST_ASSERT_TRUE(nt_gfx_scissor_enabled());
    lose_context();
    nt_gfx_begin_frame();

    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_backend_restore_count());

    restore_context();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_FALSE(nt_gfx_scissor_enabled());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_backend_restore_count());
}
// #endregion

// #region passes
static void test_zero_pass_target_routes_to_default_framebuffer_after_render_target(void) {
    nt_render_target_t rt = make_target(make_color(), NO_TEXTURE);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_color = {0.1F, 0.2F, 0.3F, 1.0F}, .clear_depth = 1.0F});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_pass_target());
    nt_gfx_end_pass();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = NT_RENDER_TARGET_INVALID, .clear_depth = 1.0F});
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_pass_target());
    nt_gfx_end_pass();
}

static void test_begin_pass_asserts_for_invalid_or_stale_target(void) {
    nt_render_target_t rt = make_target(make_color(), NO_TEXTURE);

    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(&(nt_pass_desc_t){.target = (nt_render_target_t){UINT32_MAX}, .clear_depth = 1.0F}));
    nt_gfx_destroy_render_target(rt);
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F}));
}

static void test_pass_sequencing_and_capacity_misuse_assert(void) {
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(NULL));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F}));
    nt_gfx_end_pass();

    nt_texture_t color = make_color();
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(0, make_target(color, NO_TEXTURE).id);
    }
    NT_TEST_EXPECT_ASSERT(make_target(color, NO_TEXTURE));

    nt_gfx_desc_t invalid_desc = nt_gfx_desc_defaults();
    invalid_desc.max_render_targets = 0;
    NT_TEST_EXPECT_ASSERT(nt_gfx_init(&invalid_desc));
}

/* A depth-only target has no colour to read. */
static void test_read_pixels_asserts_inside_depth_only_pass(void) {
    nt_render_target_t rt = make_target(NO_TEXTURE, make_depth());
    uint8_t pixel[4];

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_read_pixels(0, 0, 1, 1, pixel, sizeof(pixel)));
    nt_gfx_end_pass();
}
// #endregion

// #region sampling
static nt_sampler_t make_nearest_sampler(void) { return nt_gfx_make_sampler(&(nt_sampler_desc_t){.min_filter = NT_FILTER_NEAREST, .mag_filter = NT_FILTER_NEAREST}); }

/* Sampler compatibility is checked where a texture reaches a unit: the semantic set. */
static void begin_single_sampler_pass(uint8_t sampler_class) {
    const nt_program_t program = nt_gfx_fake_make_program_typed((const char *const[]){"u_tex"}, &sampler_class, 1);
    const nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
}

static void apply_one_texture(nt_texture_t texture, nt_sampler_t sampler) {
    const nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("u_tex"), .texture = texture, .sampler = sampler};
    nt_gfx_apply_texture_bindings(&binding, 1);
}

static void test_active_attachments_cannot_be_sampled(void) {
    nt_texture_t color = make_color();
    nt_texture_t depth = make_depth();
    const nt_render_target_t rt = make_target(color, depth);
    const nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_tex"}, 1);
    const nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("u_tex"), .texture = color, .sampler = make_nearest_sampler()};
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);

    NT_TEST_EXPECT_ASSERT(nt_gfx_apply_texture_bindings(&binding, 1));
    binding.texture = depth;
    NT_TEST_EXPECT_ASSERT(nt_gfx_apply_texture_bindings(&binding, 1));

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
    nt_gfx_end_pass();
}

/* Attachments are ordinary textures: NT_SAMPLER_DEFAULT is the sampler of their own desc. */
static void test_attachments_bind_with_their_default_sampler(void) {
    nt_texture_t color = make_attachment(NT_TEXTURE_FORMAT_RGBA8, 64, 32);
    nt_texture_t depth = make_depth();
    nt_render_target_t rt = make_target(color, depth);
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(rt));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_get_texture_default_sampler(color).id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_get_texture_default_sampler(depth).id);

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_FLOAT);
    apply_one_texture(color, NT_SAMPLER_DEFAULT);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    apply_one_texture(depth, NT_SAMPLER_DEFAULT);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    nt_gfx_end_pass();
}

static void test_depth_texture_rejects_linear_sampler_override(void) {
    nt_texture_t depth = make_depth();
    nt_texture_t color = make_color();
    nt_sampler_t linear = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_FLOAT);
    NT_TEST_EXPECT_ASSERT(apply_one_texture(depth, linear));

    apply_one_texture(color, linear);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
}

static void test_integer_texture_rejects_linear_sampler_override(void) {
    nt_texture_t integer = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 1,
        .height = 1,
        .format = NT_TEXTURE_FORMAT_RG16UI,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });
    nt_sampler_t linear = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_UINT);
    NT_TEST_EXPECT_ASSERT(apply_one_texture(integer, linear));
}

static nt_sampler_t make_comparison_sampler(void) {
    return nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .compare_func = NT_COMPARE_LEQUAL,
    });
}

static void test_depth_texture_accepts_linear_comparison_sampler(void) {
    nt_texture_t depth = make_depth();
    nt_sampler_t comparison = make_comparison_sampler();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, comparison.id);

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_SHADOW);
    apply_one_texture(depth, comparison);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    nt_gfx_end_pass();
}

/* Comparison against non-depth storage is undefined in GL, so the same sampler
 * that is legal on depth storage must be rejected on colour storage. */
static void test_color_texture_rejects_comparison_sampler(void) {
    nt_texture_t color = make_color();
    nt_sampler_t comparison = make_comparison_sampler();

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_FLOAT);
    NT_TEST_EXPECT_ASSERT(apply_one_texture(color, comparison));
    nt_gfx_end_pass();
}

static void test_integer_texture_rejects_comparison_sampler(void) {
    nt_texture_t integer = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 1,
        .height = 1,
        .format = NT_TEXTURE_FORMAT_RG16UI,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });
    /* NEAREST, so the integer filter rule cannot be what rejects this — only
     * the comparison guard can. */
    nt_sampler_t comparison = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .compare_func = NT_COMPARE_LEQUAL,
    });

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_UINT);
    NT_TEST_EXPECT_ASSERT(apply_one_texture(integer, comparison));

    nt_gfx_end_pass();
    nt_gfx_destroy_texture(integer);
}

/* The dedupe key must separate comparison state, or the shadow lookup and the
 * raw-depth debug view would collapse onto one GL sampler object. */
static void test_comparison_state_participates_in_sampler_dedupe(void) {
    nt_sampler_desc_t plain = {
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    };
    nt_sampler_desc_t leq = plain;
    leq.compare_func = NT_COMPARE_LEQUAL;
    nt_sampler_desc_t less = plain;
    less.compare_func = NT_COMPARE_LESS;

    nt_sampler_t a = nt_gfx_make_sampler(&plain);
    nt_sampler_t b = nt_gfx_make_sampler(&leq);
    nt_sampler_t c = nt_gfx_make_sampler(&less);

    TEST_ASSERT_NOT_EQUAL_UINT32(a.id, b.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(b.id, c.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(a.id, c.id);
    TEST_ASSERT_EQUAL_UINT32(b.id, nt_gfx_make_sampler(&leq).id);
}

/* Pack headers cast raw bytes into these enums, so an out-of-range value has to
 * key the sampler the backend actually builds — otherwise it takes over the
 * cache slot of a valid, different one. */
static void test_out_of_range_sampler_state_keys_what_the_backend_builds(void) {
    nt_sampler_desc_t clamped = {
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    };
    nt_sampler_desc_t garbage = clamped;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) — the out-of-range value is the subject of the test
    garbage.wrap_u = (nt_texture_wrap_t)9;
    nt_sampler_desc_t repeat = clamped;
    repeat.wrap_u = NT_WRAP_REPEAT;

    nt_sampler_t from_garbage = nt_gfx_make_sampler(&garbage);

    TEST_ASSERT_EQUAL_UINT32(nt_gfx_make_sampler(&clamped).id, from_garbage.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_gfx_make_sampler(&repeat).id, from_garbage.id);
}

/* Attachments are single-level, and GL_TEXTURE_MAX_LEVEL makes that complete:
 * a mip filter samples level 0. */
static void test_single_level_textures_accept_mipmap_sampler_override(void) {
    const uint8_t pixel[4] = {255, 255, 255, 255};
    nt_texture_t one_pixel = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = pixel, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_texture_t color = make_color();
    nt_sampler_t mipmap_sampler = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_FLOAT);
    apply_one_texture(color, mipmap_sampler);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    apply_one_texture(one_pixel, mipmap_sampler);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    nt_gfx_end_pass();
}
// #endregion

static void test_invalid_handle_returns_invalid_color(void) { TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_color(NT_RENDER_TARGET_INVALID).id); }

static void test_header_does_not_expose_target_bind_state_api(void) {
    FILE *f = fopen("engine/graphics/nt_gfx.h", "rb");
    TEST_ASSERT_NOT_NULL(f);

    char buf[32768];
    size_t n = fread(buf, 1, sizeof(buf) - 1U, f);
    (void)fclose(f);
    buf[n] = '\0';

    const char *rt_name = "render_target";
    const char *p = buf;
    while ((p = strstr(p, rt_name)) != NULL) {
        const char *line_start = p;
        while (line_start > buf && line_start[-1] != '\n') {
            line_start--;
        }
        const char *line_end = p;
        while (*line_end != '\0' && *line_end != '\n') {
            line_end++;
        }
        const char *bind = strstr(line_start, "bind_");
        const char *unbind = strstr(line_start, "unbind_");
        TEST_ASSERT_TRUE(bind == NULL || bind >= line_end);
        TEST_ASSERT_TRUE(unbind == NULL || unbind >= line_end);
        p += strlen(rt_name);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_color_only_target_borrows_its_texture);
    RUN_TEST(test_depth_only_target_passes_its_size_to_the_pass);
    RUN_TEST(test_color_depth_target_passes_both_backends);
    RUN_TEST(test_half_float_color_is_accepted);
    RUN_TEST(test_make_asserts_on_attachment_misuse);
    RUN_TEST(test_make_asserts_on_texture_husk);
    RUN_TEST(test_backend_failure_returns_invalid_and_keeps_textures);
    RUN_TEST(test_shared_depth_texture_serves_two_targets);
    RUN_TEST(test_destroying_a_texture_destroys_only_its_targets);
    RUN_TEST(test_destroy_render_target_tolerates_invalid_and_stale_handles);
    RUN_TEST(test_make_and_destroy_reject_active_pass);
    RUN_TEST(test_recreate_at_new_size_without_spare_slots);
    RUN_TEST(test_context_loss_frees_targets_and_leaves_texture_husks);
    RUN_TEST(test_context_restore_waits_after_a_restore_that_leaves_the_backend_lost);
    RUN_TEST(test_context_restore_stays_lost_when_the_recreate_meets_a_loss);
    RUN_TEST(test_context_restore_waits_while_backend_remains_lost);
    RUN_TEST(test_zero_pass_target_routes_to_default_framebuffer_after_render_target);
    RUN_TEST(test_begin_pass_asserts_for_invalid_or_stale_target);
    RUN_TEST(test_pass_sequencing_and_capacity_misuse_assert);
    RUN_TEST(test_read_pixels_asserts_inside_depth_only_pass);
    RUN_TEST(test_active_attachments_cannot_be_sampled);
    RUN_TEST(test_attachments_bind_with_their_default_sampler);
    RUN_TEST(test_depth_texture_rejects_linear_sampler_override);
    RUN_TEST(test_integer_texture_rejects_linear_sampler_override);
    RUN_TEST(test_depth_texture_accepts_linear_comparison_sampler);
    RUN_TEST(test_color_texture_rejects_comparison_sampler);
    RUN_TEST(test_integer_texture_rejects_comparison_sampler);
    RUN_TEST(test_comparison_state_participates_in_sampler_dedupe);
    RUN_TEST(test_out_of_range_sampler_state_keys_what_the_backend_builds);
    RUN_TEST(test_single_level_textures_accept_mipmap_sampler_override);
    RUN_TEST(test_invalid_handle_returns_invalid_color);
    RUN_TEST(test_header_does_not_expose_target_bind_state_api);
    return UNITY_END();
}
