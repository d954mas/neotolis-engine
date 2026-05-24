/*
 * UI Theme Demo -- Neotolis Engine
 *
 * Model D theme hot-swap demo: game owns per-widget styles via two
 * static-const palettes (g_dark + g_light), each grouping six pointers to
 * nt_ui_label_style_t instances. A single g_current palette pointer is
 * flipped on T-key press; the next frame's labels render with the new
 * palette. Engine ships no nt_ui_set_theme -- the swap is a pure game-side
 * pointer write.
 *
 * Style fields exercised:
 *   h1            font_size + color (large, panel bg outlines text zone)
 *   body          font_size + color (used in 3-cell align row -- see below)
 *   caption       align (CENTER) for status line
 *   wrap          wrap_mode + line_height + align (CENTER multi-line)
 *   tracking      letter_tracking
 *
 * Position alignment is proven separately via Clay container childAlignment
 * (three side-by-side cells with the SAME label style -- only each cell's
 * .childAlignment.x differs). textAlignment (style.align) is proven by the
 * centered multi-line wrap paragraph.
 *
 * Keys:  T = swap palette (DARK <-> LIGHT) | D = toggle Clay debug overlay
 *
 * Build packs first:  build_ui_theme_demo_packs build/examples/ui_theme_demo
 */

// #region includes
#include "app/nt_app.h"
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
#include "render/nt_render_defs.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_fit.h"
#include "ui/nt_ui_internal.h" /* DEMO_DEBUG_LOG: inspect frozen_cmds */
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_scale.h"
#include "window/nt_window.h"

#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"

#include "ui_theme_demo_assets.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#include "clay.h"
// #endregion

// #region palette declarations
/* Per-widget styles -- one static-const per (role, palette) pair. Lives in
 * .rodata; any mutation crashes -- proves immutability for free. Color
 * channels are 0..255 floats per Clay's convention. */

