#include "test_helpers/nt_gfx_fake.h"
/* Render-target API mechanics via the test backend. */

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static nt_render_target_desc_t rt_desc(nt_render_target_depth_t depth) {
    return (nt_render_target_desc_t){
        .width = 64,
        .height = 32,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_LINEAR,
        .color_mag_filter = NT_FILTER_LINEAR,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = depth,
        .depth_format = depth == NT_RT_DEPTH_NONE ? NT_TEXTURE_FORMAT_INVALID : NT_TEXTURE_FORMAT_DEPTH24,
        .depth_texture_min_filter = NT_FILTER_NEAREST,
        .depth_texture_mag_filter = NT_FILTER_NEAREST,
        .depth_texture_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .depth_texture_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = "test_rt",
    };
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

static void test_create_returns_target_and_color_attachment(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    TEST_ASSERT_NOT_EQUAL_UINT32(0, rt.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_render_target_color(rt).id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_create_count());

    nt_gfx_destroy_render_target(rt);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_destroy_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_color(rt).id);
}

static void test_destroy_render_target_invalidates_applied_color_attachment(void) {
    const nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_color"}, 1);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    const nt_gfx_texture_binding_t binding = {
        .name = nt_hash32_str("u_color"),
        .texture = nt_gfx_render_target_color(rt),
        .sampler = NT_SAMPLER_DEFAULT,
    };
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_apply_texture_bindings(&binding, 1);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    /* The set must still be live here, or destroy proves nothing. */
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());

    nt_gfx_destroy_render_target(rt);

    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_NONE, nt_gfx_test_texture_set_state());
}

static void test_active_color_attachment_cannot_be_sampled(void) {
    const nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    const nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    const nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_color"}, 1);
    const nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    const nt_gfx_texture_binding_t binding = {
        .name = nt_hash32_str("u_color"),
        .texture = nt_gfx_render_target_color(rt),
        .sampler = NT_SAMPLER_DEFAULT,
    };
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);

    NT_TEST_EXPECT_ASSERT(nt_gfx_apply_texture_bindings(&binding, 1));

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_active_depth_attachment_cannot_be_sampled(void) {
    const nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    const nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    const nt_program_t program = nt_gfx_fake_make_program((const char *const[]){"u_depth"}, 1);
    const nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    const nt_gfx_texture_binding_t binding = {
        .name = nt_hash32_str("u_depth"),
        .texture = nt_gfx_render_target_depth(rt),
        .sampler = NT_SAMPLER_DEFAULT,
    };
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);

    NT_TEST_EXPECT_ASSERT(nt_gfx_apply_texture_bindings(&binding, 1));

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_depth_accessor_matches_depth_mode(void) {
    nt_render_target_desc_t none_desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_desc_t buffer_desc = rt_desc(NT_RT_DEPTH_BUFFER);
    nt_render_target_desc_t texture_desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t none = nt_gfx_make_render_target(&none_desc);
    nt_render_target_t buffer = nt_gfx_make_render_target(&buffer_desc);
    nt_render_target_t texture = nt_gfx_make_render_target(&texture_desc);

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_depth(none).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_depth(buffer).id);
    nt_texture_t depth = nt_gfx_render_target_depth(texture);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, depth.id);
    TEST_ASSERT_EQUAL_UINT32(depth.id, nt_gfx_render_target_depth(texture).id);
    TEST_ASSERT_EQUAL_INT(NT_RT_DEPTH_TEXTURE, nt_gfx_fake_last_render_target_depth());
}

