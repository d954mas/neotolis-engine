/* Real-GL proof that a glyph missing from the cache draws on its first appearance: its curve row
 * is written while the draw is recorded, and the draw executes after the write. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "nt_font_format.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_gfx_test_desc.h"
#include "unity.h"
#include "window/nt_window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

enum { FB_SIZE = 32, CAPTURE_BYTES = 512 * 64 * 4, MAX_GLYPHS = 4 };

/* Em units straight to NDC: at size = units_per_em a glyph spans its bbox / 1000. */
static const char *s_vs_src = "precision highp float;\n"
                              "layout(location = 0) in vec3 a_position;\n"
                              "layout(location = 2) in vec2 a_glyph_data;\n"
                              "flat out uint v_row;\n"
                              "void main() { v_row = floatBitsToUint(a_glyph_data.x); gl_Position = vec4(a_position.xy * 0.001, 0.0, 1.0); }\n";

/* Green where the glyph's row header holds curves, red where the row is still empty. */
static const char *s_fs_src = "precision highp float;\n"
                              "precision highp int;\n"
                              "uniform highp sampler2D u_curve_texture;\n"
                              "flat in uint v_row;\n"
                              "out vec4 frag_color;\n"
                              "void main() {\n"
                              "    vec4 h = texelFetch(u_curve_texture, ivec2(0, int(v_row)), 0);\n"
                              "    frag_color = (h.y + h.w > 0.5) ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
                              "}\n";

static uint8_t s_pixels[CAPTURE_BYTES];
static uint8_t *s_blob;
static nt_font_t s_font;
static nt_material_t s_material;

/* 'A' and 'B': one triangle each, advance 500, bbox 0..400 x -200..800 in a 1000-unit em. */
static uint8_t *build_font_blob(uint32_t *out_size) {
    static const uint8_t contour[14] = {1, 0, 3, 0, 0x07, 0x00, 0, 0, 0, 0, 50, 0, (uint8_t)(int8_t)-50, 50};
    const uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    const uint32_t glyphs_size = 2U * (uint32_t)sizeof(NtFontGlyphEntry);
    const uint32_t total_size = header_size + glyphs_size + (2U * (uint32_t)sizeof contour);
    uint8_t *blob = (uint8_t *)calloc(total_size, 1);
    TEST_ASSERT_NOT_NULL(blob);

    NtFontAssetHeader hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.magic = NT_FONT_MAGIC;
    hdr.version = NT_FONT_VERSION;
    hdr.glyph_count = 2;
    hdr.units_per_em = 1000;
    hdr.ascent = 800;
    hdr.descent = -200;
    memcpy(blob, &hdr, sizeof hdr);

    for (uint32_t g = 0; g < 2U; g++) {
        NtFontGlyphEntry entry;
        memset(&entry, 0, sizeof entry);
        entry.codepoint = 'A' + g;
        entry.data_offset = header_size + glyphs_size + (g * (uint32_t)sizeof contour);
        entry.advance = 500;
        entry.bbox_y0 = -200;
        entry.bbox_x1 = 400;
        entry.bbox_y1 = 800;
        entry.curve_count = 3;
        memcpy(blob + header_size + ((size_t)g * sizeof entry), &entry, sizeof entry);
        memcpy(blob + entry.data_offset, contour, sizeof contour);
    }
    *out_size = total_size;
    return blob;
}

void setUp(void) {
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&NT_GFX_TEST_DESC(.max_shaders = 4, .max_programs = 2, .max_pipelines = 2, .max_buffers = 8, .max_textures = 4, .max_meshes = 2, .max_vertex_inputs = 4, .max_render_targets = 2));
    nt_gfx_begin_frame();
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_material_init(&(nt_material_desc_t){.max_materials = 2});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 1});

    uint32_t size = 0;
    s_blob = build_font_blob(&size);
    s_font = nt_font_create(&(nt_font_create_desc_t){.max_glyphs = MAX_GLYPHS});
    nt_font_add(s_font, nt_font_test_resource(nt_font_test_register_data(s_blob, size)));
    nt_resource_step();
    nt_font_step();

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    s_material = nt_material_create(&(nt_material_create_desc_t){.program = nt_gfx_make_program(vs, fs), .cull_mode = NT_CULL_NONE, .label = "native_text_row"});
}

void tearDown(void) {
    nt_text_renderer_shutdown();
    nt_material_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    nt_resource_shutdown();
    nt_gfx_shutdown();
    nt_hash_shutdown();
    free(s_blob);
    s_blob = NULL;
}

/* The window may be wider than requested (OS minimum width), so probes are in NDC. */
static void assert_ndc_pixel(float x, float y, uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t w = g_nt_window.fb_width;
    const uint32_t h = g_nt_window.fb_height;
    const uint32_t px = (uint32_t)((x + 1.0F) * 0.5F * (float)w);
    const uint32_t py = (uint32_t)((1.0F - y) * 0.5F * (float)h); /* captured rows are top-left */
    const uint8_t expected[3] = {r, g, b};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, &s_pixels[((size_t)py * w + px) * 4U], 3);
}

/* 'A' misses before the run opens and 'B' in the middle of it; both rows are written after
 * the frame's first recorded command and before its draws execute. */
void test_cold_glyph_misses_draw_in_their_first_frame(void) {
    /* Every glyph row empty (row 0 is the tofu), so a draw that ran before its row arrived reads red. */
    static uint16_t zeros[(MAX_GLYPHS - 1) * 2048 * 4];
    nt_gfx_update_texture(nt_font_get_curve_texture(s_font), 0, 1, 2048, MAX_GLYPHS - 1, zeros);

    nt_text_renderer_set_material(s_material);
    nt_text_renderer_set_font(s_font);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nt_text_renderer_draw("AB", identity, 1000.0F, 0xFFFFFFFFU, 0.0F, 0.0F);

    const uint32_t w = g_nt_window.fb_width;
    const uint32_t h = g_nt_window.fb_height;
    const bool read = nt_gfx_read_pixels(0, 0, (int)w, (int)h, s_pixels, sizeof(s_pixels));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE_MESSAGE(read, "framebuffer larger than the capture buffer");
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());

    assert_ndc_pixel(0.2F, 0.3F, 0, 255, 0); /* 'A' */
    assert_ndc_pixel(0.7F, 0.3F, 0, 255, 0); /* 'B' */
    assert_ndc_pixel(-0.5F, -0.5F, 0, 0, 0); /* outside both quads */
}

int main(void) {
    /* One hidden window and GL context serve every test; setUp/tearDown reset only engine state. */
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){.max_dpr = 1.0F, .resizable = false, .width = FB_SIZE, .height = FB_SIZE};
    nt_window_init();
    UNITY_BEGIN();
    RUN_TEST(test_cold_glyph_misses_draw_in_their_first_frame);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
