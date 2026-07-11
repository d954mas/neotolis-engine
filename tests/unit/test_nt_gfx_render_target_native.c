#include "graphics/nt_gfx.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stddef.h>
#include <stdint.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

static void assert_rgba(const uint8_t *pixels, uint32_t pixel_count, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (uint32_t i = 0; i < pixel_count; i++) {
        const uint8_t *pixel = &pixels[(size_t)i * 4U];
        TEST_ASSERT_UINT8_WITHIN(1, r, pixel[0]);
        TEST_ASSERT_UINT8_WITHIN(1, g, pixel[1]);
        TEST_ASSERT_UINT8_WITHIN(1, b, pixel[2]);
        TEST_ASSERT_UINT8_WITHIN(1, a, pixel[3]);
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
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_TEXTURE,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
        .depth_texture_min_filter = NT_FILTER_NEAREST,
        .depth_texture_mag_filter = NT_FILTER_NEAREST,
        .depth_texture_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .depth_texture_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
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

static void test_depth_texture_uses_explicit_format_and_wrap(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_TEXTURE,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH16,
        .depth_texture_min_filter = NT_FILTER_NEAREST,
        .depth_texture_mag_filter = NT_FILTER_NEAREST,
        .depth_texture_wrap_u = NT_WRAP_REPEAT,
        .depth_texture_wrap_v = NT_WRAP_MIRRORED_REPEAT,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(target, 6, 5));

    nt_gfx_bind_texture(nt_gfx_render_target_depth(target), 0);
    GLint value = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &value);
    TEST_ASSERT_EQUAL_INT(GL_DEPTH_COMPONENT16, value);
    GLint sampler = 0;
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
    TEST_ASSERT_NOT_EQUAL_INT(0, sampler);
    glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_MIN_FILTER, &value);
    TEST_ASSERT_EQUAL_INT(GL_NEAREST, value);
    glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_MAG_FILTER, &value);
    TEST_ASSERT_EQUAL_INT(GL_NEAREST, value);
    glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_WRAP_S, &value);
    TEST_ASSERT_EQUAL_INT(GL_REPEAT, value);
    glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_WRAP_T, &value);
    TEST_ASSERT_EQUAL_INT(GL_MIRRORED_REPEAT, value);

    nt_gfx_destroy_render_target(target);
}

static void test_depth_buffer_uses_explicit_format(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_BUFFER,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH32F,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_depth = 1.0F});
    GLint object_type = 0;
    GLint renderbuffer = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &object_type);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &renderbuffer);
    TEST_ASSERT_EQUAL_INT(GL_RENDERBUFFER, object_type);
    TEST_ASSERT_NOT_EQUAL_INT(0, renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)renderbuffer);
    GLint format = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
    TEST_ASSERT_EQUAL_INT(GL_DEPTH_COMPONENT32F, format);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_render_target(target);
}

static void test_begin_pass_clears_depth_after_depth_writes_were_disabled(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0); }\n";
    static const char *fragment_source = "#ifdef GL_ES\n"
                                         "precision mediump float;\n"
                                         "#endif\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(1.0); }\n";

    nt_shader_t vertex_shader = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fragment_shader = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_pipeline_t no_depth_write_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .depth_write = false,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, no_depth_write_pipeline.id);

    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_BUFFER,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_depth = 0.25F});
    nt_gfx_bind_pipeline(no_depth_write_pipeline);
    nt_gfx_end_pass();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_depth = 0.75F});
    float depth = 0.0F;
    glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    uint32_t depth_milli = (uint32_t)((depth * 1000.0F) + 0.5F);
    TEST_ASSERT_UINT32_WITHIN(1, 750, depth_milli);
    GLboolean depth_write_enabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write_enabled);
    TEST_ASSERT_EQUAL_INT(GL_FALSE, depth_write_enabled);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_render_target(target);
    nt_gfx_destroy_pipeline(no_depth_write_pipeline);
    nt_gfx_destroy_shader(fragment_shader);
    nt_gfx_destroy_shader(vertex_shader);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_render_target_resize_without_spare_texture_slots);
    RUN_TEST(test_depth_texture_uses_explicit_format_and_wrap);
    RUN_TEST(test_depth_buffer_uses_explicit_format);
    RUN_TEST(test_begin_pass_clears_depth_after_depth_writes_were_disabled);
    return UNITY_END();
}
