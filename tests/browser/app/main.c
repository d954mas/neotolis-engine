/* Browser smoke-test app -- the MINIMAL standalone web target the Playwright specs drive.
 * Renders exactly two always-visible surfaces (a Cyrillic input field + a rich-text block with one
 * clickable <link>) and exposes the window.__nt hooks. Extracted from examples/ui_showcase. */

// #region includes
#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "font/nt_font.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material/nt_program_ref.h"
#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_mesh_format.h" /* in-code mesh blob for the instanced VAO probe */
#include "nt_pack_format.h" /* NT_ASSET_* resource-type enum */
#include "render/nt_render_defs.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "time/nt_time.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_input.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_rich_text.h"
#include "ui/nt_ui_scale.h"
#include "window/nt_window.h"

#include "app/nt_app.h"

#include "ui_showcase_assets.h" /* reuse the showcase pack's generated asset ids */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h> /* EM_JS / EMSCRIPTEN_KEEPALIVE for the window.__nt hooks */
#endif

#include "clay.h"
// #endregion

// #region layers + reference resolution
#define LAYER_IMG 3
#define LAYER_TEXT 4
#define UI_REF_W 1280.0F
#define UI_REF_H 800.0F
#define SCRATCH_ARENA_SIZE (1U << 20)
#define UI_MAX_ELEMENTS 512U
// #endregion

// #region state
/* The Cyrillic field edits this game-owned buffer in place (input.spec.ts reads it). Prefilled so the
 * spec's select-all-then-type replaces a known value. */
static struct {
    char cyrillic[64];
    float time;           /* rich-text effect clock (game-owned, accumulated from dt) */
    uint32_t link_clicks; /* bumped on a quest-link click */
} s_state = {.cyrillic = "Привет, мир"};

static nt_ui_context_t *s_ctx;
static uint8_t s_ui_arena[1U << 20];

static nt_hash32_t s_pack_id;
static nt_resource_t s_atlas_handle, s_atlas_tex_handle;
static nt_resource_t s_font_resource;
static nt_resource_t s_rich_font_resource[4];

static nt_material_t s_sprite_material, s_text_material;
static nt_program_ref_t s_sprite_program, s_text_program;

/* Links each pair once both its stages are ready. The programs are ours:
 * materials only borrow the handles, and context loss forces a relink. */
static void link_programs(void) {
    if (nt_program_ref_update(&s_sprite_program)) {
        nt_material_set_program(s_sprite_material, s_sprite_program.program);
    }
    if (nt_program_ref_update(&s_text_program)) {
        nt_material_set_program(s_text_material, s_text_program.program);
    }
}
static nt_font_t s_font;
static nt_font_t s_rich_font[4]; /* R/B/I/BI faces for the rich block */

static nt_buffer_t s_frame_ubo;

static bool s_atlas_bound;
static bool s_font_bound;
static bool s_rich_font_bound;

static uint32_t s_id_input_cyrillic; /* nt_ui_id, resolved once */

static const nt_ui_label_style_t s_caption = {.font_id = 0, .font_size = 16, .color = {165.0F, 170.0F, 182.0F, 255.0F}};
static const nt_ui_label_style_t s_body = {.font_id = 0, .font_size = 22, .color = {225.0F, 228.0F, 235.0F, 255.0F}};
static nt_ui_input_style_t s_input_style; /* filled at init (defaults + visible bg/border) */
// #endregion

// #region mesh probe (instanced VAO path)
/* Browser probe: two quads use a baked VBO/IBO and instance re-pointing at a
 * nonzero offset, matching the mesh renderer's WebGL2 VAO path. */
static nt_shader_t s_mesh_vs, s_mesh_fs;
static nt_program_t s_mesh_program;
static nt_pipeline_t s_mesh_pipeline;
static nt_vertex_input_t s_mesh_vi;
static nt_buffer_t s_mesh_instance_buf;
static uint32_t s_mesh_handle;
static uint32_t s_mesh_index_count, s_mesh_vertex_count;

static const char *s_mesh_vs_src = "precision mediump float;\n"
                                   "layout(location = 0) in vec3 a_position;\n"
                                   "layout(location = 4) in vec2 i_offset;\n"
                                   "void main() { gl_Position = vec4(a_position.xy * 0.1 + i_offset, 0.0, 1.0); }\n";
