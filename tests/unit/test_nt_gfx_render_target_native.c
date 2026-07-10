#include "graphics/nt_gfx.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stdint.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

static void assert_rgba(const uint8_t *pixels, uint32_t pixel_count, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (uint32_t i = 0; i < pixel_count; i++) {
        TEST_ASSERT_UINT8_WITHIN(1, r, pixels[i * 4U]);
        TEST_ASSERT_UINT8_WITHIN(1, g, pixels[i * 4U + 1U]);
        TEST_ASSERT_UINT8_WITHIN(1, b, pixels[i * 4U + 2U]);
        TEST_ASSERT_UINT8_WITHIN(1, a, pixels[i * 4U + 3U]);
    }
}

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){
        .max_dpr = 1.0F,
        .resizable = false,
        .width = 64,
        .height = 64,
    };
    nt_window_init();

    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.max_textures = 2;
    desc.max_render_targets = 1;
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_window_shutdown();
}

static void test_render_target_resize_without_spare_texture_slots(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_PIXEL_RGBA8,
        .depth = NT_RT_DEPTH_TEXTURE,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = "native_rt_smoke",
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(target));

    uint8_t first_pixels[4U * 4U * 4U] = {0};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = target,
        .clear_color = {0.25F, 0.5F, 0.75F, 1.0F},
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 4, 4, first_pixels, sizeof(first_pixels)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    assert_rgba(first_pixels, 16, 64, 128, 191, 255);

    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(target, 7, 5));
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(target));

    uint8_t resized_pixels[7U * 5U * 4U] = {0};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = target,
        .clear_color = {1.0F, 0.25F, 0.5F, 1.0F},
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 7, 5, resized_pixels, sizeof(resized_pixels)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    assert_rgba(resized_pixels, 35, 255, 64, 128, 255);

    nt_gfx_destroy_render_target(target);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_render_target_resize_without_spare_texture_slots);
    return UNITY_END();
}
