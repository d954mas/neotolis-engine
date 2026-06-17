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

// #region procedural icon rasterizer
/* Coverage-AA triangle icons baked white so widgets tint them per-theme. The atlas is premultiplied,
 * so coverage IS the premultiplied white value (R=G=B=A=coverage). 4x supersample edge for soft AA. */
#define ICON_MAX_DIM 32
#define ICON_SS 4

/* dir: 0=down (chevron), 1=right (submenu arrow), 2=up (tooltip caret). Triangle inset 1px so the
 * extrude/AA edge never clips. Returns coverage [0,1] for sample (sx,sy) in [0,1] unit space. */
static float icon_tri_cover(int dir, float ux, float uy) {
    /* Map to a 0..1 isosceles triangle pointing along `dir`; inside test via the apex/base edges. */
    float a;
    float b; /* a = across-axis position [-1,1] from center, b = along-axis progress [0,1] base->apex */
    switch (dir) {
    case 1: /* right: apex at x=1, base at x=0 */
        a = uy * 2.0F - 1.0F;
        b = ux;
        break;
    case 2: /* up: apex at y=0, base at y=1 */
        a = ux * 2.0F - 1.0F;
        b = 1.0F - uy;
        break;
    default: /* down: apex at y=1, base at y=0 */
        a = ux * 2.0F - 1.0F;
        b = uy;
        break;
    }
    /* Inside when |a| <= (1 - b): full width at the base, narrowing to a point at the apex. */
    const float half = 1.0F - b;
    return (b >= 0.0F && b <= 1.0F && (a < 0.0F ? -a : a) <= half) ? 1.0F : 0.0F;
}

/* Rasterize a white coverage-AA triangle into rgba[w*h*4] (premultiplied: rgb == a == coverage). */
static void icon_raster_tri(uint8_t *rgba, int w, int h, int dir) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc = 0.0F;
            for (int sy = 0; sy < ICON_SS; ++sy) {
                for (int sx = 0; sx < ICON_SS; ++sx) {
                    const float ux = ((float)x + ((float)sx + 0.5F) / (float)ICON_SS) / (float)w;
                    const float uy = ((float)y + ((float)sy + 0.5F) / (float)ICON_SS) / (float)h;
                    acc += icon_tri_cover(dir, ux, uy);
                }
            }
            const float cov = acc / (float)(ICON_SS * ICON_SS);
            const uint8_t v = (uint8_t)((cov * 255.0F) + 0.5F);
            uint8_t *px = &rgba[((size_t)y * (size_t)w + (size_t)x) * 4U];
            px[0] = v;
            px[1] = v;
            px[2] = v;
            px[3] = v; /* premultiplied white */
        }
    }
}

static void add_icon_tri(NtBuilderContext *ctx, const char *name, int w, int h, int dir) {
    static uint8_t buf[ICON_MAX_DIM * ICON_MAX_DIM * 4];
    icon_raster_tri(buf, w, h, dir);
    nt_atlas_sprite_opts_t o = nt_atlas_sprite_opts_defaults();
    o.name = name;
    nt_builder_atlas_add_raw(ctx, buf, (uint32_t)w, (uint32_t)h, &o);
}
// #endregion

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

    (void)printf("  Atlas: widgets + 3 panels (s9:%d) + 3 buttons (s9:%d) + icon\n", PANEL_BORDER, BUTTON_BORDER);

    /* White pixel for UI rects (panel backgrounds, tab list). */
    static const uint8_t white_pixel[4] = {255, 255, 255, 255};
    nt_atlas_sprite_opts_t white_opts = nt_atlas_sprite_opts_defaults();
    white_opts.name = "_white";
    nt_builder_atlas_add_raw(ctx, white_pixel, 1, 1, &white_opts);
    (void)printf("  Atlas region '_white': 1x1\n");

    /* App-widget icon affordances (white, tintable): dropdown chevron, submenu arrow, tooltip caret. */
    add_icon_tri(ctx, "chevron_down", 16, 16, 0);
    add_icon_tri(ctx, "arrow_right", 12, 12, 1);
    add_icon_tri(ctx, "caret", 14, 10, 2);
    (void)printf("  Atlas icons (tintable): chevron_down 16x16, arrow_right 12x12, caret 14x10\n");

    nt_builder_end_atlas(ctx);
    // #endregion

    // #region font: ASCII Latin + Cyrillic (demo-only)
    nt_builder_add_font(ctx, FONT_PATH,
                        &(nt_font_opts_t){
                            .charset = NT_CHARSET_ASCII CHARSET_CYRILLIC,
                            .resource_name = "ui_showcase/font",
                        });
    (void)printf("  Font (ASCII + Cyrillic) added: ui_showcase/font\n");
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
