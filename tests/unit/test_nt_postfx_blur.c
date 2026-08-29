#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "postfx/nt_postfx_blur.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static bool weights_are_finite_and_equal(const float *a, const float *b, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!isfinite(a[i]) || !float_near(a[i], b[i], 0.000001F)) {
            return false;
        }
    }
    return true;
}

static float sum_weights(const float *weights, uint32_t count) {
    float sum = 0.0F;
    for (uint32_t i = 0; i < count; i++) {
        sum += weights[i];
    }
    return sum;
}

static nt_render_target_desc_t blur_rt_desc(uint16_t width, uint16_t height, const char *label) {
    return (nt_render_target_desc_t){
        .width = width,
        .height = height,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_LINEAR,
        .color_mag_filter = NT_FILTER_LINEAR,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_NONE,
        .depth_format = NT_TEXTURE_FORMAT_INVALID,
        .label = label,
    };
}

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){
        .max_shaders = 8,
        .max_programs = 8,
        .max_pipelines = 8,
        .max_buffers = 8,
        .max_textures = 12,
        .max_meshes = 4,
        .max_render_targets = 4,
    });
    nt_gfx_stub_test_reset();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_postfx_blur_init());
    nt_postfx_blur_test_reset_counters();
}

void tearDown(void) {
    nt_postfx_blur_shutdown();
    nt_gfx_shutdown();
}

static void test_kernel_is_symmetric_normalized_and_deterministic(void) {
    float weights_a[NT_POSTFX_BLUR_MAX_KERNEL] = {0};
    float weights_b[NT_POSTFX_BLUR_MAX_KERNEL] = {0};

    uint32_t count_a = nt_postfx_blur_test_build_kernel(3.0F, 1.5F, weights_a);
    uint32_t count_b = nt_postfx_blur_test_build_kernel(3.0F, 1.5F, weights_b);

    TEST_ASSERT_EQUAL_UINT32(7, count_a);
    TEST_ASSERT_EQUAL_UINT32(count_a, count_b);

    TEST_ASSERT_TRUE(weights_are_finite_and_equal(weights_a, weights_b, count_a));
    TEST_ASSERT_TRUE(float_near(sum_weights(weights_a, count_a), 1.0F, 0.0001F));
    TEST_ASSERT_TRUE(float_near(weights_a[0], weights_a[6], 0.000001F));
    TEST_ASSERT_TRUE(float_near(weights_a[1], weights_a[5], 0.000001F));
    TEST_ASSERT_TRUE(float_near(weights_a[2], weights_a[4], 0.000001F));
    TEST_ASSERT_TRUE(weights_a[3] > weights_a[2]);
}

static void test_kernel_derives_sigma_when_zero(void) {
    float derived[NT_POSTFX_BLUR_MAX_KERNEL] = {0};
    float explicit_sigma[NT_POSTFX_BLUR_MAX_KERNEL] = {0};

    uint32_t derived_count = nt_postfx_blur_test_build_kernel(6.0F, 0.0F, derived);
    uint32_t explicit_count = nt_postfx_blur_test_build_kernel(6.0F, 2.0F, explicit_sigma);

    TEST_ASSERT_EQUAL_UINT32(13, derived_count);
    TEST_ASSERT_EQUAL_UINT32(derived_count, explicit_count);
    for (uint32_t i = 0; i < derived_count; i++) {
        TEST_ASSERT_TRUE(float_near(explicit_sigma[i], derived[i], 0.000001F));
    }
}

static void test_invalid_descriptors_assert_without_draw(void) {
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);
    nt_texture_t source = nt_gfx_render_target_color(dest);

    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(NULL));
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){.source = (nt_texture_t){0}, .temp = temp, .dest = dest, .radius = 4.0F}));
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){.source = source, .temp = NT_RENDER_TARGET_INVALID, .dest = dest, .radius = 4.0F}));
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){.source = source, .temp = temp, .dest = NT_RENDER_TARGET_INVALID, .radius = 4.0F}));
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){.source = source, .temp = temp, .dest = dest, .radius = 0.0F}));
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){.source = source, .temp = temp, .dest = dest, .radius = -1.0F}));
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){.source = source, .temp = temp, .dest = dest, .radius = NAN}));
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){.source = source, .temp = temp, .dest = dest, .radius = 4.0F, .sigma = -1.0F}));

    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
}

