/* Build ui_stateful demo pack:
 *   ui_stateful_demo.ntpack -- atlas (checkbox box + check, radio ring + dot,
 *   toggle track off/on slice9 + thumb, white pixel) + shaders + font.
 * Usage: build_ui_stateful_demo_packs <pack_dir>
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

#define HEADER_DIR "examples/ui_stateful_demo/generated"
/* Reuse the ui_theme_demo font (mirrors ui_buttons_demo's FONT_PATH precedent --
 * the .ttf is not duplicated into this demo's raw/). */
#define FONT_PATH "examples/ui_theme_demo/raw/font.ttf"

/* Toggle track is a 128x48 rounded pill. Horizontal slice9 (24px) keeps the
 * rounded end-caps fixed; vertical slice9 (16px) keeps the rounded top/bottom.
 * Builder asserts L+R < width (48<128) and T+B < height (32<48). */
#define TRACK_BORDER_X 24
#define TRACK_BORDER_Y 16

/* bar_track / bar_fill_smooth are 192x32 pills. 8px slice9 stays small enough that the fill
 * never end-cap-overlaps even in the 18px-tall slider track or a narrow low-value fill (8+8 <
 * both target dims). */
#define BAR_BORDER 8

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_ui_stateful_demo_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build UI Stateful Demo Pack -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(HEADER_DIR);

    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);
    MKDIR(cache_dir);

    // #region pack: ui_stateful_demo.ntpack
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "ui_stateful_demo.ntpack"));
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start ui_stateful_demo.ntpack\n");
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

    // #region atlas: checkbox / radio / toggle sprites + white pixel
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

    nt_builder_begin_atlas(ctx, "ui_stateful_demo_atlas", &atlas_opts);

    /* Checkbox: box (off) + checkmark overlay. */
    nt_atlas_sprite_opts_t opts = nt_atlas_sprite_opts_defaults();
    opts.name = "box_off";
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/box_off.png", &opts);

    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "checkmark";
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/checkmark.png", &opts);

    /* Radio: ring + dot overlay. */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "radio_ring";
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/radio_ring.png", &opts);

    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "radio_dot";
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/radio_dot.png", &opts);

    /* Toggle track: slice9 pill so the rounded ends stay fixed when stretched.
     * OFF (grey) + ON (green) so the track recolors by value. */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "track_off";
    opts.slice9_left = TRACK_BORDER_X;
    opts.slice9_right = TRACK_BORDER_X;
    opts.slice9_top = TRACK_BORDER_Y;
    opts.slice9_bottom = TRACK_BORDER_Y;
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/track_off.png", &opts);

    opts.name = "track_on";
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/track_on.png", &opts);

    /* Toggle thumb (circle); slides left<->right via the value_t offset DELTA. */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "thumb";
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/thumb.png", &opts);

    /* Progress-bar art: a recessed track + two fills demoing STRETCH vs CROP. bar_track +
     * bar_fill_smooth are slice9 pills (rounded ends stay fixed when stretched); bar_fill_shaped
     * (diagonal candy stripes) gets NO slice9 — CROP reveals it undistorted. */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "bar_track";
    opts.slice9_left = BAR_BORDER;
    opts.slice9_right = BAR_BORDER;
    opts.slice9_top = BAR_BORDER;
    opts.slice9_bottom = BAR_BORDER;
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/bar_track.png", &opts);

    opts.name = "bar_fill_smooth";
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/bar_fill_smooth.png", &opts);

    /* Shaped CROP fill: no slice9 (a partial reveal is geometrically incompatible with slice9). */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "bar_fill_shaped";
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/bar_fill_shaped.png", &opts);

    /* Scrollbar pieces: a recessed slot track + a light capsule thumb, both 32x32 with 8px slice9
     * (BAR_BORDER) so the rounded caps stay fixed whether the bar is stretched tall (vertical) or
     * wide (horizontal). */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "scroll_track";
    opts.slice9_left = BAR_BORDER;
    opts.slice9_right = BAR_BORDER;
    opts.slice9_top = BAR_BORDER;
    opts.slice9_bottom = BAR_BORDER;
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/scroll_track.png", &opts);

    opts.name = "bar_thumb";
    nt_builder_atlas_add(ctx, "examples/ui_stateful_demo/raw/bar_thumb.png", &opts);

    (void)printf("  Atlas: box/check + ring/dot + track_off/on (s9 %dx%d) + thumb + bar track/fills + scroll track/thumb (s9 %d)\n", TRACK_BORDER_X, TRACK_BORDER_Y, BAR_BORDER);

    /* White pixel for UI rects (panel backgrounds, inspector sidebar). */
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
                            .resource_name = "ui_stateful_demo/font",
                        });
    (void)printf("  Font (ASCII) added: ui_stateful_demo/font\n");
    // #endregion

    // #region finish + codegen
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "ui_stateful_demo.ntpack failed: %d\n", r);
        return 1;
    }

    char base_hdr[512];
    (void)snprintf(base_hdr, sizeof(base_hdr), "%s/ui_stateful_demo.h", HEADER_DIR);
    const char *headers[] = {base_hdr};
    char combined[512];
    (void)snprintf(combined, sizeof(combined), "%s/ui_stateful_demo_assets.h", HEADER_DIR);
    nt_builder_merge_headers(headers, 1, combined);
    (void)printf("Generated: %s\n", combined);
    // #endregion

    /* Pack size summary. */
    (void)printf("\n=== Pack Size Summary ===\n");
    FILE *f = fopen(pack_path(out_dir, "ui_stateful_demo.ntpack"), "rb");
    if (f) {
        (void)fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        (void)fclose(f);
        (void)printf("  ui_stateful_demo.ntpack    %8.1f KB\n", (double)sz / 1024.0);
    }

    (void)printf("\n=== Done ===\n");
    return 0;
}