static const char *s_mesh_fs_src = "precision mediump float;\n"
                                   "out vec4 frag_color;\n"
                                   "void main() { frag_color = vec4(0.0, 1.0, 0.0, 1.0); }\n";

/* False when the context is (still) lost mid-restore: the caller retries on a
 * later frame; partial handles are {0}-safe for mesh_probe_destroy. */
static bool mesh_probe_create(void) {
    /* Unit quad, one float3 position stream, uint16 indices. */
    static const float verts[12] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    static const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + sizeof(verts) + sizeof(indices)];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = NT_INDEX_UINT16;
    hdr->vertex_count = 4;
    hdr->index_count = 6;
    hdr->vertex_data_size = sizeof(verts);
    hdr->index_data_size = sizeof(indices);
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->name_hash = nt_hash32_str("position").value;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;
    memcpy(blob + sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc), verts, sizeof(verts));
    memcpy(blob + sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + sizeof(verts), indices, sizeof(indices));

    s_mesh_handle = nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob));
    if (s_mesh_handle == 0) {
        return false; /* lost context during activation */
    }
    const nt_gfx_mesh_info_t *info = nt_gfx_get_mesh_info((nt_mesh_t){s_mesh_handle});
    s_mesh_index_count = info->index_count;
    s_mesh_vertex_count = info->vertex_count;

    s_mesh_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_mesh_vs_src, .label = "mesh_probe_vs"});
    s_mesh_fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_mesh_fs_src, .label = "mesh_probe_fs"});
    s_mesh_program = nt_gfx_make_program(s_mesh_vs, s_mesh_fs);
    s_mesh_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = s_mesh_program, .label = "mesh_probe_pipeline"});
    s_mesh_vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}},
        .instance_layout = {.attr_count = 1, .stride = 8, .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 2}}},
        .vertex_buffer = info->vbo,
        .index_buffer = info->ibo,
        .label = "mesh_probe_vi",
    });
    s_mesh_instance_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64, .label = "mesh_probe_inst"});
    return s_mesh_pipeline.id != 0 && s_mesh_vi.id != 0 && s_mesh_instance_buf.id != 0;
}

static void mesh_probe_destroy(void) {
    nt_gfx_destroy_vertex_input(s_mesh_vi); /* also stale-safe after a context loss */
    s_mesh_vi = NT_VERTEX_INPUT_INVALID;
    if (s_mesh_handle != 0) {
        nt_gfx_deactivate_mesh(s_mesh_handle);
        s_mesh_handle = 0;
    }
    nt_gfx_destroy_buffer(s_mesh_instance_buf);
    s_mesh_instance_buf = (nt_buffer_t){0};
    nt_gfx_destroy_pipeline(s_mesh_pipeline);
    s_mesh_pipeline = (nt_pipeline_t){0};
    nt_gfx_destroy_program(s_mesh_program);
    s_mesh_program = NT_PROGRAM_INVALID;
    nt_gfx_destroy_shader(s_mesh_fs);
    nt_gfx_destroy_shader(s_mesh_vs);
    s_mesh_fs = (nt_shader_t){0};
    s_mesh_vs = (nt_shader_t){0};
}

static void mesh_probe_draw(void) {
    if (s_mesh_vi.id == 0 || !nt_gfx_program_ready(s_mesh_program)) {
        return;
    }
    /* Instance data at byte offset 8: proves the nonzero-offset re-pointing
     * the mesh renderer's ring allocator relies on. Two stacked quads. */
    float inst[6] = {0.0F, 0.0F, 0.85F, -0.95F, 0.85F, -0.82F};
    nt_gfx_update_buffer(s_mesh_instance_buf, 0, inst, sizeof(inst));
    nt_gfx_bind_pipeline(s_mesh_pipeline);
    nt_gfx_bind_vertex_input(s_mesh_vi);
    nt_gfx_bind_instance_buffer(s_mesh_instance_buf, 8);
    nt_gfx_draw_indexed_instanced(0, s_mesh_index_count, s_mesh_vertex_count, 2);
}
// #endregion