static void test_pass_target_routes_to_backend(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_BUFFER);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = rt,
        .clear_color = {0.1F, 0.2F, 0.3F, 1.0F},
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_pass_target());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_zero_pass_target_routes_to_default_framebuffer(void) {
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = NT_RENDER_TARGET_INVALID,
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_pass_target());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_zero_pass_target_restores_default_after_render_target(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_BUFFER);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = rt,
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_pass_target());
    nt_gfx_end_pass();

    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = NT_RENDER_TARGET_INVALID,
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_pass_target());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_resize_preserves_target_and_attachment_handles(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    uint32_t rt_id = rt.id;
    nt_texture_t color = nt_gfx_render_target_color(rt);
    nt_texture_t depth = nt_gfx_render_target_depth(rt);

    TEST_ASSERT_NOT_EQUAL_UINT32(0, rt.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, color.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, depth.id);
    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(rt, 128, 96));

    TEST_ASSERT_EQUAL_UINT32(rt_id, rt.id);
    TEST_ASSERT_EQUAL_UINT32(color.id, nt_gfx_render_target_color(rt).id);
    TEST_ASSERT_EQUAL_UINT32(depth.id, nt_gfx_render_target_depth(rt).id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_resize_count());
    TEST_ASSERT_EQUAL_UINT16(128, nt_gfx_fake_last_render_target_width());
    TEST_ASSERT_EQUAL_UINT16(96, nt_gfx_fake_last_render_target_height());
    TEST_ASSERT_EQUAL_INT(NT_RT_DEPTH_TEXTURE, nt_gfx_fake_last_render_target_depth());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_depth_texture_backend());
}

static void test_resize_failure_keeps_existing_target_ready(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_texture_t color = nt_gfx_render_target_color(rt);
    nt_texture_t depth = nt_gfx_render_target_depth(rt);

    nt_gfx_fake_fail_next_render_target_resize();
    TEST_ASSERT_FALSE(nt_gfx_resize_render_target(rt, 128, 96));

    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(rt));
    TEST_ASSERT_EQUAL_UINT32(color.id, nt_gfx_render_target_color(rt).id);
    TEST_ASSERT_EQUAL_UINT32(depth.id, nt_gfx_render_target_depth(rt).id);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = rt,
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_pass_target());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_resize_does_not_need_generic_texture_replacement(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_fake_fail_texture_creates(1U);
    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(rt, 128, 96));
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(rt));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_resize_count());
}

static void test_depth_attachment_uses_explicit_texture_descriptor(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    desc.depth_format = NT_TEXTURE_FORMAT_DEPTH32F;
    desc.depth_texture_wrap_u = NT_WRAP_REPEAT;
    desc.depth_texture_wrap_v = NT_WRAP_MIRRORED_REPEAT;

    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_texture_desc_t depth_desc = nt_gfx_fake_last_texture_desc();

    TEST_ASSERT_NOT_EQUAL_UINT32(0, rt.id);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_texture_create_count());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_depth_texture_backend());
    TEST_ASSERT_EQUAL_INT(NT_TEXTURE_FORMAT_DEPTH32F, depth_desc.format);
    TEST_ASSERT_EQUAL_INT(NT_FILTER_NEAREST, depth_desc.min_filter);
    TEST_ASSERT_EQUAL_INT(NT_FILTER_NEAREST, depth_desc.mag_filter);
    TEST_ASSERT_EQUAL_INT(NT_WRAP_REPEAT, depth_desc.wrap_u);
    TEST_ASSERT_EQUAL_INT(NT_WRAP_MIRRORED_REPEAT, depth_desc.wrap_v);
}

static void test_make_rejects_active_pass(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_render_target_create_count());
}

static void test_resize_rejects_active_pass(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_resize_render_target(rt, 128, 96));
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(rt));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_render_target_resize_count());
}

static void test_destroy_rejects_active_pass(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_gfx_destroy_render_target(rt));
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(rt));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_render_target_destroy_count());
}

static void test_resize_rejects_zero_dimensions_without_recreating_storage(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_BUFFER);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_texture_t color = nt_gfx_render_target_color(rt);

    NT_TEST_EXPECT_ASSERT(nt_gfx_resize_render_target(rt, 0, 96));
    NT_TEST_EXPECT_ASSERT(nt_gfx_resize_render_target(rt, 128, 0));

    TEST_ASSERT_EQUAL_UINT32(color.id, nt_gfx_render_target_color(rt).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_render_target_resize_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_render_target_create_count());
}

static void test_make_rejects_mipmap_filters_for_attachments(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    desc.color_min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR;

    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_render_target_create_count());
}

