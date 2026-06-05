/* Build turkic_jam pack:
 *   turkic_jam.ntpack -- atlas (white pixel) + sprite/text shaders + font.
 * Usage: build_turkic_jam_packs <pack_dir>
 * Run from the project root directory. */

/* clang-format off */
#include "nt_builder.h"
/* clang-format on */

#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define HEADER_DIR "games/turkic-jam-2026/generated"
/* Reuse the ui_theme_demo font (same precedent as ui_buttons_demo). */
#define FONT_PATH "examples/ui_theme_demo/raw/font.ttf"

/* Kenney button slice9 corner (px). */
#define BUTTON_BORDER 16

/* ASCII + Cyrillic + Turkish letters used by i18n (EN/RU/TR). Roboto covers all. */
#define TJ_CHARSET                                                                                                                                                                                     \
    NT_CHARSET_ASCII                                                                                                                                                                                   \
    "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"                                                                                                                                                                \
    "абвгдеёжзийклмнопрстуфхцчшщъыьэюя"                                                                                                                                                                \
    "çÇğĞıİöÖşŞüÜ"

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_turkic_jam_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build Turkic Jam Pack -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(HEADER_DIR);

    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);
    MKDIR(cache_dir);

    // #region pack: turkic_jam.ntpack
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "turkic_jam.ntpack"));
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start turkic_jam.ntpack\n");
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

    // #region atlas: white pixel only (UI rects + tinted slice9 button bg)
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

    nt_builder_begin_atlas(ctx, "turkic_jam_atlas", &atlas_opts);

    static const uint8_t white_pixel[4] = {255, 255, 255, 255};
    nt_atlas_sprite_opts_t white_opts = nt_atlas_sprite_opts_defaults();
    white_opts.name = "_white";
    nt_builder_atlas_add_raw(ctx, white_pixel, 1, 1, &white_opts);
    (void)printf("  Atlas region '_white': 1x1\n");

    /* Kenney CC0 buttons (384x128, 16px corners), reused as slice9 bg art. */
    nt_atlas_sprite_opts_t btn_opts = nt_atlas_sprite_opts_defaults();
    btn_opts.slice9_left = BUTTON_BORDER;
    btn_opts.slice9_right = BUTTON_BORDER;
    btn_opts.slice9_top = BUTTON_BORDER;
    btn_opts.slice9_bottom = BUTTON_BORDER;
    btn_opts.name = "button_blue";
    nt_builder_atlas_add(ctx, "games/turkic-jam-2026/raw/ui/button_blue_depth.png", &btn_opts);
    btn_opts.name = "button_green";
    nt_builder_atlas_add(ctx, "games/turkic-jam-2026/raw/ui/button_green_depth.png", &btn_opts);
    btn_opts.name = "button_red";
    nt_builder_atlas_add(ctx, "games/turkic-jam-2026/raw/ui/button_red_depth.png", &btn_opts);
    (void)printf("  Atlas: 3 Kenney buttons (s9:%d)\n", BUTTON_BORDER);

    nt_builder_end_atlas(ctx);
    // #endregion

    // #region font: ASCII Latin only (reuse ui_theme_demo font)
    nt_builder_add_font(ctx, FONT_PATH,
                        &(nt_font_opts_t){
                            .charset = TJ_CHARSET,
                            .resource_name = "turkic_jam/font",
                        });
    (void)printf("  Font (ASCII+Cyrillic+Turkish) added: turkic_jam/font\n");
    // #endregion

    // #region finish + codegen
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "turkic_jam.ntpack failed: %d\n", r);
        return 1;
    }

    char base_hdr[512];
    (void)snprintf(base_hdr, sizeof(base_hdr), "%s/turkic_jam.h", HEADER_DIR);
    const char *headers[] = {base_hdr};
    char combined[512];
    (void)snprintf(combined, sizeof(combined), "%s/turkic_jam_assets.h", HEADER_DIR);
    nt_builder_merge_headers(headers, 1, combined);
    (void)printf("Generated: %s\n", combined);
    // #endregion

    (void)printf("\n=== Done ===\n");
    return 0;
}