// #region resource binding
static void try_bind_resources(void) {
    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        const uint32_t white = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS__WHITE.value);
        NT_ASSERT(white != NT_ATLAS_INVALID_REGION);
        nt_ui_set_atlas_white_region(s_ctx, s_atlas_handle, white);
        s_atlas_bound = true;
    }
    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ctx, 0U, s_font);
        s_font_bound = true;
    }
    if (!s_rich_font_bound && nt_resource_is_ready(s_rich_font_resource[0]) && nt_resource_is_ready(s_rich_font_resource[1]) && nt_resource_is_ready(s_rich_font_resource[2]) &&
        nt_resource_is_ready(s_rich_font_resource[3])) {
        for (uint32_t i = 0; i < 4U; i++) {
            nt_font_add(s_rich_font[i], s_rich_font_resource[i]);
        }
        s_rich_font_bound = true;
    }
}
// #endregion

// #region web test hooks (window.__nt) -- web build only; the native build compiles without them.
#if defined(__EMSCRIPTEN__)
static int s_nt_ready;                 /* set after the first rendered frame */
static unsigned int s_nt_drawn_frames; /* frames that reached the draw path */
static float s_nt_field_css_x;         /* Cyrillic field center, CSS px (canvas-relative) */
static float s_nt_field_css_y;
static float s_nt_field_css_w;
static float s_nt_field_css_h;
static int s_nt_field_visible; /* the field was laid out this frame */
static int s_nt_hidden_probe;
static nt_ui_input_style_t s_nt_hidden_input_style;
static nt_ui_label_style_t s_nt_hidden_caption;

/* The quest <link>'s block-local rect (from res.first_link_rect), stashed at the rich_text call and
 * mapped to CSS px against the block bbox each frame so rich.spec.ts clicks its exact center. */
static float s_nt_rich_link_local[4]; /* {x, y, w, h} block-local px */
static int s_nt_rich_link_present;
static float s_nt_rich_link_css_x;
static float s_nt_rich_link_css_y;
static float s_nt_rich_link_css_w;
static float s_nt_rich_link_css_h;

EMSCRIPTEN_KEEPALIVE int nt_test_ready(void) { return s_nt_ready; }
/* A fresh draw is required in addition to the test's pixel comparison. */
EMSCRIPTEN_KEEPALIVE unsigned int nt_test_drawn_frames(void) { return s_nt_drawn_frames; }
/* Both game programs linked and assigned -- false through the whole window
 * between the loss and the relink. */