static void test_update_rejects_render_target_owned_texture(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_texture_t color = nt_gfx_render_target_color(rt);
    uint8_t pixel[4] = {255, 0, 255, 255};

    NT_TEST_EXPECT_ASSERT(nt_gfx_update_texture(color, 0, 0, 1, 1, pixel));

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_update_texture_count());
}

static void test_make_render_target_rejects_null_and_zero_dimensions(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);

    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(NULL));
    desc.width = 0;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
}

// NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange) -- invalid enum values are the subject under test.
static void test_make_render_target_rejects_invalid_formats(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);

    desc.color_format = NT_TEXTURE_FORMAT_DEPTH24;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_NONE);
    desc.depth_storage = (nt_render_target_depth_t)(NT_RT_DEPTH_TEXTURE + 1);
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_NONE);
    desc.depth_storage = (nt_render_target_depth_t)-1;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    desc.depth_format = NT_TEXTURE_FORMAT_RGBA8;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    desc.depth_format = NT_TEXTURE_FORMAT_INVALID;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_NONE);
    desc.depth_format = NT_TEXTURE_FORMAT_DEPTH24;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
}

static void test_make_render_target_accepts_half_float_color(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    desc.color_format = NT_TEXTURE_FORMAT_RGBA16F;

    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, rt.id);
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(rt));

    /* The attachment carries the requested format, not a silently substituted one. */
    nt_texture_t color = nt_gfx_render_target_color(rt);
    TEST_ASSERT_EQUAL_INT(NT_TEXTURE_FORMAT_RGBA16F, nt_gfx_texture_format(color));

    nt_gfx_destroy_render_target(rt);
}

static void test_make_render_target_still_rejects_full_float_color(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    desc.color_format = NT_TEXTURE_FORMAT_RGBA32F;

    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
}

static void test_make_render_target_rejects_invalid_sampler_modes(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);

    desc.color_min_filter = (nt_texture_filter_t)-1;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_NONE);
    desc.color_wrap_u = (nt_texture_wrap_t)(NT_WRAP_MIRRORED_REPEAT + 1);
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_NONE);
    desc.color_wrap_v = (nt_texture_wrap_t)-1;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    desc.depth_texture_min_filter = NT_FILTER_LINEAR;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    desc.depth_texture_mag_filter = NT_FILTER_LINEAR;
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
    desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    desc.depth_texture_wrap_u = (nt_texture_wrap_t)(NT_WRAP_MIRRORED_REPEAT + 1);
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));
}
// NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)

/* Sampler compatibility is checked where a texture reaches a unit: the semantic set. */
static void begin_single_sampler_pass(uint8_t sampler_class) {
    const nt_program_t program = nt_gfx_fake_make_program_typed((const char *const[]){"u_tex"}, &sampler_class, 1);
    const nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
}

static void apply_one_texture(nt_texture_t texture, nt_sampler_t sampler) {
    const nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("u_tex"), .texture = texture, .sampler = sampler};
    nt_gfx_apply_texture_bindings(&binding, 1);
}

static void test_depth_texture_rejects_linear_sampler_override(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_texture_t depth = nt_gfx_render_target_depth(rt);
    nt_texture_t color = nt_gfx_render_target_color(rt);
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
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_sampler_t comparison = make_comparison_sampler();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, comparison.id);

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_SHADOW);
    apply_one_texture(nt_gfx_render_target_depth(rt), comparison);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());

    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_destroy_render_target(rt);
}

/* Comparison against non-depth storage is undefined in GL, so the same sampler
 * that is legal on the depth attachment must be rejected on the colour one. */
static void test_color_texture_rejects_comparison_sampler(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_sampler_t comparison = make_comparison_sampler();

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_FLOAT);
    NT_TEST_EXPECT_ASSERT(apply_one_texture(nt_gfx_render_target_color(rt), comparison));

    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_destroy_render_target(rt);
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
    nt_gfx_end_frame();
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

static nt_sampler_t make_mipmap_sampler(void) {
    return nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });
}

