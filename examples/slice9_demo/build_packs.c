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

/* Generate a 64x64 RGBA panel image with distinct colors per zone.
 * Border = 8 px on each side. Colors:
 *   corners:  red-ish   (200, 60, 60, 255)
 *   h-edges:  green-ish (60, 180, 60, 255)
 *   v-edges:  blue-ish  (60, 60, 200, 255)
 *   center:   gray      (160, 160, 160, 255) */
#define PANEL_W 64
#define PANEL_H 64
#define BORDER 8

static uint8_t s_panel_rgba[PANEL_W * PANEL_H * 4];

static void generate_panel_image(void) {
    for (int y = 0; y < PANEL_H; y++) {
        for (int x = 0; x < PANEL_W; x++) {
            const bool in_left = x < BORDER;
            const bool in_right = x >= (PANEL_W - BORDER);
            const bool in_top = y < BORDER;
            const bool in_bottom = y >= (PANEL_H - BORDER);

            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if ((in_left || in_right) && (in_top || in_bottom)) {
                /* corner */
                r = 200;
                g = 60;
                b = 60;
            } else if (in_top || in_bottom) {
                /* horizontal edge */
                r = 60;
                g = 180;
                b = 60;
            } else if (in_left || in_right) {
                /* vertical edge */
                r = 60;
                g = 60;
                b = 200;
            } else {
                /* center */
                r = 160;
                g = 160;
                b = 160;
            }

            uint8_t *px = &s_panel_rgba[(size_t)((y * PANEL_W) + x) * 4];
            px[0] = r;
            px[1] = g;
            px[2] = b;
            px[3] = 255;
        }
    }
}

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

    // #region atlas: slice9 panel + white pixel
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

    /* Slice9 panel: 64x64 with 8px borders */
    generate_panel_image();
    nt_atlas_sprite_opts_t panel_opts = nt_atlas_sprite_opts_defaults();
    panel_opts.name = "panel_bg";
    panel_opts.slice9_left = BORDER;
    panel_opts.slice9_right = BORDER;
    panel_opts.slice9_top = BORDER;
    panel_opts.slice9_bottom = BORDER;
    nt_builder_atlas_add_raw(ctx, s_panel_rgba, PANEL_W, PANEL_H, &panel_opts);
    (void)printf("  Atlas region 'panel_bg': %dx%d slice9(%d,%d,%d,%d)\n", PANEL_W, PANEL_H, BORDER, BORDER, BORDER, BORDER);

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