static void test_feedback_aliases_assert_without_draw(void) {
    nt_render_target_desc_t source_desc = blur_rt_desc(64, 32, "source");
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t source_rt = nt_gfx_make_render_target(&source_desc);
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);
    nt_texture_t source = nt_gfx_render_target_color(source_rt);

    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = nt_gfx_render_target_color(temp),
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    }));
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = source,
        .temp = dest,
        .dest = dest,
        .radius = 4.0F,
    }));

    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
}

static void test_depth_feedback_alias_asserts_without_draw(void) {
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    temp_desc.depth_storage = NT_RT_DEPTH_TEXTURE;
    temp_desc.depth_format = NT_TEXTURE_FORMAT_DEPTH24;
    temp_desc.depth_texture_min_filter = NT_FILTER_NEAREST;
    temp_desc.depth_texture_mag_filter = NT_FILTER_NEAREST;
    temp_desc.depth_texture_wrap_u = NT_WRAP_CLAMP_TO_EDGE;
    temp_desc.depth_texture_wrap_v = NT_WRAP_CLAMP_TO_EDGE;
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);

    nt_gfx_begin_frame();
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = nt_gfx_render_target_depth(temp),
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    }));
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
}

static void test_stale_source_asserts_without_draw(void) {
    nt_render_target_desc_t source_desc = blur_rt_desc(64, 32, "source");
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t source_rt = nt_gfx_make_render_target(&source_desc);
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);
    nt_texture_t stale_source = nt_gfx_render_target_color(source_rt);
    nt_gfx_destroy_render_target(source_rt);

    nt_gfx_begin_frame();
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = stale_source,
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    }));
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
}

static void test_integer_source_asserts_without_draw(void) {
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);
    nt_texture_t source = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 64,
        .height = 32,
        .format = NT_TEXTURE_FORMAT_RG16UI,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = "integer_source",
    });

    nt_gfx_begin_frame();
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = source,
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    }));
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
}

static void test_blur_outside_frame_asserts_without_draw(void) {
    nt_render_target_desc_t source_desc = blur_rt_desc(64, 32, "source");
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t source_rt = nt_gfx_make_render_target(&source_desc);
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);

    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = nt_gfx_render_target_color(source_rt),
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    }));
    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
}

static void test_blur_inside_active_pass_asserts_without_closing_it(void) {
    nt_render_target_desc_t source_desc = blur_rt_desc(64, 32, "source");
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t source_rt = nt_gfx_make_render_target(&source_desc);
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = nt_gfx_render_target_color(source_rt),
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    }));
    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_incomplete_targets_assert_without_draw(void) {
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t source_desc = blur_rt_desc(64, 32, "source");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t source_rt = nt_gfx_make_render_target(&source_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);

    nt_gfx_stub_test_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_stub_test_fail_next_render_target_create();
    nt_gfx_stub_test_set_context_lost(false);
    nt_gfx_begin_frame();

    TEST_ASSERT_FALSE(nt_gfx_render_target_ready(temp));
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = nt_gfx_render_target_color(source_rt),
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    }));

    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
    nt_gfx_end_frame();
}

static void test_mixed_size_targets_assert_without_draw(void) {
    nt_render_target_desc_t source_desc = blur_rt_desc(64, 32, "source");
    nt_render_target_desc_t temp_desc = blur_rt_desc(32, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t source_rt = nt_gfx_make_render_target(&source_desc);
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);

    nt_gfx_begin_frame();
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = nt_gfx_render_target_color(source_rt),
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    }));
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
}

static void test_enabled_scissor_asserts_without_draw(void) {
    nt_render_target_desc_t source_desc = blur_rt_desc(64, 32, "source");
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t source_rt = nt_gfx_make_render_target(&source_desc);
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);

    nt_gfx_begin_frame();
    nt_gfx_set_scissor(0, 0, 1, 1);
    nt_gfx_set_scissor_enabled(true);
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = nt_gfx_render_target_color(source_rt),
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    }));
    TEST_ASSERT_TRUE(nt_gfx_scissor_enabled());
    nt_gfx_set_scissor_enabled(false);
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(0, nt_postfx_blur_test_draw_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_stub_test_pass_target_count());
}

