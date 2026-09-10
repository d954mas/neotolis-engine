#include "app/nt_app.h"
#include "basisu/nt_basisu_transcoder.h"
#include "core/nt_core.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "nt_pack_format.h"
#include "platform/web/nt_platform_web.h"
#include "resource/nt_resource.h"
#include "window/nt_window.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 96U
#define HEIGHT 64U
#define LEVELS 7U
#define CHECK(expr) check((expr), #expr, __LINE__)

static nt_resource_t s_textures[4];
static bool s_ready[4];
static uint8_t s_reference[4][LEVELS][WIDTH * HEIGHT * 4];
static uint32_t s_cpu_rows;
static uint32_t s_gpu_rows;
static nt_pipeline_t s_pipeline;
static nt_vertex_input_t s_vertex_input;
static nt_render_target_t s_target;

static void check(bool ok, const char *expression, int line) {
    if (!ok) {
        (void)fprintf(stderr, "Basis fixture failed at %d: %s\n", line, expression);
        abort();
    }
}

/* clang-format off */
EM_JS(void, publish, (const char *preset, int cpu_rows, int gpu_rows, int bc7, int astc, int etc2, int assert_mode), {
    window['basisResult'] = {'preset': UTF8ToString(preset), 'cpuRows': cpu_rows, 'gpuRows': gpu_rows,
        'bc7': !!bc7, 'astc': !!astc, 'etc2': !!etc2, 'assertMode': assert_mode};
})
EM_JS_DEPS(publish, "$UTF8ToString")
/* clang-format on */

static void check_source(const uint8_t *rgba, bool alpha) {
    uint32_t errors[4] = {0};
    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            const uint32_t expected[] = {32U + (x * 160U / WIDTH), 24U + (y * 120U / HEIGHT), ((x / 4U + y / 8U) & 1U) ? 208U : 48U, alpha ? 40U + (x * 180U / WIDTH) : 255U};
            for (uint32_t c = 0; c < 4; c++) {
                int delta = (int)rgba[((((size_t)y * WIDTH) + x) * 4) + c] - (int)expected[c];
                errors[c] += (uint32_t)(delta < 0 ? -delta : delta);
            }
        }
    }
    for (uint32_t c = 0; c < 4; c++) {
        CHECK(errors[c] / (WIDTH * HEIGHT) < 24U);
    }
}

static void convert(const uint8_t *basis, uint32_t size, uint32_t index) {
    const nt_basisu_format_t formats[] = {NT_BASISU_FORMAT_ETC1_RGB, NT_BASISU_FORMAT_ETC2_RGBA, NT_BASISU_FORMAT_BC7_RGBA, NT_BASISU_FORMAT_ASTC_4x4_RGBA, NT_BASISU_FORMAT_RGBA32};
    CHECK(nt_basisu_get_level_count(basis, size) == LEVELS);
    CHECK(nt_basisu_start_transcoding(basis, size));
    for (uint32_t level = 0; level < LEVELS; level++) {
        uint32_t w = 0;
        uint32_t h = 0;
        uint32_t blocks = 0;
        CHECK(nt_basisu_get_level_desc(basis, size, level, &w, &h, &blocks));
        CHECK(w == ((WIDTH >> level) ? (WIDTH >> level) : 1U));
        CHECK(h == (HEIGHT >> level));
        for (uint32_t f = 0; f < 5; f++) {
            uint8_t out[(WIDTH * HEIGHT * 4) + 16];
            memset(out, 0xCD, sizeof(out));
            uint32_t count = formats[f] == NT_BASISU_FORMAT_RGBA32 ? w * h : blocks;
            uint32_t bytes = count * nt_basisu_bytes_per_block(formats[f]);
            CHECK(nt_basisu_transcode_level(basis, size, level, out, count, formats[f]));
            for (uint32_t i = bytes; i < bytes + 16; i++) {
                CHECK(out[i] == 0xCD);
            }
            if (formats[f] == NT_BASISU_FORMAT_RGBA32) {
                memcpy(s_reference[index][level], out, bytes);
            }
            s_cpu_rows++;
        }
    }
    nt_basisu_stop_transcoding();
    check_source(s_reference[index][0], (index & 1U) != 0);
}

static void resolved(const uint8_t *data, uint32_t size, nt_resource_t handle, uint32_t runtime_handle, void *user_data) {
    (void)user_data;
    CHECK(runtime_handle != 0);
    CHECK(data != NULL && size >= sizeof(NtTextureAssetHeader));
    const NtTextureAssetHeader *header = (const NtTextureAssetHeader *)data;
    CHECK(header->compression == NT_TEXTURE_COMPRESSION_BASIS);
    CHECK(header->width == WIDTH && header->height == HEIGHT && header->mip_count == LEVELS);
    CHECK(header->data_size == size - sizeof(*header));
    for (uint32_t i = 0; i < 4; i++) {
        if (handle.id == s_textures[i].id && !s_ready[i]) {
            convert(data + sizeof(*header), header->data_size, i);
            s_ready[i] = true;
        }
    }
}

