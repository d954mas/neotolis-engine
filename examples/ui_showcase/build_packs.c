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

/* Rich-text family: DejaVu R/B/I/BI faces (real bold + oblique, full Cyrillic). The rich tab binds
 * these to variant slots so <b>/<i> select a real face instead of synth-shear. */
#define FONT_RICH_R_PATH "examples/ui_showcase/raw/font_dejavu_r.ttf"
#define FONT_RICH_B_PATH "examples/ui_showcase/raw/font_dejavu_b.ttf"
#define FONT_RICH_I_PATH "examples/ui_showcase/raw/font_dejavu_i.ttf"
#define FONT_RICH_BI_PATH "examples/ui_showcase/raw/font_dejavu_bi.ttf"

/* Demo-only Cyrillic block: the engine is codepoint-agnostic; the SHOWCASE bakes Latin +
 * Cyrillic so the Input tab's Cyrillic field renders real multi-byte UTF-8 glyphs, not tofu. */
#define CHARSET_CYRILLIC "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя"

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

    // #region shaders (sprite for UI rects + Slug for text + radial SDF widgets)
    nt_builder_add_shader(ctx, "assets/shaders/sprite.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/sprite.frag", NT_BUILD_SHADER_FRAGMENT);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.frag", NT_BUILD_SHADER_FRAGMENT);
    /* Radial: shared extended-layout VS (a_radial @ loc 4) + the flat SDF FS
     * (nt_ui_radial) + the textured reveal FS (nt_ui_radial_image). */
    nt_builder_add_shader(ctx, "assets/shaders/sprite_radial.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/radial.frag", NT_BUILD_SHADER_FRAGMENT);
    nt_builder_add_shader(ctx, "assets/shaders/radial_image.frag", NT_BUILD_SHADER_FRAGMENT);
    (void)printf("  Shaders added: 7 (sprite + slug_text + radial vs/fs + radial_image fs)\n");
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

    /* Shaped diagonal-stripe fill for the CROP-clip progress variant (revealed, not stretched;
     * slice9 is ignored in CROP, so it keeps the bar pill borders only for the STRETCH siblings). */
    opts.name = "bar_fill_shaped";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/bar_fill_shaped.png", &opts);

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

    btn_opts.name = "button_red";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/button_red_depth.png", &btn_opts);

    /* Real icon art (Kenney CC0 bunny, 32x32) for the icon button variant. */
    nt_atlas_sprite_opts_t icon_opts = nt_atlas_sprite_opts_defaults();
    icon_opts.name = "icon_bunny";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/icon_bunny.png", &icon_opts);

    /* Rich-text inline icons (named regions for <img=name/> by-name resolve). Two 16x16 fully-opaque
     * solid-color icons: "heart" (red) + "gold" (amber). Solid color so nothing trims; the rich demo
     * tints them via <color> on the standard u8 sprite path. The atlas-wide padding/extrude (2/1)
     * gives each a bleed border so the scaled inline icon never edge-bleeds its neighbour. */
    enum { ICON_DIM = 16 };
    static uint8_t icon_heart[ICON_DIM * ICON_DIM * 4];
    static uint8_t icon_gold[ICON_DIM * ICON_DIM * 4];
    for (int i = 0; i < ICON_DIM * ICON_DIM; ++i) {
        uint8_t *h = &icon_heart[(size_t)i * 4U];
        h[0] = 220;
        h[1] = 40;
        h[2] = 60;
        h[3] = 255;
        uint8_t *g = &icon_gold[(size_t)i * 4U];
        g[0] = 250;
        g[1] = 200;
        g[2] = 60;
        g[3] = 255;
    }
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "heart";
    nt_builder_atlas_add_raw(ctx, icon_heart, ICON_DIM, ICON_DIM, &opts);
    opts.name = "gold";
    nt_builder_atlas_add_raw(ctx, icon_gold, ICON_DIM, ICON_DIM, &opts);

    /* Tristate MIXED indicator: a centered horizontal dash (white, tintable) on a transparent
     * 28x28 field. slice9 borders keep the source UNTRIMMED so the dash stays CENTERED with
     * vertical breathing room when drawn at the checkbox overlay size (trim would strip the
     * transparent margins and stretch the bar to fill the box). */
    enum { DASH_DIM = 28, DASH_BAR_X0 = 4, DASH_BAR_X1 = 24, DASH_BAR_Y0 = 12, DASH_BAR_Y1 = 16 };
    static uint8_t mixed_dash[DASH_DIM * DASH_DIM * 4];
    for (int y = 0; y < DASH_DIM; ++y) {
        for (int x = 0; x < DASH_DIM; ++x) {
            uint8_t *px = &mixed_dash[(((size_t)y * DASH_DIM) + (size_t)x) * 4U];
            bool bar = (x >= DASH_BAR_X0 && x < DASH_BAR_X1) && (y >= DASH_BAR_Y0 && y < DASH_BAR_Y1);
            px[0] = 255;
            px[1] = 255;
            px[2] = 255;
            px[3] = bar ? 255 : 0;
        }
    }
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "mixed_dash";
    opts.slice9_left = 1;
    opts.slice9_right = 1;
    opts.slice9_top = 1;
    opts.slice9_bottom = 1;
    nt_builder_atlas_add_raw(ctx, mixed_dash, DASH_DIM, DASH_DIM, &opts);

    (void)printf("  Atlas: widgets + 3 panels (s9:%d) + 3 buttons (s9:%d) + icon + heart/gold inline icons\n", PANEL_BORDER, BUTTON_BORDER);

    /* White pixel for UI rects (panel backgrounds, tab list). */
    static const uint8_t white_pixel[4] = {255, 255, 255, 255};
    nt_atlas_sprite_opts_t white_opts = nt_atlas_sprite_opts_defaults();
    white_opts.name = "_white";
    nt_builder_atlas_add_raw(ctx, white_pixel, 1, 1, &white_opts);
    (void)printf("  Atlas region '_white': 1x1\n");

    /* App-widget icon affordances (white, tintable): dropdown chevron, submenu arrow, tooltip caret.
     * Prebaked PNGs in raw/; builder premultiplies like every other sprite. */
    opts = nt_atlas_sprite_opts_defaults();
    opts.name = "chevron_down";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/chevron_down.png", &opts);

    opts.name = "arrow_right";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/arrow_right.png", &opts);

    opts.name = "caret";
    nt_builder_atlas_add(ctx, "examples/ui_showcase/raw/caret.png", &opts);
    (void)printf("  Atlas icons (tintable): chevron_down 16x16, arrow_right 12x12, caret 14x10\n");

    nt_builder_end_atlas(ctx);
    // #endregion

    // #region atlas: radial-image art (single full-bleed sprite -> UV spans [0,1])
    /* The reveal now centers on ANY rectangular region (region-local UV via a_uvrect), so a
     * packed sub-region works too — the showcase proves that on the shared atlas's bunny. This
     * DEDICATED full-bleed single-sprite atlas (no padding/margin/extrude, non-POT, RECT,
     * fully-OPAQUE so the trimmer strips nothing) is kept for the A/B [0,1]-UV reference cell. */
    nt_atlas_opts_t radial_opts = nt_atlas_opts_defaults();
    radial_opts.shape = NT_ATLAS_SHAPE_RECT;
    radial_opts.allow_transform = false;
    radial_opts.padding = 0;
    radial_opts.margin = 0;
    radial_opts.extrude = 0;
    radial_opts.power_of_two = false; /* tight page == sprite dims -> UV [0,1] */
    radial_opts.premultiplied = true;
    radial_opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    radial_opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    radial_opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    radial_opts.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    radial_opts.gen_mipmaps = false;

    nt_builder_begin_atlas(ctx, "ui_showcase_radial_art", &radial_opts);

    /* Procedural 128x128 opaque test image: a 2-axis color gradient with a darker grid
     * overlay so the four reveal modes (desaturate/dim/hide/tint) and the swept boundary
     * are all clearly legible. Fully opaque (alpha 255) so nothing is trimmed. */
    enum { RADIAL_ART_DIM = 128 };
    static uint8_t radial_art[RADIAL_ART_DIM * RADIAL_ART_DIM * 4];
    for (int y = 0; y < RADIAL_ART_DIM; ++y) {
        for (int x = 0; x < RADIAL_ART_DIM; ++x) {
            uint8_t *px = &radial_art[(((size_t)y * RADIAL_ART_DIM) + (size_t)x) * 4U];
            uint8_t rr = (uint8_t)((x * 255) / (RADIAL_ART_DIM - 1));
            uint8_t gg = (uint8_t)((y * 255) / (RADIAL_ART_DIM - 1));
            uint8_t bb = (uint8_t)(255 - ((x * 255) / (RADIAL_ART_DIM - 1)));
            /* 16px grid lines darken the swatch so reveal composites read clearly. */
            bool grid = ((x % 16) == 0) || ((y % 16) == 0);
            px[0] = grid ? (uint8_t)(rr / 3) : rr;
            px[1] = grid ? (uint8_t)(gg / 3) : gg;
            px[2] = grid ? (uint8_t)(bb / 3) : bb;
            px[3] = 255;
        }
    }
    nt_atlas_sprite_opts_t radial_sprite = nt_atlas_sprite_opts_defaults();
    radial_sprite.name = "radial_art";
    radial_sprite.shape = NT_ATLAS_SPRITE_SHAPE_RECT;
    radial_sprite.allow_rotate = NT_ATLAS_SPRITE_ROTATE_NO;
    nt_builder_atlas_add_raw(ctx, radial_art, RADIAL_ART_DIM, RADIAL_ART_DIM, &radial_sprite);
    (void)printf("  Atlas 'ui_showcase_radial_art': radial_art %dx%d (full-bleed, UV [0,1])\n", RADIAL_ART_DIM, RADIAL_ART_DIM);

    nt_builder_end_atlas(ctx);
    // #endregion

    // #region font: ASCII Latin + Cyrillic (demo-only)
    nt_builder_add_font(ctx, FONT_PATH,
                        &(nt_font_opts_t){
                            .charset = NT_CHARSET_ASCII CHARSET_CYRILLIC,
                            .resource_name = "ui_showcase/font",
                        });
    (void)printf("  Font (ASCII + Cyrillic) added: ui_showcase/font\n");

    /* Rich-text family: four real faces, same small ASCII+Cyrillic subset, one per variant slot. */
    nt_builder_add_font(ctx, FONT_RICH_R_PATH, &(nt_font_opts_t){.charset = NT_CHARSET_ASCII CHARSET_CYRILLIC, .resource_name = "ui_showcase/font_rich_r"});
    nt_builder_add_font(ctx, FONT_RICH_B_PATH, &(nt_font_opts_t){.charset = NT_CHARSET_ASCII CHARSET_CYRILLIC, .resource_name = "ui_showcase/font_rich_b"});
    nt_builder_add_font(ctx, FONT_RICH_I_PATH, &(nt_font_opts_t){.charset = NT_CHARSET_ASCII CHARSET_CYRILLIC, .resource_name = "ui_showcase/font_rich_i"});
    nt_builder_add_font(ctx, FONT_RICH_BI_PATH, &(nt_font_opts_t){.charset = NT_CHARSET_ASCII CHARSET_CYRILLIC, .resource_name = "ui_showcase/font_rich_bi"});
    (void)printf("  Rich-text family (DejaVu R/B/I/BI) added: ui_showcase/font_rich_{r,b,i,bi}\n");
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
