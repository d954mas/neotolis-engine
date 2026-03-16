/*
 * Textured Quad Demo - Neotolis Engine
 *
 * Renders a quad textured with a hardcoded 4x4 checkerboard pattern.
 * Proves the full texture pipeline: create, bind, draw, destroy.
 */

#include "app/nt_app.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "graphics/nt_gfx.h"
#include "input/nt_input.h"
#include "window/nt_window.h"

#include "math/nt_math.h"

#include <stdint.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

/* ---- Shader sources (no version prefix -- backend adds it) ---- */

static const char *s_vs_source = "in vec3 a_position;\n"
                                 "in vec2 a_texcoord;\n"
                                 "out vec2 v_uv;\n"
                                 "uniform mat4 u_mvp;\n"
                                 "void main() {\n"
                                 "    v_uv = a_texcoord;\n"
                                 "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
                                 "}\n";

static const char *s_fs_source = "uniform sampler2D u_texture;\n"
                                 "in vec2 v_uv;\n"
                                 "out vec4 frag_color;\n"
                                 "void main() {\n"
                                 "    frag_color = texture(u_texture, v_uv);\n"
                                 "}\n";

/* ---- 4x4 checkerboard RGBA8 pixel data ---- */

/* clang-format off */
static const uint8_t s_checker_4x4[4 * 4 * 4] = {
    255,255,255,255,  0,0,0,255,      255,255,255,255,  0,0,0,255,
    0,0,0,255,        255,255,255,255, 0,0,0,255,        255,255,255,255,
    255,255,255,255,  0,0,0,255,      255,255,255,255,  0,0,0,255,
    0,0,0,255,        255,255,255,255, 0,0,0,255,        255,255,255,255,
};
/* clang-format on */

/* ---- Quad geometry ---- */

static const float s_quad_vertices[] = {
    /* pos x,y,z       uv u,v */
    -0.5F, -0.5F, 0.0F, 0.0F, 0.0F, 0.5F, -0.5F, 0.0F, 1.0F, 0.0F, 0.5F, 0.5F, 0.0F, 1.0F, 1.0F, -0.5F, 0.5F, 0.0F, 0.0F, 1.0F,
};

static const uint16_t s_quad_indices[] = {0, 1, 2, 0, 2, 3};

/* ---- Resource handles ---- */

static nt_pipeline_t s_pipeline;
static nt_buffer_t s_vbo;
static nt_buffer_t s_ibo;
static nt_texture_t s_texture;

/* ---- Frame callback ---- */

static void frame(void) {
    nt_window_poll();
    nt_input_poll();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    /* Build MVP: perspective camera looking at quad */
    float aspect = 1.0F;
    if (g_nt_window.fb_height > 0) {
        aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    }

    mat4 proj;
    mat4 view;
    mat4 mvp;
    glm_perspective(glm_rad(60.0F), aspect, 0.1F, 10.0F, proj);
    glm_lookat((vec3){0.0F, 0.0F, 2.0F}, (vec3){0.0F, 0.0F, 0.0F}, (vec3){0.0F, 1.0F, 0.0F}, view);
    glm_mat4_mul(proj, view, mvp);

    /* Render */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.2F, 0.2F, 0.2F, 1.0F}, .clear_depth = 1.0F});

    nt_gfx_bind_pipeline(s_pipeline);
    nt_gfx_bind_vertex_buffer(s_vbo);
    nt_gfx_bind_index_buffer(s_ibo);
    nt_gfx_bind_texture(s_texture, 0);

    nt_gfx_set_uniform_mat4("u_mvp", (float *)mvp);
    nt_gfx_set_uniform_int("u_texture", 0);

    nt_gfx_draw_indexed(0, 6, 4);

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_window_swap_buffers();
}

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "textured_quad";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        return 1;
    }

    g_nt_window.width = 800;
    g_nt_window.height = 600;
    nt_window_init();
    nt_input_init();

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx_desc);

    /* Create shaders */
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_VERTEX,
        .source = s_vs_source,
        .label = "textured_quad_vs",
    });
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_FRAGMENT,
        .source = s_fs_source,
        .label = "textured_quad_fs",
    });

    /* Create pipeline */
    s_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .layout =
            {
                .attrs =
                    {
                        {.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT3, .offset = 0},
                        {.location = NT_ATTR_TEXCOORD0, .format = NT_FORMAT_FLOAT2, .offset = 12},
                    },
                .attr_count = 2,
                .stride = 20,
            },
        .depth_test = true,
        .depth_write = true,
        .depth_func = NT_DEPTH_LEQUAL,
        .label = "textured_quad_pipeline",
    });

    /* Destroy shaders after pipeline creation (pipeline owns copies) */
    nt_gfx_destroy_shader(vs);
    nt_gfx_destroy_shader(fs);

    /* Create vertex buffer */
    s_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = s_quad_vertices,
        .size = sizeof(s_quad_vertices),
        .label = "textured_quad_vbo",
    });

    /* Create index buffer */
    s_ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = s_quad_indices,
        .size = sizeof(s_quad_indices),
        .label = "textured_quad_ibo",
    });

    /* Create texture: 4x4 checkerboard with NEAREST filtering for crisp pixels */
    s_texture = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_checker_4x4,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
        .label = "checkerboard",
    });

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_gfx_destroy_texture(s_texture);
    nt_gfx_destroy_pipeline(s_pipeline);
    nt_gfx_destroy_buffer(s_vbo);
    nt_gfx_destroy_buffer(s_ibo);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