static void compare_readback(uint32_t index, uint32_t level, const uint8_t *pixels) {
    uint32_t w = (WIDTH >> level) ? (WIDTH >> level) : 1U;
    uint32_t h = HEIGHT >> level;
    uint32_t errors[4] = {0};
    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            /* Public readback is top-down; gl_FragCoord and uploaded rows start at the bottom. */
            size_t offset = (((size_t)((HEIGHT - 1U - y) * h / HEIGHT) * w) + (x * w / WIDTH)) * 4;
            for (uint32_t c = 0; c < 4; c++) {
                int delta = (int)pixels[((((size_t)y * WIDTH) + x) * 4) + c] - (int)s_reference[index][level][offset + c];
                errors[c] += (uint32_t)(delta < 0 ? -delta : delta);
            }
        }
    }
    for (uint32_t c = 0; c < 4; c++) {
        if (errors[c] / (WIDTH * HEIGHT) >= 24U) {
            (void)fprintf(stderr, "sample texture=%u mip=%u channel=%u mae=%u first=%u expected=%u\n", index, level, c, errors[c] / (WIDTH * HEIGHT), pixels[c], s_reference[index][level][c]);
        }
        CHECK(errors[c] / (WIDTH * HEIGHT) < 24U);
    }
}

static void sample(void) {
    for (uint32_t i = 0; i < 4; i++) {
        nt_texture_t texture = {nt_resource_get(s_textures[i])};
        CHECK(nt_gfx_texture_ready(texture));
        for (uint32_t level = 0; level < LEVELS; level++) {
            nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target});
            nt_gfx_bind_pipeline(s_pipeline);
            nt_gfx_bind_vertex_input(s_vertex_input);
            nt_gfx_apply_texture_bindings(&(nt_gfx_texture_binding_t){.name = nt_hash32_str("u_texture"), .texture = texture}, 1);
            nt_gfx_set_uniform_float(nt_hash32_str("u_lod"), (float)level);
            nt_gfx_draw(0, 3);
            uint8_t pixels[WIDTH * HEIGHT * 4];
            CHECK(nt_gfx_read_pixels(0, 0, WIDTH, HEIGHT, pixels, sizeof(pixels)));
            nt_gfx_end_pass();
            compare_readback(i, level, pixels);
            s_gpu_rows++;
        }
    }
}

static void frame(void) {
    static bool done;
    nt_window_poll();
    nt_gfx_begin_frame();
    nt_resource_step();
    if (!done && s_ready[0] && s_ready[1] && s_ready[2] && s_ready[3]) {
        sample();
        const nt_gfx_gpu_caps_t *caps = nt_gfx_gpu_caps();
        publish(NT_TEST_PRESET_NAME, (int)s_cpu_rows, (int)s_gpu_rows, caps->has_bc7, caps->has_astc, caps->has_etc2, NT_ASSERT_MODE);
        done = true;
        nt_platform_web_loading_complete();
    }
    nt_gfx_end_frame();
}

int main(void) {
    CHECK(nt_engine_init(&(nt_engine_config_t){.app_name = "basis_fixture", .version = 1}) == NT_OK);
    g_nt_window.width = WIDTH;
    g_nt_window.height = HEIGHT;
    nt_window_init();
    nt_input_init();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    CHECK(g_nt_gfx.initialized);
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_http_init();
    CHECK(nt_resource_init(&(nt_resource_desc_t){0}) == NT_OK);
    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_post_resolve_callback(NT_ASSET_TEXTURE, resolved);
    const char *names[] = {"etc1s_rgb", "etc1s_alpha", "uastc_rgb", "uastc_alpha"};
    for (uint32_t i = 0; i < 4; i++) {
        s_textures[i] = nt_resource_request(nt_hash64_str(names[i]), NT_ASSET_TEXTURE);
    }
    nt_hash32_t pack = nt_hash32_str("basis_fixture");
    CHECK(nt_resource_mount(pack, 0) == NT_OK);
    CHECK(nt_resource_load_url(pack, "assets/fixture.ntpack") == NT_OK);
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT,
                                                            .source = "precision highp float;uniform sampler2D u_texture;uniform float u_lod;out vec4 color;"
                                                                      "void main(){color=textureLod(u_texture,gl_FragCoord.xy/vec2(96.0,64.0),u_lod);}"});
    nt_program_t program = nt_gfx_make_program(vs, fs);
    CHECK(nt_gfx_program_ready(program));
    s_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    s_vertex_input = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    s_target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.width = WIDTH, .height = HEIGHT, .color_format = NT_TEXTURE_FORMAT_RGBA8});
    CHECK(s_target.id != 0);
    nt_app_run(frame);
    return 0;
}