static void test_valid_blur_uses_two_passes_and_no_hidden_target_allocation(void) {
    nt_render_target_desc_t source_desc = blur_rt_desc(64, 32, "source");
    nt_render_target_desc_t temp_desc = blur_rt_desc(64, 32, "temp");
    nt_render_target_desc_t dest_desc = blur_rt_desc(64, 32, "dest");
    nt_render_target_t source_rt = nt_gfx_make_render_target(&source_desc);
    nt_render_target_t temp = nt_gfx_make_render_target(&temp_desc);
    nt_render_target_t dest = nt_gfx_make_render_target(&dest_desc);
    nt_texture_t source = nt_gfx_render_target_color(source_rt);
    nt_texture_t temp_color = nt_gfx_render_target_color(temp);
    uint32_t creates_before = nt_gfx_stub_test_render_target_create_count();

    nt_gfx_begin_frame();
    nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = source,
        .temp = temp,
        .dest = dest,
        .radius = 4.0F,
    });
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(creates_before, nt_gfx_stub_test_render_target_create_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_postfx_blur_test_draw_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_get_frame_draw_calls());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_stub_test_pass_target_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_render_target_backend_id(temp), nt_gfx_stub_test_pass_target_at(0));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_render_target_backend_id(dest), nt_gfx_stub_test_pass_target_at(1));
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_stub_test_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(source), nt_gfx_stub_test_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(temp_color), nt_gfx_stub_test_bound_texture_at(1));
}

static void test_blur_lifecycle_misuse_asserts(void) {
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_init());

    nt_postfx_blur_shutdown();
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_restore_gpu());
    NT_TEST_EXPECT_ASSERT(nt_postfx_blur_gaussian(NULL));
}

static void test_source_does_not_expose_blur_through_nt_gfx_or_allocate_targets(void) {
    FILE *impl = fopen("engine/postfx/nt_postfx_blur.c", "rb");
    TEST_ASSERT_NOT_NULL(impl);
    char impl_buf[32768];
    size_t impl_n = fread(impl_buf, 1, sizeof(impl_buf) - 1U, impl);
    (void)fclose(impl);
    impl_buf[impl_n] = '\0';
    TEST_ASSERT_NULL(strstr(impl_buf, "nt_gfx_make_render_target"));
    TEST_ASSERT_NULL(strstr(impl_buf, "nt_gfx_resize_render_target"));

    FILE *gfx = fopen("engine/graphics/nt_gfx.h", "rb");
    TEST_ASSERT_NOT_NULL(gfx);
    char gfx_buf[32768];
    size_t gfx_n = fread(gfx_buf, 1, sizeof(gfx_buf) - 1U, gfx);
    (void)fclose(gfx);
    gfx_buf[gfx_n] = '\0';
    TEST_ASSERT_NULL(strstr(gfx_buf, "nt_postfx_blur"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_kernel_is_symmetric_normalized_and_deterministic);
    RUN_TEST(test_kernel_derives_sigma_when_zero);
    RUN_TEST(test_invalid_descriptors_assert_without_draw);
    RUN_TEST(test_feedback_aliases_assert_without_draw);
    RUN_TEST(test_depth_feedback_alias_asserts_without_draw);
    RUN_TEST(test_stale_source_asserts_without_draw);
    RUN_TEST(test_integer_source_asserts_without_draw);
    RUN_TEST(test_blur_outside_frame_asserts_without_draw);
    RUN_TEST(test_blur_inside_active_pass_asserts_without_closing_it);
    RUN_TEST(test_incomplete_targets_assert_without_draw);
    RUN_TEST(test_mixed_size_targets_assert_without_draw);
    RUN_TEST(test_enabled_scissor_asserts_without_draw);
    RUN_TEST(test_valid_blur_uses_two_passes_and_no_hidden_target_allocation);
    RUN_TEST(test_blur_lifecycle_misuse_asserts);
    RUN_TEST(test_source_does_not_expose_blur_through_nt_gfx_or_allocate_targets);
    return UNITY_END();
}