static const nt_ui_label_style_t g_h1_dark = {
    .font_id = 0,
    .font_size = 44,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
};
static const nt_ui_label_style_t g_body_dark = {
    .font_id = 0,
    .font_size = 22,
    .color = {230.0F, 230.0F, 235.0F, 255.0F},
};
static const nt_ui_label_style_t g_caption_dark = {
    .font_id = 0,
    .font_size = 16,
    .color = {170.0F, 170.0F, 180.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};
static const nt_ui_label_style_t g_wrap_dark = {
    .font_id = 0,
    .font_size = 20,
    .color = {200.0F, 200.0F, 255.0F, 255.0F},
    /* line_height = 0 -> use font's natural metrics; avoids Clay container
     * underestimating panel height when half-leading shifts text past bb. */
    .wrap_mode = CLAY_TEXT_WRAP_WORDS,
    .align = CLAY_TEXT_ALIGN_CENTER,
};
static const nt_ui_label_style_t g_tracking_dark = {
    .font_id = 0,
    .font_size = 28,
    .color = {255.0F, 220.0F, 150.0F, 255.0F},
    .letter_tracking = 12,
    .wrap_mode = CLAY_TEXT_WRAP_WORDS,
};

static const nt_ui_label_style_t g_h1_light = {
    .font_id = 0,
    .font_size = 44,
    .color = {0.0F, 0.0F, 0.0F, 255.0F},
};
static const nt_ui_label_style_t g_body_light = {
    .font_id = 0,
    .font_size = 22,
    .color = {20.0F, 20.0F, 28.0F, 255.0F},
};
static const nt_ui_label_style_t g_caption_light = {
    .font_id = 0,
    .font_size = 16,
    .color = {80.0F, 80.0F, 90.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};
static const nt_ui_label_style_t g_wrap_light = {
    .font_id = 0,
    .font_size = 20,
    .color = {40.0F, 40.0F, 100.0F, 255.0F},
    .wrap_mode = CLAY_TEXT_WRAP_WORDS,
    .align = CLAY_TEXT_ALIGN_CENTER,
};
static const nt_ui_label_style_t g_tracking_light = {
    .font_id = 0,
    .font_size = 28,
    .color = {140.0F, 80.0F, 0.0F, 255.0F},
    .letter_tracking = 12,
    .wrap_mode = CLAY_TEXT_WRAP_WORDS,
};

typedef struct {
    const nt_ui_label_style_t *h1;
    const nt_ui_label_style_t *body;
    const nt_ui_label_style_t *caption;
    const nt_ui_label_style_t *wrap;
    const nt_ui_label_style_t *tracking;
    Clay_Color bg;
    Clay_Color panel; /* slightly contrasted bg -- shown under labels so text zones are visible */
    const char *name;
} ui_palette_t;

static const ui_palette_t g_dark = {
    .h1 = &g_h1_dark,
    .body = &g_body_dark,
    .caption = &g_caption_dark,
    .wrap = &g_wrap_dark,
    .tracking = &g_tracking_dark,
    .bg = {18.0F, 18.0F, 22.0F, 255.0F},
    .panel = {40.0F, 40.0F, 52.0F, 255.0F},
    .name = "DARK",
};
static const ui_palette_t g_light = {
    .h1 = &g_h1_light,
    .body = &g_body_light,
    .caption = &g_caption_light,
    .wrap = &g_wrap_light,
    .tracking = &g_tracking_light,
    .bg = {245.0F, 245.0F, 250.0F, 255.0F},
    .panel = {225.0F, 225.0F, 235.0F, 255.0F},
    .name = "LIGHT",
};

/* The pointer write IS the hot-swap. No engine API involved. */
static const ui_palette_t *g_current = &g_dark;
// #endregion

// #region engine state
#define UI_ARENA_SIZE ((size_t)2U * 1024U * 1024U) /* 2 MB -- plenty for 6 labels */
#define SCRATCH_ARENA_SIZE ((size_t)256U * 1024U)

static NT_UI_DECLARE_ARENA(s_ui_arena, UI_ARENA_SIZE);

static nt_ui_context_t *s_ctx;
static nt_buffer_t s_frame_ubo;

static nt_hash32_t s_pack_id;
static nt_resource_t s_atlas_handle;
static nt_resource_t s_atlas_tex_handle;
static nt_resource_t s_sprite_vs_handle;
static nt_resource_t s_sprite_fs_handle;
static nt_resource_t s_text_vs_handle;
static nt_resource_t s_text_fs_handle;
static nt_resource_t s_font_resource;

static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static nt_font_t s_font;

static bool s_atlas_bound;
static bool s_font_bound;
static bool s_debug_overlay;
static uint32_t s_white_region_idx;

/* Reference resolution -- everything in main.c speaks logical pixels.
 * 960x640 landscape (1.5:1) -- iterating on adaptive UI; verify behavior
 * at 960x640 (1:1 to ref), 1920x1080, 480x320, etc. */
#define UI_REF_W 960.0F
#define UI_REF_H 640.0F
static nt_ui_scale_mode_t s_scale_mode = NT_UI_SCALE_EXPAND;
static const char *const k_scale_mode_names[] = {"STRETCH", "LETTERBOX", "CROP", "EXPAND"};

/* Frame-by-frame debug log. Off by default; flip to 1 to dump scale + Clay
 * bbox values on every framebuffer-size change. Used by scripts/test_demo_sizes.ps1. */
#define DEMO_DEBUG_LOG 0
#if DEMO_DEBUG_LOG
static uint32_t s_frame_counter = 0U;
static int s_last_logged_fb_w = -1;
static int s_last_logged_fb_h = -1;
#endif

/* Auto-fit demo: cycle through short / medium / long text via key 2 to see
 * nt_ui_fit_width (title) and nt_ui_fit_box (paragraph) shrink as content
 * grows. Same font_size_max in both; same containers; only text length varies. */
static const char *const k_titles[] = {
    "Title",
    "Theme Hot-Swap Demo",
    "Theme Hot-Swap Demo with a Long Localized Title",
};
static const char *const k_paragraphs[] = {
    "Short.",
    "Medium paragraph that wraps onto two or three lines.",
    "A much longer paragraph that needs many lines to fit. The fit_box helper "
    "shrinks the font_size until the wrapped text fits inside the panel; cycle "
    "with key 2 to see the font drop further as text gets longer.",
};
static int s_demo_text_idx = 1;
// #endregion

// #region binding (run once resources are READY)
static void try_bind_resources(void) {
    if (s_atlas_bound && s_font_bound) {
        return;
    }

    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        uint32_t ridx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_THEME_DEMO_ATLAS__WHITE_PNG.value);
        NT_ASSERT(ridx != NT_ATLAS_INVALID_REGION && "ui_theme_demo: _white.png region missing in atlas");
        s_white_region_idx = ridx;
        nt_ui_set_atlas_white_region(s_ctx, s_atlas_handle, s_white_region_idx);
        s_atlas_bound = true;
        nt_log_info("ui_theme_demo: atlas bound, white_region_idx=%u", s_white_region_idx);
    }

    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ctx, 0U, s_font);
        s_font_bound = true;
        nt_log_info("ui_theme_demo: font bound at slot 0");
    }
}
// #endregion

