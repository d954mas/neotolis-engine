/* Build ui_buttons demo pack:
 *   ui_buttons_demo.ntpack -- atlas (button_blue slice9 + white pixel) + shaders + font.
 * Usage: build_ui_buttons_demo_packs <pack_dir>
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

#define HEADER_DIR "examples/ui_buttons_demo/generated"
/* Reuse the ui_theme_demo font (mirrors slice9_demo's FONT_PATH precedent --
 * the .ttf is not duplicated into this demo's raw/). */
#define FONT_PATH "examples/ui_theme_demo/raw/font.ttf"

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

/* Kenney button slice9 corners (384x128, 16px). */
#define BUTTON_BORDER 16

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_ui_buttons_demo_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build UI Buttons Demo Pack -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(HEADER_DIR);

    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);
    MKDIR(cache_dir);

    // #region pack: ui_buttons_demo.ntpack
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "ui_buttons_demo.ntpack"));
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start ui_buttons_demo.ntpack\n");
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

    // #region atlas: button_blue (slice9) + white pixel
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

    nt_builder_begin_atlas(ctx, "ui_buttons_demo_atlas", &atlas_opts);

    /* Kenney buttons (384x128, 16px corners) -- TWO regions so the
     * VISUAL-SWAP variant in main.c can swap bg_region per state
     * (blue idle/disabled vs green hover/pressed). Mirrors the
     * slice9_demo pattern (lines 108-117 of its build_packs.c). */
    nt_atlas_sprite_opts_t btn_opts = nt_atlas_sprite_opts_defaults();
    btn_opts.name = "button_blue";
    btn_opts.slice9_left = BUTTON_BORDER;
    btn_opts.slice9_right = BUTTON_BORDER;
    btn_opts.slice9_top = BUTTON_BORDER;
    btn_opts.slice9_bottom = BUTTON_BORDER;
    nt_builder_atlas_add(ctx, "examples/ui_buttons_demo/raw/button_blue_depth.png", &btn_opts);

    btn_opts.name = "button_green";
    nt_builder_atlas_add(ctx, "examples/ui_buttons_demo/raw/button_green_depth.png", &btn_opts);

    (void)printf("  Atlas: 2 buttons (384x128 s9:%d)\n", BUTTON_BORDER);

    /* White pixel for UI rects + gold-tinted icon child. */
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
                            .resource_name = "ui_buttons_demo/font",
                        });
    (void)printf("  Font (ASCII) added: ui_buttons_demo/font\n");
    // #endregion

    // #region finish + codegen
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "ui_buttons_demo.ntpack failed: %d\n", r);
        return 1;
    }

    char base_hdr[512];
    (void)snprintf(base_hdr, sizeof(base_hdr), "%s/ui_buttons_demo.h", HEADER_DIR);
    const char *headers[] = {base_hdr};
    char combined[512];
    (void)snprintf(combined, sizeof(combined), "%s/ui_buttons_demo_assets.h", HEADER_DIR);
    nt_builder_merge_headers(headers, 1, combined);
    (void)printf("Generated: %s\n", combined);
    // #endregion

    /* Pack size summary. */
    (void)printf("\n=== Pack Size Summary ===\n");
    FILE *f = fopen(pack_path(out_dir, "ui_buttons_demo.ntpack"), "rb");
    if (f) {
        (void)fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        (void)fclose(f);
        (void)printf("  ui_buttons_demo.ntpack    %8.1f KB\n", (double)sz / 1024.0);
    }

    (void)printf("\n=== Done ===\n");
    return 0;
}
