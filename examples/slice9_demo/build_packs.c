/* Build slice9 demo pack:
 *   slice9_demo.ntpack -- atlas with slice9 panel region + white pixel + shaders + font.
 * Usage: build_slice9_demo_packs <pack_dir>
 * Run from the project root directory. */

/* clang-format off */
#include "nt_builder.h"
/* clang-format on */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define HEADER_DIR "examples/slice9_demo/generated"
#define FONT_PATH "examples/ui_theme_demo/raw/font.ttf"

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

/* Kenney UI assets (CC0). Panels: 100x100, corners ~10px. Button: 384x128, corners ~16px. */
#define PANEL_BORDER 10
#define BUTTON_BORDER 16

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_slice9_demo_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build Slice9 Demo Pack -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(HEADER_DIR);

    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);
    MKDIR(cache_dir);

    // #region pack: slice9_demo.ntpack
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "slice9_demo.ntpack"));
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start slice9_demo.ntpack\n");
        return 1;
    }

    nt_builder_set_header_dir(ctx, HEADER_DIR);
    nt_builder_set_cache_dir(ctx, cache_dir);
    nt_builder_set_threads_auto(ctx);
    // #endregion

    // #region shaders (sprite for UI rects + Slug for text)
    nt_builder_add_shader(ctx, "assets/shaders/sprite.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/sprite.frag", NT_BUILD_SHADER_FRAGMENT);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.frag", NT_BUILD_SHADER_FRAGMENT);
    (void)printf("  Shaders added: 4 (sprite + slug_text)\n");
    // #endregion

    // #region atlas: Kenney panels + button + white pixel
    nt_atlas_opts_t atlas_opts = nt_atlas_opts_defaults();
    atlas_opts.shape = NT_ATLAS_SHAPE_RECT;
    atlas_opts.allow_transform = false;
    atlas_opts.pixels_per_unit = 1.0F;
    atlas_opts.padding = 2;
    atlas_opts.margin = 2;
    atlas_opts.extrude = 1;
    atlas_opts.premultiplied = true;
    atlas_opts.compress = NULL;
    atlas_opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    atlas_opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    atlas_opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    atlas_opts.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    atlas_opts.gen_mipmaps = false;

    nt_builder_begin_atlas(ctx, "slice9_demo_atlas", &atlas_opts);

    /* Kenney panels (100x100, 10px corners) */
    nt_atlas_sprite_opts_t panel_opts = nt_atlas_sprite_opts_defaults();
    panel_opts.slice9_left = PANEL_BORDER;
    panel_opts.slice9_right = PANEL_BORDER;
    panel_opts.slice9_top = PANEL_BORDER;
    panel_opts.slice9_bottom = PANEL_BORDER;

    panel_opts.name = "panel_beige";
    nt_builder_atlas_add(ctx, "examples/slice9_demo/raw/panel_beige.png", &panel_opts);

    panel_opts.name = "panel_blue";
    nt_builder_atlas_add(ctx, "examples/slice9_demo/raw/panel_blue.png", &panel_opts);

    panel_opts.name = "panel_brown";
    nt_builder_atlas_add(ctx, "examples/slice9_demo/raw/panel_brown.png", &panel_opts);

    /* Kenney button (384x128, 16px corners) */
    nt_atlas_sprite_opts_t btn_opts = nt_atlas_sprite_opts_defaults();
    btn_opts.name = "button_blue";
    btn_opts.slice9_left = BUTTON_BORDER;
    btn_opts.slice9_right = BUTTON_BORDER;
    btn_opts.slice9_top = BUTTON_BORDER;
    btn_opts.slice9_bottom = BUTTON_BORDER;
    nt_builder_atlas_add(ctx, "examples/slice9_demo/raw/button_blue_depth.png", &btn_opts);

    btn_opts.name = "button_green";
    nt_builder_atlas_add(ctx, "examples/slice9_demo/raw/button_green_depth.png", &btn_opts);

    (void)printf("  Atlas: 3 panels (100x100 s9:%d) + 2 buttons (384x128 s9:%d)\n", PANEL_BORDER, BUTTON_BORDER);

    /* White pixel for UI rects */
    static const uint8_t white_pixel[4] = {255, 255, 255, 255};
    nt_atlas_sprite_opts_t white_opts = nt_atlas_sprite_opts_defaults();
    white_opts.name = "_white";
    nt_builder_atlas_add_raw(ctx, white_pixel, 1, 1, &white_opts);
    (void)printf("  Atlas region '_white': 1x1\n");

    nt_builder_end_atlas(ctx);
    // #endregion

    // #region font: ASCII Latin only (reuse ui_theme_demo font)
    nt_builder_add_font(ctx, FONT_PATH,
                        &(nt_font_opts_t){
                            .charset = NT_CHARSET_ASCII,
                            .resource_name = "slice9_demo/font",
                        });
    (void)printf("  Font (ASCII) added: slice9_demo/font\n");
    // #endregion

    // #region finish + codegen
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "slice9_demo.ntpack failed: %d\n", r);
        return 1;
    }

    char base_hdr[512];
    (void)snprintf(base_hdr, sizeof(base_hdr), "%s/slice9_demo.h", HEADER_DIR);
    const char *headers[] = {base_hdr};
    char combined[512];
    (void)snprintf(combined, sizeof(combined), "%s/slice9_demo_assets.h", HEADER_DIR);
    nt_builder_merge_headers(headers, 1, combined);
    (void)printf("Generated: %s\n", combined);
    // #endregion

    /* Pack size summary. */
    (void)printf("\n=== Pack Size Summary ===\n");
    FILE *f = fopen(pack_path(out_dir, "slice9_demo.ntpack"), "rb");
    if (f) {
        (void)fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        (void)fclose(f);
        (void)printf("  slice9_demo.ntpack    %8.1f KB\n", (double)sz / 1024.0);
    }

    (void)printf("\n=== Done ===\n");
    return 0;
}