// #region frame
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    nt_mem_scratch_reset();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    nt_resource_step();
    nt_material_step();

    /* Drift-3 corrected: the verified API is nt_input_key_is_pressed; the legacy
     * planning draft used the wrong name (no _is_ infix) which would link-fail. */
    if (nt_input_key_is_pressed(NT_KEY_T)) {
        g_current = (g_current == &g_dark) ? &g_light : &g_dark;
        (void)printf("ui_theme_demo: swapped to %s palette\n", g_current->name);
    }
    if (nt_input_key_is_pressed(NT_KEY_D)) {
        s_debug_overlay = !s_debug_overlay;
        (void)printf("ui_theme_demo: debug overlay %s\n", s_debug_overlay ? "ON" : "OFF");
    }
    if (nt_input_key_is_pressed(NT_KEY_1)) {
        s_scale_mode = (nt_ui_scale_mode_t)((s_scale_mode + 1) % 4);
        (void)printf("ui_theme_demo: scale mode -> %s\n", k_scale_mode_names[s_scale_mode]);
    }
    if (nt_input_key_is_pressed(NT_KEY_2)) {
        s_demo_text_idx = (s_demo_text_idx + 1) % 3;
        (void)printf("ui_theme_demo: demo text -> %d\n", s_demo_text_idx);
    }

    try_bind_resources();

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);

    /* Logical-space layout: everything below speaks in logical px. Ortho
     * projection maps the logical box to the (possibly letterboxed) physical
     * region; mouse gets the inverse transform via nt_ui_scale_apply_pointer.
     * Slug text stays sharp because it computes derivatives in screen space. */
    nt_ui_scale_desc_t scale_desc = {.ref_w = UI_REF_W, .ref_h = UI_REF_H, .mode = s_scale_mode};
    nt_ui_scale_t scale = nt_ui_compute_scale(&scale_desc, fb_w, fb_h);
    nt_ui_scale_ortho_t ortho = nt_ui_scale_ortho(&scale);

#if DEMO_DEBUG_LOG
    s_frame_counter++;
    const int fbw_i = (int)fb_w;
    const int fbh_i = (int)fb_h;
    const bool log_this_frame = (fbw_i != s_last_logged_fb_w || fbh_i != s_last_logged_fb_h);
    if (log_this_frame) {
        s_last_logged_fb_w = fbw_i;
        s_last_logged_fb_h = fbh_i;
        (void)fprintf(stderr, "[demo frame=%u] fb=%dx%d ref=%dx%d mode=%s\n", s_frame_counter, fbw_i, fbh_i, (int)UI_REF_W, (int)UI_REF_H, k_scale_mode_names[s_scale_mode]);
        (void)fprintf(stderr, "  scale.logical=%dx%d scale_x=%.4f scale_y=%.4f offset=(%.1f, %.1f) fb_in_scale=%dx%d\n", (int)scale.logical_w, (int)scale.logical_h, (double)scale.scale_x,
                      (double)scale.scale_y, (double)scale.offset_x, (double)scale.offset_y, (int)scale.fb_w, (int)scale.fb_h);
        (void)fprintf(stderr, "  ortho L=%.1f R=%.1f B=%.1f T=%.1f\n", (double)ortho.left, (double)ortho.right, (double)ortho.bottom, (double)ortho.top);
        (void)fflush(stderr);
    }