static void test_render_target_color_rejects_mipmap_sampler_override(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_sampler_t mipmap_sampler = make_mipmap_sampler();

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_FLOAT);
    NT_TEST_EXPECT_ASSERT(apply_one_texture(nt_gfx_render_target_color(rt), mipmap_sampler));
}

static void test_one_pixel_texture_accepts_mipmap_sampler_override(void) {
    const uint8_t pixel[4] = {255, 255, 255, 255};
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 1,
        .height = 1,
        .data = pixel,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });
    nt_sampler_t mipmap_sampler = make_mipmap_sampler();

    begin_single_sampler_pass(NT_GFX_SAMPLER_CLASS_FLOAT);
    apply_one_texture(texture, mipmap_sampler);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
}

static void test_invalid_render_target_lifecycle_arguments_assert(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    NT_TEST_EXPECT_ASSERT(nt_gfx_resize_render_target(NT_RENDER_TARGET_INVALID, 64, 32));
    NT_TEST_EXPECT_ASSERT(nt_gfx_destroy_render_target(NT_RENDER_TARGET_INVALID));
    NT_TEST_EXPECT_ASSERT(nt_gfx_destroy_texture(nt_gfx_render_target_color(rt)));
}

static void test_begin_pass_asserts_for_invalid_or_incomplete_target(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_begin_frame();
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(&(nt_pass_desc_t){.target = (nt_render_target_t){UINT32_MAX}, .clear_depth = 1.0F}));
    nt_gfx_end_frame();

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_fail_next_render_target_create();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(nt_gfx_render_target_ready(rt));
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_depth = 1.0F}));
    nt_gfx_end_frame();
}

static void test_pass_sequencing_and_capacity_misuse_assert(void) {
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(NULL));
    NT_TEST_EXPECT_ASSERT(nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F}));

    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_make_render_target(&desc).id);
    }
    NT_TEST_EXPECT_ASSERT(nt_gfx_make_render_target(&desc));

    nt_gfx_desc_t invalid_desc = nt_gfx_desc_defaults();
    invalid_desc.max_render_targets = 0;
    NT_TEST_EXPECT_ASSERT(nt_gfx_init(&invalid_desc));
}

static void test_resize_preserves_depth_mode_accessor_matrix(void) {
    nt_render_target_desc_t none_desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_desc_t buffer_desc = rt_desc(NT_RT_DEPTH_BUFFER);
    nt_render_target_desc_t texture_desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t none = nt_gfx_make_render_target(&none_desc);
    nt_render_target_t buffer = nt_gfx_make_render_target(&buffer_desc);
    nt_render_target_t texture = nt_gfx_make_render_target(&texture_desc);
    nt_texture_t texture_depth = nt_gfx_render_target_depth(texture);

    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(none, 48, 24));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_depth(none).id);
    TEST_ASSERT_EQUAL_INT(NT_RT_DEPTH_NONE, nt_gfx_fake_last_render_target_depth());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_depth_texture_backend());

    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(buffer, 80, 40));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_depth(buffer).id);
    TEST_ASSERT_EQUAL_INT(NT_RT_DEPTH_BUFFER, nt_gfx_fake_last_render_target_depth());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_depth_texture_backend());

    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(texture, 160, 64));
    TEST_ASSERT_EQUAL_UINT32(texture_depth.id, nt_gfx_render_target_depth(texture).id);
    TEST_ASSERT_EQUAL_INT(NT_RT_DEPTH_TEXTURE, nt_gfx_fake_last_render_target_depth());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_depth_texture_backend());
}

