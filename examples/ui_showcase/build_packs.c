/* Build ui_showcase vitrine pack:
 *   ui_showcase.ntpack -- atlas (white pixel + checkbox/radio/toggle art + slider/progress
 *   bar art + scrollbar art + Kenney slice9 panels + buttons) + shaders + font.
 * Folds the art of the superseded ui_buttons/ui_stateful/ui_theme demos + slice9 panels into
 * one pack so the vitrine drives every widget from a single source of truth.
 * Usage: build_ui_showcase_packs <pack_dir>
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

#define HEADER_DIR "examples/ui_showcase/generated"
#define FONT_PATH "examples/ui_showcase/raw/font.ttf"

/* Toggle track is a rounded pill; slice9 keeps the rounded end-caps fixed when stretched. */
#define TRACK_BORDER_X 24
#define TRACK_BORDER_Y 16

/* bar_track / bar_fill_smooth pills: 8px slice9 stays small enough to never end-cap-overlap. */
#define BAR_BORDER 8

/* Kenney panels (100x100, ~10px corners) + buttons (384x128, ~16px corners). */
#define PANEL_BORDER 10
#define BUTTON_BORDER 16

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_ui_showcase_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build UI Showcase Pack -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(HEADER_DIR);

    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);
    MKDIR(cache_dir);

    // #region pack: ui_showcase.ntpack
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "ui_showcase.ntpack"));
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start ui_showcase.ntpack\n");
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

    // #region atlas: widget art + slice9 panels + white pixel
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

    nt_builder_begin_atlas(ctx, "ui_showcase_atlas", &atlas_opts);

    /* Checkbox: box (off) + checkmark overlay. */
    nt_atlas_sprite_opts_t opts = nt_atlas_sprite_opts_defaults();
    opts.name = "box_off";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/box_off.png", &opts);

    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "checkmark";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/checkmark.png", &opts);

    /* Radio: ring + dot overlay. */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "radio_ring";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/radio_ring.png", &opts);

    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "radio_dot";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/radio_dot.png", &opts);

    /* Toggle track (slice9 pill): OFF grey + ON green so the track recolors by value. */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "track_off";
    opts.slice9_left = TRACK_BORDER_X;
    opts.slice9_right = TRACK_BORDER_X;
    opts.slice9_top = TRACK_BORDER_Y;
    opts.slice9_bottom = TRACK_BORDER_Y;
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/track_off.png", &opts);

    opts.name = "track_on";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/track_on.png", &opts);

    /* Toggle thumb / slider thumb (circle). */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "thumb";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/thumb.png", &opts);

    /* Slider/progress bar art: recessed track + smooth slice9 fill. */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "bar_track";
    opts.slice9_left = BAR_BORDER;
    opts.slice9_right = BAR_BORDER;
    opts.slice9_top = BAR_BORDER;
    opts.slice9_bottom = BAR_BORDER;
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/bar_track.png", &opts);

    opts.name = "bar_fill_smooth";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/bar_fill_smooth.png", &opts);

    /* Scrollbar: recessed slot track + light capsule thumb (8px slice9). */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "scroll_track";
    opts.slice9_left = BAR_BORDER;
    opts.slice9_right = BAR_BORDER;
    opts.slice9_top = BAR_BORDER;
    opts.slice9_bottom = BAR_BORDER;
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/scroll_track.png", &opts);

    opts.name = "bar_thumb";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/bar_thumb.png", &opts);

    /* Kenney slice9 panels (100x100, 10px corners) for the Images & Slice9 tab. */
    nt_atlas_sprite_opts_t panel_opts = nt_atlas_sprite_opts_defaults();
    panel_opts.slice9_left = PANEL_BORDER;
    panel_opts.slice9_right = PANEL_BORDER;
    panel_opts.slice9_top = PANEL_BORDER;
    panel_opts.slice9_bottom = PANEL_BORDER;

    panel_opts.name = "panel_beige";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/panel_beige.png", &panel_opts);

    panel_opts.name = "panel_blue";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/panel_blue.png", &panel_opts);

    panel_opts.name = "panel_brown";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/panel_brown.png", &panel_opts);

    /* Kenney buttons (384x128, 16px corners) for the button sibling-variation tabs. */
    nt_atlas_sprite_opts_t btn_opts = nt_atlas_sprite_opts_defaults();
    btn_opts.slice9_left = BUTTON_BORDER;
    btn_opts.slice9_right = BUTTON_BORDER;
    btn_opts.slice9_top = BUTTON_BORDER;
    btn_opts.slice9_bottom = BUTTON_BORDER;

    btn_opts.name = "button_blue";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/button_blue_depth.png", &btn_opts);

    btn_opts.name = "button_green";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/button_green_depth.png", &btn_opts);

    (void)printf("  Atlas: widgets + 3 panels (s9:%d) + 2 buttons (s9:%d)\n", PANEL_BORDER, BUTTON_BORDER);

    /* White pixel for UI rects (panel backgrounds, tab list). */
    static const uint8_t white_pixel[4] = {255, 255, 255, 255};
    nt_atlas_sprite_opts_t white_opts = nt_atlas_sprite_opts_defaults();
    white_opts.name = "_white";
    nt_builder_atlas_add_raw(ctx, white_pixel, 1, 1, &white_opts);
    (void)printf("  Atlas region '_white': 1x1\n");

    nt_builder_end_atlas(ctx);
    // #endregion

    // #region font: ASCII Latin only
    nt_builder_add_font(ctx, FONT_PATH,
                        &(nt_font_opts_t){
                            .charset = NT_CHARSET_ASCII,
                            .resource_name = "ui_showcase/font",
                        });
    (void)printf("  Font (ASCII) added: ui_showcase/font\n");
    // #endregion

    // #region finish + codegen
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "ui_showcase.ntpack failed: %d\n", r);
        return 1;
    }

    char base_hdr[512];
    (void)snprintf(base_hdr, sizeof(base_hdr), "%s/ui_showcase.h", HEADER_DIR);
    const char *headers[] = {base_hdr};
    char combined[512];
    (void)snprintf(combined, sizeof(combined), "%s/ui_showcase_assets.h", HEADER_DIR);
    nt_builder_merge_headers(headers, 1, combined);
    (void)printf("Generated: %s\n", combined);
    // #endregion

    /* Pack size summary. */
    (void)printf("\n=== Pack Size Summary ===\n");
    FILE *f = fopen(pack_path(out_dir, "ui_showcase.ntpack"), "rb");
    if (f) {
        (void)fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        (void)fclose(f);
        (void)printf("  ui_showcase.ntpack    %8.1f KB\n", (double)sz / 1024.0);
    }

    (void)printf("\n=== Done ===\n");
    return 0;
}