#endif

    /* Frame ortho VP -- logical-space, bottom-left origin. */
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
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        nt_resource_invalidate(NT_ASSET_TEXTURE);
        nt_resource_invalidate(NT_ASSET_FONT);
        nt_gfx_destroy_buffer(s_frame_ubo);
        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_frame_uniforms_t),
            .label = "frame_uniforms",
        });
        nt_sprite_renderer_restore_gpu();
        nt_text_renderer_restore_gpu();
        s_atlas_bound = false;
        s_font_bound = false;
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .clear_color = {g_current->bg.r / 255.0F, g_current->bg.g / 255.0F, g_current->bg.b / 255.0F, 1.0F},
        .clear_depth = 1.0F,
    });

    nt_font_step();

    /* All bindings ready? Declare + walk the UI. */
    const nt_material_info_t *sprite_info = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    const bool can_render = s_atlas_bound && s_font_bound && sprite_info && sprite_info->ready && text_info && text_info->ready;

    if (can_render) {
        nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        /* Physical mouse -> logical-space mouse for the UI's coord system. */
        const nt_pointer_t mouse_logical = nt_ui_scale_apply_pointer(&scale, g_nt_input.pointers[0]);
        nt_ui_set_debug_overlay(s_ctx, s_debug_overlay);
        nt_ui_begin(s_ctx, scale.logical_w, scale.logical_h, &mouse_logical);

        char status_text[160];
        (void)snprintf(status_text, sizeof status_text, "%s  %s  %dx%d  [T]palette [D]debug [1]scale [2]text", k_scale_mode_names[s_scale_mode], g_current->name, (int)scale.logical_w,
                       (int)scale.logical_h);

        /* Container width chosen as PERCENT-of-root so every panel scales
         * uniformly with the window. Padding values picked to give visible
         * panel bg around text without crowding.
         *
         * IMPORTANT: Clay's CLAY_SIZING_PERCENT is a percentage of the parent's
         * INNER content area (parent_w - parent_padding), not parent_w. Game-side
         * fit_* helpers need the actual inner panel width to match what Clay
         * will allocate for the text. */
        const float root_pad = 20.0F;
        const float panel_pct = 0.85F;
        const float panel_h_fixed = 140.0F;
        const float panel_pad = 14.0F;
        const float root_inner_w = scale.logical_w - (2.0F * root_pad);
        const float panel_w = root_inner_w * panel_pct;
        const float inner_w = panel_w - (2.0F * panel_pad);
        const float inner_h_box = panel_h_fixed - (2.0F * panel_pad);

        /* Pre-Clay auto-fit computations (helpers know nothing about Clay --
         * just font + container dims). Pass results to nt_ui_label_sized. */
        const uint16_t title_fit = nt_ui_fit_width(s_ctx, g_current->h1->font_id, k_titles[s_demo_text_idx], inner_w, 14U, g_current->h1->font_size, (float)g_current->h1->letter_tracking);
        const uint16_t box_fit = nt_ui_fit_box(s_ctx, g_current->wrap->font_id, k_paragraphs[s_demo_text_idx], inner_w, inner_h_box, 10U, g_current->wrap->font_size,
                                               (float)g_current->wrap->letter_tracking, g_current->wrap->line_height);

        char hint_title[80];
        char hint_box[80];
        (void)snprintf(hint_title, sizeof hint_title, "fit_width -> %u pt (max %u, idx %d)", title_fit, g_current->h1->font_size, s_demo_text_idx);
        (void)snprintf(hint_box, sizeof hint_box, "fit_box -> %u pt (max %u, idx %d)", box_fit, g_current->wrap->font_size, s_demo_text_idx);

        // #region clay declaration
        CLAY({.id = CLAY_ID("root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .padding = CLAY_PADDING_ALL(20),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 12,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = g_current->bg}) {

            /* Status caption (font 16, align CENTER). Short -- fits one line. */
            nt_ui_label(s_ctx, status_text, g_current->caption);

            /* === fit_width demo === */
            nt_ui_label(s_ctx, hint_title, g_current->caption);
            CLAY({.id = CLAY_ID("title-box"),
                  .layout = {.sizing = {CLAY_SIZING_PERCENT(panel_pct), CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL((uint16_t)panel_pad)},
                  .backgroundColor = g_current->panel}) {
                nt_ui_label_sized(s_ctx, k_titles[s_demo_text_idx], g_current->h1, title_fit);
            }

            /* === Position alignment row (childAlignment proof) === */
            CLAY({.id = CLAY_ID("align-row"), .layout = {.sizing = {CLAY_SIZING_PERCENT(panel_pct), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(48)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
                      .backgroundColor = g_current->panel}) {
                    nt_ui_label(s_ctx, "LEFT", g_current->body);
                }
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(48)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                      .backgroundColor = g_current->panel}) {
                    nt_ui_label(s_ctx, "CENTER", g_current->body);
                }
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(48)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER}},
                      .backgroundColor = g_current->panel}) {
                    nt_ui_label(s_ctx, "RIGHT", g_current->body);
                }
            }

            /* === fit_box demo === */
            nt_ui_label(s_ctx, hint_box, g_current->caption);
            CLAY({.id = CLAY_ID("fit-box"),
                  .layout = {.sizing = {CLAY_SIZING_PERCENT(panel_pct), CLAY_SIZING_FIXED((uint16_t)panel_h_fixed)}, .padding = CLAY_PADDING_ALL((uint16_t)panel_pad)},
                  .backgroundColor = g_current->panel}) {
                nt_ui_label_sized(s_ctx, k_paragraphs[s_demo_text_idx], g_current->wrap, box_fit);
            }

            /* === Letter-tracking demo === */
            CLAY({.id = CLAY_ID("tracking-box"),
                  .layout = {.sizing = {CLAY_SIZING_PERCENT(panel_pct), CLAY_SIZING_FIT(0)},
                             .padding = CLAY_PADDING_ALL((uint16_t)panel_pad),
                             .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = g_current->panel}) {
                nt_ui_label(s_ctx, "T R A C K I N G", g_current->tracking);
            }
        }
        // #endregion

        nt_ui_end(s_ctx);