EMSCRIPTEN_KEEPALIVE int nt_test_programs_ready(void) { return (nt_gfx_program_ready(s_sprite_program.program) && nt_gfx_program_ready(s_text_program.program)) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE const char *nt_test_input_buffer(void) { return s_state.cyrillic; }
EMSCRIPTEN_KEEPALIVE unsigned int nt_test_walk_text_cmd_count(void) { return nt_ui_get_last_walk_text_command_count(s_ctx); }
EMSCRIPTEN_KEEPALIVE float nt_test_field_css_x(void) { return s_nt_field_css_x; }
EMSCRIPTEN_KEEPALIVE float nt_test_field_css_y(void) { return s_nt_field_css_y; }
EMSCRIPTEN_KEEPALIVE float nt_test_field_css_w(void) { return s_nt_field_css_w; }
EMSCRIPTEN_KEEPALIVE float nt_test_field_css_h(void) { return s_nt_field_css_h; }
EMSCRIPTEN_KEEPALIVE int nt_test_field_visible(void) { return s_nt_field_visible; }
EMSCRIPTEN_KEEPALIVE void nt_test_hide_probe(int mode) {
    NT_ASSERT(mode >= 0 && mode <= 2);
    s_nt_hidden_probe = mode;
}
EMSCRIPTEN_KEEPALIVE int nt_test_rich_link_present(void) { return s_nt_rich_link_present; }
EMSCRIPTEN_KEEPALIVE float nt_test_rich_link_css_x(void) { return s_nt_rich_link_css_x; }
EMSCRIPTEN_KEEPALIVE float nt_test_rich_link_css_y(void) { return s_nt_rich_link_css_y; }
EMSCRIPTEN_KEEPALIVE float nt_test_rich_link_css_w(void) { return s_nt_rich_link_css_w; }
EMSCRIPTEN_KEEPALIVE float nt_test_rich_link_css_h(void) { return s_nt_rich_link_css_h; }
EMSCRIPTEN_KEEPALIVE unsigned int nt_test_rich_link_clicks(void) { return s_state.link_clicks; }

/* clang-format off */
/* Force-include UTF8ToString for input_buffer(). The C getters are KEEPALIVE (live as bare _name in the
 * glue scope); bare access + EM_JS_DEPS is Closure-safe, no exported-methods list. */
EM_JS_DEPS(nt_browser_app_test_hooks, "$UTF8ToString")

/* Quoted keys keep the Playwright-facing API stable under Closure. */
EM_JS(void, nt_test_install_hooks, (void), {
    window['__nt'] = {
        get 'ready'() { return _nt_test_ready() !== 0; },
        'input_buffer': function() { return UTF8ToString(_nt_test_input_buffer()); },
        'walk_text_cmd_count': function() { return _nt_test_walk_text_cmd_count() >>> 0; },
        'drawn_frames': function() { return _nt_test_drawn_frames() >>> 0; },
        'programs_ready': function() { return _nt_test_programs_ready() !== 0; },
        'hide_probe': function(mode) { _nt_test_hide_probe(mode); },
        'field_visible': function() { return _nt_test_field_visible() !== 0; },
        'field_css': function() {
            return { 'x': _nt_test_field_css_x(), 'y': _nt_test_field_css_y(), 'w': _nt_test_field_css_w(), 'h': _nt_test_field_css_h() };
        },
        'rich_link_css': function() {
            return { 'present': _nt_test_rich_link_present() !== 0, 'x': _nt_test_rich_link_css_x(), 'y': _nt_test_rich_link_css_y(), 'w': _nt_test_rich_link_css_w(), 'h': _nt_test_rich_link_css_h() };
        },
        'rich_link_clicks': function() { return _nt_test_rich_link_clicks() >>> 0; }
    };
})
/* clang-format on */
#endif /* __EMSCRIPTEN__ */
// #endregion

// #region rich block (one clickable link)
static inline uint32_t quest_link_id(void) {
    return nt_hash32_str("quest").value;
}

static nt_ui_rich_style_t rich_base_style(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    for (uint32_t i = 0; i < 4U; i++) {
        base.font_id[i] = s_rich_font[i];
    }
    base.color_abgr = 0xFFEBE4E1U; /* near-white body (0xAABBGGRR) */
    base.default_atlas = nt_atlas_ref(s_atlas_handle, 0U);
    return base;
}

/* Minimal code-first block: some text + one clickable quest <link>. res carries first_link_rect so the
 * hooks recover the link's block-local rect. */
static void render_rich(nt_ui_context_t *ctx) {
    nt_ui_rich_style_t base = rich_base_style();
    nt_ui_rich_begin(ctx, &base);
    nt_ui_rich_push_bold(ctx);
    nt_ui_rich_text_n(ctx, "DRAKE", 5U);
    nt_ui_rich_pop(ctx);
    nt_ui_rich_text_n(ctx, " quest -- accept it: ", 21U);
    nt_ui_rich_push_color(ctx, 0xFFE0A040U); /* link blue */
    nt_ui_rich_link(ctx, quest_link_id());
    nt_ui_rich_text_n(ctx, "[Accept quest]", 14U);
    nt_ui_rich_link(ctx, 0U); /* end link -- set/clear, NOT a push */
    nt_ui_rich_pop(ctx);      /* color */
    nt_ui_rich_end(ctx);

    nt_ui_rich_result_t res = {0};
    nt_ui_rich_text(ctx, nt_ui_id("app/rich"), NT_UI_DATA_LAYER(LAYER_TEXT), &base, 560.0F, NT_RICH_ALIGN_LEFT, s_state.time, &res);

#if defined(__EMSCRIPTEN__)
    /* Stash the quest link's block-local rect for the per-frame CSS map (rich.spec.ts clicks it). */
    s_nt_rich_link_present = (res.first_link != 0U) ? 1 : 0;
    s_nt_rich_link_local[0] = res.first_link_rect[0];
    s_nt_rich_link_local[1] = res.first_link_rect[1];
    s_nt_rich_link_local[2] = res.first_link_rect[2];
    s_nt_rich_link_local[3] = res.first_link_rect[3];
#endif
    if (res.clicked_link != 0U) {
        s_state.link_clicks++;
    }
}
// #endregion

// #region frame
/* A second loss during recovery skips rendering and retries next frame.
 * Destroy calls are stale-/{0}-safe, so the step may repeat. */
static bool s_gpu_restore_pending;

static bool gpu_restore_step(void) {
    nt_gfx_destroy_buffer(s_frame_ubo);
    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });
    bool ok = s_frame_ubo.id != 0;
    ok = (nt_sprite_renderer_restore_gpu() == NT_OK) && ok;
    ok = (nt_text_renderer_restore_gpu() == NT_OK) && ok;
    /* The probe's mesh and vertex input died with the context. */
    mesh_probe_destroy();
    ok = mesh_probe_create() && ok;
    return ok;
}

