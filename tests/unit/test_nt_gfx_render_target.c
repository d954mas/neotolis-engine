/* Render-target API mechanics via the stub backend. */

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "unity.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static nt_render_target_desc_t rt_desc(nt_render_target_depth_t depth) {
    return (nt_render_target_desc_t){
        .width = 64,
        .height = 32,
        .color_format = NT_PIXEL_RGBA8,
        .depth = depth,
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = "test_rt",
    };
}

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){
        .max_shaders = 4,
        .max_pipelines = 4,
        .max_buffers = 8,
        .max_textures = 8,
        .max_meshes = 4,
        .max_render_targets = 4,
    });
    nt_gfx_stub_test_reset();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) { nt_gfx_shutdown(); }

static void test_create_returns_target_and_color_attachment(void) {
    nt_render_target_t rt = nt_gfx_make_render_target(&rt_desc(NT_RT_DEPTH_NONE));

    TEST_ASSERT_NOT_EQUAL_UINT32(0, rt.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_render_target_color(rt).id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_stub_test_render_target_create_count());

    nt_gfx_destroy_render_target(rt);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_stub_test_render_target_destroy_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_color(rt).id);
}

static void test_depth_accessor_matches_depth_mode(void) {
    nt_render_target_t none = nt_gfx_make_render_target(&rt_desc(NT_RT_DEPTH_NONE));
    nt_render_target_t buffer = nt_gfx_make_render_target(&rt_desc(NT_RT_DEPTH_BUFFER));
    nt_render_target_t texture = nt_gfx_make_render_target(&rt_desc(NT_RT_DEPTH_TEXTURE));

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_depth(none).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_depth(buffer).id);
    nt_texture_t depth = nt_gfx_render_target_depth(texture);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, depth.id);
    TEST_ASSERT_EQUAL_UINT32(depth.id, nt_gfx_render_target_depth(texture).id);
    TEST_ASSERT_EQUAL_INT(NT_RT_DEPTH_TEXTURE, nt_gfx_stub_test_last_render_target_depth());
}

static void test_pass_target_routes_to_backend(void) {
    nt_render_target_t rt = nt_gfx_make_render_target(&rt_desc(NT_RT_DEPTH_BUFFER));

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = rt,
        .clear_color = {0.1F, 0.2F, 0.3F, 1.0F},
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_stub_test_last_pass_target());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_zero_pass_target_routes_to_default_framebuffer(void) {
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = NT_RENDER_TARGET_INVALID,
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_stub_test_last_pass_target());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_invalid_handles_return_invalid_attachments(void) {
    nt_render_target_t invalid = NT_RENDER_TARGET_INVALID;

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_color(invalid).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_depth(invalid).id);
}

static void test_header_does_not_expose_target_bind_state_api(void) {
    FILE *f = fopen("engine/graphics/nt_gfx.h", "rb");
    TEST_ASSERT_NOT_NULL(f);

    char buf[32768];
    size_t n = fread(buf, 1, sizeof(buf) - 1U, f);
    fclose(f);
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
    RUN_TEST(test_depth_accessor_matches_depth_mode);
    RUN_TEST(test_pass_target_routes_to_backend);
    RUN_TEST(test_zero_pass_target_routes_to_default_framebuffer);
    RUN_TEST(test_invalid_handles_return_invalid_attachments);
    RUN_TEST(test_header_does_not_expose_target_bind_state_api);
    return UNITY_END();
}