#if DEMO_DEBUG_LOG
        if (log_this_frame) {
            /* dump first 6 commands' bbox (one frame after size change so layout
             * has settled). */
            const Clay_RenderCommandArray *cmds = &s_ctx->frozen_cmds;
            (void)fprintf(stderr, "  cmds=%d:\n", cmds->length);
            int dumped = 0;
            for (int32_t ci = 0; ci < cmds->length && dumped < 12; ++ci) {
                const Clay_RenderCommand *c = &cmds->internalArray[ci];
                const char *type = "?";
                switch (c->commandType) {
                case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
                    type = "RECT";
                    break;
                case CLAY_RENDER_COMMAND_TYPE_TEXT:
                    type = "TEXT";
                    break;
                case CLAY_RENDER_COMMAND_TYPE_BORDER:
                    type = "BRDR";
                    break;
                case CLAY_RENDER_COMMAND_TYPE_IMAGE:
                    type = "IMG";
                    break;
                case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
                    type = "SCRS";
                    break;
                case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                    type = "SCRE";
                    break;
                default:
                    continue;
                }
                (void)fprintf(stderr, "    [%d] %s bb=(%.1f, %.1f, %.1fx%.1f)\n", ci, type, (double)c->boundingBox.x, (double)c->boundingBox.y, (double)c->boundingBox.width, (double)c->boundingBox.height);
                dumped++;
            }
            (void)fflush(stderr);
        }
#endif

        nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(s_ctx, &target);
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
    config.app_name = "ui_theme_demo";
    config.version = 1;

    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }

    g_nt_window.width = 800;
    g_nt_window.height = 600;
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

    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 2});

    nt_sprite_renderer_desc_t sr_desc = nt_sprite_renderer_desc_defaults();
    nt_sprite_renderer_init(&sr_desc);
    nt_text_renderer_init();

    nt_ui_module_init();
    const nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
    s_ctx = nt_ui_create_context(s_ui_arena, sizeof s_ui_arena, &ui_desc);
    NT_ASSERT(s_ctx != NULL && "ui_theme_demo: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    s_pack_id = nt_hash32_str("ui_theme_demo");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/ui_theme_demo/ui_theme_demo.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/ui_theme_demo.ntpack");
#endif

    s_sprite_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_UI_THEME_DEMO_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_UI_THEME_DEMO_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_UI_THEME_DEMO_FONT, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_sprite_vs_handle,
        .fs = s_sprite_fs_handle,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_theme_demo_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_text_vs_handle,
        .fs = s_text_fs_handle,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_theme_demo_text",
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

    nt_resource_set_activate_time_budget(0);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_log_info("ui_theme_demo: starting in DARK palette; press T to toggle");

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_ui_destroy_context(s_ctx);
    nt_ui_module_shutdown();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    nt_material_destroy(s_sprite_material);
    nt_material_destroy(s_text_material);
    nt_material_shutdown();
    nt_mem_scratch_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_destroy_buffer(s_frame_ubo);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
// #endregion