static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    nt_mem_scratch_reset();

    nt_resource_step();
    nt_material_step();
    link_programs();

    s_state.time += g_nt_app.dt;

    try_bind_resources();

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);

    nt_ui_scale_desc_t scale_desc = {.ref_w = UI_REF_W, .ref_h = UI_REF_H, .mode = NT_UI_SCALE_EXPAND};
    nt_ui_scale_t scale = nt_ui_compute_scale(&scale_desc, fb_w, fb_h);
    nt_ui_scale_ortho_t ortho = nt_ui_scale_ortho(&scale);

    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_mat4_identity(view_m);
    glm_ortho(ortho.left, ortho.right, ortho.bottom, ortho.top, -1.0F, 1.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.resolution[0] = fb_w;
    uniforms.resolution[1] = fb_h;
    uniforms.resolution[2] = (fb_w > 0.0F) ? (1.0F / fb_w) : 0.0F;
    uniforms.resolution[3] = (fb_h > 0.0F) ? (1.0F / fb_h) : 0.0F;
    uniforms.near_far[0] = -1.0F;
    uniforms.near_far[1] = 1.0F;

    nt_gfx_begin_frame();
    if (g_nt_gfx.context_restored) {
        /* One-shot per restored event; the GPU recreation below may retry. */
        nt_resource_invalidate(NT_ASSET_TEXTURE);
        nt_resource_invalidate(NT_ASSET_FONT);
        /* Materials retain their handles; rendering waits for relinking on a later frame.
         * Renderer reset and program destruction may run in either order without draws. */
        nt_program_ref_drop(&s_sprite_program);
        nt_program_ref_drop(&s_text_program);
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        s_atlas_bound = false;
        /* Font sources survive restore; adding them again asserts on duplicates.
         * nt_font_step rebuilds textures after the font assets reactivate. */
        s_gpu_restore_pending = true;
    }
    if (s_gpu_restore_pending) {
        s_gpu_restore_pending = !gpu_restore_step();
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .clear_color = {0.07F, 0.08F, 0.10F, 1.0F},
        .clear_depth = 1.0F,
    });

    nt_font_step();

    const nt_material_info_t *sprite_info = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    /* A pending restore means renderer buffers may be missing; submitting
     * sprites then asserts by contract, so rendering waits it out. */
    const bool can_render = !s_gpu_restore_pending && s_atlas_bound && s_font_bound && s_rich_font_bound && sprite_info && nt_gfx_program_ready(sprite_info->program) && text_info &&
                            nt_gfx_program_ready(text_info->program);

    if (can_render) {
        nt_gfx_update_buffer(s_frame_ubo, 0, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        nt_ui_begin(s_ctx, scale.logical_w, scale.logical_h, g_nt_app.dt, &g_nt_input.pointers[0], 1);
        nt_ui_set_viewport(s_ctx, nt_ui_viewport_from_scale(&scale));

        const nt_ui_label_style_t *caption = &s_caption;
        const nt_ui_input_style_t *input_style = &s_input_style;
#if defined(__EMSCRIPTEN__)
        if (s_nt_hidden_probe == 1) {
            input_style = &s_nt_hidden_input_style;
        } else if (s_nt_hidden_probe == 2) {
            caption = &s_nt_hidden_caption;
        }
#endif

        CLAY({.id = CLAY_ID("root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .padding = CLAY_PADDING_ALL(24),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 16,
                         .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
              .backgroundColor = {18.0F, 20.0F, 26.0F, 255.0F}}) {
            /* Surface 1: Cyrillic input field. */
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Cyrillic (multi-byte UTF-8)", caption);
            (void)nt_ui_input_text(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_input_cyrillic, s_state.cyrillic, sizeof s_state.cyrillic, &(nt_ui_input_props_t){0}, input_style,
                                   &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(40)}}}, true, NULL);

            /* Surface 2: rich-text block with one clickable link. */
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Rich text (one clickable <link>):", &s_body);
            render_rich(s_ctx);
        }

        nt_ui_end(s_ctx);

        nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(s_ctx, &target);