static void test_context_restore_recreates_backend_from_retained_descriptor(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    desc.depth_format = NT_TEXTURE_FORMAT_DEPTH16;
    desc.depth_texture_wrap_u = NT_WRAP_REPEAT;
    desc.depth_texture_wrap_v = NT_WRAP_MIRRORED_REPEAT;
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    uint32_t rt_id = rt.id;
    nt_texture_t color = nt_gfx_render_target_color(rt);
    nt_texture_t depth = nt_gfx_render_target_depth(rt);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);

    TEST_ASSERT_EQUAL_UINT32(rt_id, rt.id);
    TEST_ASSERT_EQUAL_UINT32(color.id, nt_gfx_render_target_color(rt).id);
    TEST_ASSERT_EQUAL_UINT32(depth.id, nt_gfx_render_target_depth(rt).id);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_render_target_create_count());
    TEST_ASSERT_EQUAL_UINT16(desc.width, nt_gfx_fake_last_render_target_width());
    TEST_ASSERT_EQUAL_UINT16(desc.height, nt_gfx_fake_last_render_target_height());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_depth_texture_backend());
    nt_texture_desc_t restored_depth = nt_gfx_fake_last_texture_desc();
    TEST_ASSERT_EQUAL_INT(NT_TEXTURE_FORMAT_DEPTH16, restored_depth.format);
    TEST_ASSERT_EQUAL_INT(NT_WRAP_REPEAT, restored_depth.wrap_u);
    TEST_ASSERT_EQUAL_INT(NT_WRAP_MIRRORED_REPEAT, restored_depth.wrap_v);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_gpu_caps_probe_count());

    nt_gfx_end_frame();
}

static void test_context_restore_retries_after_backend_recreate_failure(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);

    nt_gfx_fake_fail_next_backend_restore();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_backend_restore_count());

    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(rt));
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_backend_restore_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_gpu_caps_probe_count());
    nt_gfx_end_frame();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_context_restore_waits_while_backend_remains_lost(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_set_scissor_enabled(true);
    TEST_ASSERT_TRUE(nt_gfx_scissor_enabled());
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_begin_frame();

    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_backend_restore_count());
    TEST_ASSERT_FALSE(nt_gfx_render_target_ready(rt));

    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_FALSE(nt_gfx_scissor_enabled());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_backend_restore_count());
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(rt));
    nt_gfx_end_frame();
}

static void test_context_restore_marks_failed_target_not_ready(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);

    nt_gfx_fake_fail_next_render_target_create();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();

    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    TEST_ASSERT_FALSE(nt_gfx_render_target_ready(rt));

    nt_gfx_end_frame();
}

static void test_resize_does_not_recover_missing_stub_backend(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_NONE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_fail_next_render_target_create();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();

    TEST_ASSERT_FALSE(nt_gfx_render_target_ready(rt));
    TEST_ASSERT_FALSE(nt_gfx_resize_render_target(rt, 128, 96));
    TEST_ASSERT_FALSE(nt_gfx_render_target_ready(rt));

    nt_gfx_end_frame();
}

static void test_invalid_handles_return_invalid_attachments(void) {
    nt_render_target_t invalid = NT_RENDER_TARGET_INVALID;

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_color(invalid).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_depth(invalid).id);
}

static void test_texture_size_tracks_attachment_resize(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);
    nt_texture_t color = nt_gfx_render_target_color(rt);
    nt_texture_t depth = nt_gfx_render_target_depth(rt);
    uint16_t width = 0;
    uint16_t height = 0;

    TEST_ASSERT_TRUE(nt_gfx_texture_size(color, &width, &height));
    TEST_ASSERT_EQUAL_UINT16(64, width);
    TEST_ASSERT_EQUAL_UINT16(32, height);
    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(rt, 128, 96));
    TEST_ASSERT_TRUE(nt_gfx_texture_size(depth, &width, &height));
    TEST_ASSERT_EQUAL_UINT16(128, width);
    TEST_ASSERT_EQUAL_UINT16(96, height);

    width = 7;
    height = 9;
    TEST_ASSERT_FALSE(nt_gfx_texture_size((nt_texture_t){UINT32_MAX}, &width, &height));
    TEST_ASSERT_EQUAL_UINT16(0, width);
    TEST_ASSERT_EQUAL_UINT16(0, height);
}