#if defined(__EMSCRIPTEN__)
        /* Map the Cyrillic field's logical bbox -> canvas CSS px so Playwright clicks the real widget.
         * UI logical -> framebuffer px (offset + ui*scale) -> CSS px (/dpr). Clay bbox + fb are both
         * Y-down, matching nt_input_web's css->fb map (no Y-flip). */
        {
            const nt_ui_bbox_t fb = nt_ui_get_bbox(s_ctx, s_id_input_cyrillic);
            s_nt_field_visible = fb.found ? 1 : 0;
            if (fb.found) {
                const float dpr = (g_nt_window.dpr > 0.0F) ? g_nt_window.dpr : 1.0F;
                const float cx_fb = scale.offset_x + (fb.x + fb.width * 0.5F) * scale.scale_x;
                const float cy_fb = scale.offset_y + (fb.y + fb.height * 0.5F) * scale.scale_y;
                s_nt_field_css_x = cx_fb / dpr;
                s_nt_field_css_y = cy_fb / dpr;
                s_nt_field_css_w = fb.width * scale.scale_x / dpr;
                s_nt_field_css_h = fb.height * scale.scale_y / dpr;
            }
            s_nt_ready = 1;
        }
        /* Map the quest link's block-local rect -> canvas CSS px (same UI->fb->CSS map, via the block bbox). */
        {
            const nt_ui_bbox_t rb = nt_ui_get_bbox(s_ctx, nt_ui_id("app/rich"));
            if (rb.found && s_nt_rich_link_present) {
                const float dpr = (g_nt_window.dpr > 0.0F) ? g_nt_window.dpr : 1.0F;
                const float lcx_fb = scale.offset_x + (rb.x + s_nt_rich_link_local[0] + s_nt_rich_link_local[2] * 0.5F) * scale.scale_x;
                const float lcy_fb = scale.offset_y + (rb.y + s_nt_rich_link_local[1] + s_nt_rich_link_local[3] * 0.5F) * scale.scale_y;
                s_nt_rich_link_css_x = lcx_fb / dpr;
                s_nt_rich_link_css_y = lcy_fb / dpr;
                s_nt_rich_link_css_w = (s_nt_rich_link_local[2] * scale.scale_x) / dpr;
                s_nt_rich_link_css_h = (s_nt_rich_link_local[3] * scale.scale_y) / dpr;
            }
        }
#endif /* __EMSCRIPTEN__ */

        nt_text_renderer_flush();
        mesh_probe_draw(); /* on top of the UI so the spec can pixel-probe it */
#ifdef __EMSCRIPTEN__
        /* Count frames that actually submitted geometry: reaching the draw path
         * proves nothing if every renderer skipped. */
        if (nt_gfx_get_frame_draw_calls() > 0U) {
            s_nt_drawn_frames++;
        }
#endif
    }

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_window_swap_buffers();
}
// #endregion

// #region main + init
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    nt_engine_config_t config = {0};
    config.app_name = "browser_smoke";
    config.version = 1;
    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }
    nt_log_info("browser_smoke: %s build (%s)", nt_engine_build_string(), nt_engine_preset_string());

    g_nt_window.width = 1280;
    g_nt_window.height = 800;
    nt_window_init();
    nt_input_init();

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx_desc);
    nt_gfx_register_global_block("Globals", 0);

    nt_http_init();
    nt_fs_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_mem_scratch_init(SCRATCH_ARENA_SIZE);

    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);
    nt_atlas_init();

    nt_material_init(&(nt_material_desc_t){.max_materials = 2});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 5}); /* base + 4 rich faces */

    nt_sprite_renderer_desc_t sr_desc = nt_sprite_renderer_desc_defaults();
    nt_sprite_renderer_init(&sr_desc);
    nt_text_renderer_init();
    const bool probe_ok = mesh_probe_create();
    NT_ASSERT(probe_ok && "mesh probe creation failed at startup"); /* cold start: the context is alive */
    (void)probe_ok;

    nt_ui_module_init();
    nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
    ui_desc.max_elements = UI_MAX_ELEMENTS;
    s_ctx = nt_ui_create_context(s_ui_arena, sizeof s_ui_arena, &ui_desc);
    NT_ASSERT(s_ctx != NULL && "browser_smoke: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    /* Reuse the showcase pack (same generated asset ids); the CMake copies ui_showcase.ntpack here. */
    s_pack_id = nt_hash32_str("ui_showcase");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/ui_showcase/ui_showcase.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/ui_showcase.ntpack");
#endif

    s_sprite_program.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_program.fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_program.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_program.fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_UI_SHOWCASE_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_UI_SHOWCASE_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_UI_SHOWCASE_FONT, NT_ASSET_FONT);
    s_rich_font_resource[0] = nt_resource_request(ASSET_FONT_UI_SHOWCASE_FONT_RICH_R, NT_ASSET_FONT);
    s_rich_font_resource[1] = nt_resource_request(ASSET_FONT_UI_SHOWCASE_FONT_RICH_B, NT_ASSET_FONT);
    s_rich_font_resource[2] = nt_resource_request(ASSET_FONT_UI_SHOWCASE_FONT_RICH_I, NT_ASSET_FONT);
    s_rich_font_resource[3] = nt_resource_request(ASSET_FONT_UI_SHOWCASE_FONT_RICH_BI, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend = nt_blend_alpha_premultiplied(),
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "browser_smoke_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .blend = nt_blend_alpha_premultiplied(),
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .params[0] = {.name = "u_alpha_cutoff", .value = {NT_TEXT_ALPHA_CUTOFF_DEFAULT}},
        .param_count = 1,
        .label = "browser_smoke_text",
    });

    nt_ui_set_sprite_material(s_ctx, s_sprite_material);
    nt_ui_set_text_material(s_ctx, s_text_material);

    s_font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
        .measure_cache_size = 256,
    });
    for (uint32_t i = 0; i < 4U; i++) {
        s_rich_font[i] = nt_font_create(&(nt_font_create_desc_t){
            .curve_texture_width = 1024,
            .curve_texture_height = 512,
            .band_texture_height = 256,
            .band_count = 8,
            .measure_cache_size = 256,
        });
    }

    /* Flat-color field with a visible idle/focused bg + border (defaults give a valid caret). */
    s_input_style = nt_ui_input_style_defaults();
    s_input_style.text.font_id = 0;
    s_input_style.text.font_size = 22.0F;
    s_input_style.text.color = (Clay_Color){225.0F, 228.0F, 235.0F, 255.0F};
    s_input_style.placeholder.font_id = 0;
    s_input_style.placeholder.font_size = 22.0F;
    s_input_style.placeholder.color = (Clay_Color){120.0F, 126.0F, 138.0F, 255.0F};
    s_input_style.pad_x = 10.0F;
    s_input_style.pad_y = 8.0F;
    s_input_style.skin[NT_UI_INPUT_IDLE].bg_color = 0xFF303438U;
    s_input_style.skin[NT_UI_INPUT_IDLE].border_color = 0xFF505860U;
    s_input_style.skin[NT_UI_INPUT_FOCUSED].bg_color = 0xFF383E44U;
    s_input_style.skin[NT_UI_INPUT_FOCUSED].border_color = 0xFFF0A040U;
    s_input_style.border_width = 1.0F;

    s_id_input_cyrillic = nt_ui_id("app/input_cyrillic");

    nt_resource_set_activate_time_budget(0);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

#if defined(__EMSCRIPTEN__)
    /* Transparency removes one probe's pixels without changing layout or readiness. */
    s_nt_hidden_input_style = s_input_style;
    for (uint32_t i = 0; i < NT_UI_INPUT_STATE_COUNT; i++) {
        s_nt_hidden_input_style.skin[i].bg_color &= 0x00FFFFFFU;
        s_nt_hidden_input_style.skin[i].border_color &= 0x00FFFFFFU;
    }
    s_nt_hidden_caption = s_caption;
    s_nt_hidden_caption.color.a = 0.0F;
    nt_test_install_hooks(); /* window.__nt smoke-test surface */
#endif

    nt_log_info("browser_smoke: starting");
    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_ui_destroy_context(s_ctx);
    nt_ui_module_shutdown();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_font_destroy(s_font);
    for (uint32_t i = 0; i < 4U; i++) {
        nt_font_destroy(s_rich_font[i]);
    }
    nt_font_shutdown();
    nt_material_destroy(s_sprite_material);
    nt_material_destroy(s_text_material);
    nt_program_ref_drop(&s_sprite_program);
    nt_program_ref_drop(&s_text_program);
    nt_material_shutdown();
    nt_mem_scratch_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    mesh_probe_destroy();
    nt_gfx_destroy_buffer(s_frame_ubo);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
// #endregion