static void test_texture_format_reports_logical_format(void) {
    nt_render_target_desc_t desc = rt_desc(NT_RT_DEPTH_TEXTURE);
    nt_render_target_t rt = nt_gfx_make_render_target(&desc);

    TEST_ASSERT_EQUAL_INT(NT_TEXTURE_FORMAT_RGBA8, nt_gfx_texture_format(nt_gfx_render_target_color(rt)));
    TEST_ASSERT_EQUAL_INT(NT_TEXTURE_FORMAT_DEPTH24, nt_gfx_texture_format(nt_gfx_render_target_depth(rt)));
    TEST_ASSERT_EQUAL_INT(NT_TEXTURE_FORMAT_INVALID, nt_gfx_texture_format((nt_texture_t){UINT32_MAX}));
}
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
    RUN_TEST(test_create_returns_target_and_color_attachment);
    RUN_TEST(test_destroy_render_target_invalidates_applied_color_attachment);
    RUN_TEST(test_active_color_attachment_cannot_be_sampled);
    RUN_TEST(test_active_depth_attachment_cannot_be_sampled);
    RUN_TEST(test_depth_accessor_matches_depth_mode);
    RUN_TEST(test_pass_target_routes_to_backend);
    RUN_TEST(test_zero_pass_target_routes_to_default_framebuffer);
    RUN_TEST(test_zero_pass_target_restores_default_after_render_target);
    RUN_TEST(test_resize_preserves_target_and_attachment_handles);
    RUN_TEST(test_resize_failure_keeps_existing_target_ready);
    RUN_TEST(test_resize_does_not_need_generic_texture_replacement);
    RUN_TEST(test_depth_attachment_uses_explicit_texture_descriptor);
    RUN_TEST(test_make_rejects_active_pass);
    RUN_TEST(test_resize_rejects_active_pass);
    RUN_TEST(test_destroy_rejects_active_pass);
    RUN_TEST(test_resize_rejects_zero_dimensions_without_recreating_storage);
    RUN_TEST(test_make_rejects_mipmap_filters_for_attachments);
    RUN_TEST(test_update_rejects_render_target_owned_texture);
    RUN_TEST(test_make_render_target_rejects_null_and_zero_dimensions);
    RUN_TEST(test_make_render_target_rejects_invalid_formats);
    RUN_TEST(test_make_render_target_accepts_half_float_color);
    RUN_TEST(test_make_render_target_still_rejects_full_float_color);
    RUN_TEST(test_make_render_target_rejects_invalid_sampler_modes);
    RUN_TEST(test_depth_texture_rejects_linear_sampler_override);
    RUN_TEST(test_integer_texture_rejects_linear_sampler_override);
    RUN_TEST(test_depth_texture_accepts_linear_comparison_sampler);
    RUN_TEST(test_color_texture_rejects_comparison_sampler);
    RUN_TEST(test_integer_texture_rejects_comparison_sampler);
    RUN_TEST(test_comparison_state_participates_in_sampler_dedupe);
    RUN_TEST(test_out_of_range_sampler_state_keys_what_the_backend_builds);
    RUN_TEST(test_render_target_color_rejects_mipmap_sampler_override);
    RUN_TEST(test_one_pixel_texture_accepts_mipmap_sampler_override);
    RUN_TEST(test_invalid_render_target_lifecycle_arguments_assert);
    RUN_TEST(test_begin_pass_asserts_for_invalid_or_incomplete_target);
    RUN_TEST(test_pass_sequencing_and_capacity_misuse_assert);
    RUN_TEST(test_resize_preserves_depth_mode_accessor_matrix);
    RUN_TEST(test_context_restore_recreates_backend_from_retained_descriptor);
    RUN_TEST(test_context_restore_retries_after_backend_recreate_failure);
    RUN_TEST(test_context_restore_waits_while_backend_remains_lost);
    RUN_TEST(test_context_restore_marks_failed_target_not_ready);
    RUN_TEST(test_resize_does_not_recover_missing_stub_backend);
    RUN_TEST(test_invalid_handles_return_invalid_attachments);
    RUN_TEST(test_texture_size_tracks_attachment_resize);
    RUN_TEST(test_texture_format_reports_logical_format);
    RUN_TEST(test_header_does_not_expose_target_bind_state_api);
    return UNITY_END();
}
