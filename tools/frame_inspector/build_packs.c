/* Build the frame inspector pack: frame_inspector.ntpack -- sprite + text shaders, a monospace font and an
 * atlas holding the white pixel the UI fills rectangles with.
 * Usage: build_frame_inspector_packs <pack_dir>
 * Run from the project root directory. */

#include "nt_builder.h"

#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define HEADER_DIR "tools/frame_inspector/generated"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        (void)fprintf(stderr, "Usage: build_frame_inspector_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];
    if (nt_builder_up_to_date(out_dir)) {
        return 0;
    }
    MKDIR(out_dir);
    MKDIR(HEADER_DIR);
    char path[512];
    char cache_dir[512];
    (void)snprintf(path, sizeof(path), "%s/frame_inspector.ntpack", out_dir);
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);
    MKDIR(cache_dir);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start frame_inspector.ntpack\n");
        return 1;
    }
    nt_builder_set_header_dir(ctx, HEADER_DIR);
    nt_builder_set_cache_dir(ctx, cache_dir);
    nt_builder_set_threads_auto(ctx);

    nt_builder_add_shader(ctx, "assets/shaders/sprite.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/sprite.frag", NT_BUILD_SHADER_FRAGMENT);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.frag", NT_BUILD_SHADER_FRAGMENT);

    nt_atlas_opts_t atlas_opts = nt_atlas_opts_defaults();
    atlas_opts.shape = NT_ATLAS_SHAPE_RECT;
    atlas_opts.allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY;
    atlas_opts.pixels_per_unit = 1.0F;
    atlas_opts.padding = 2;
    atlas_opts.margin = 2;
    atlas_opts.extrude = 1;
    atlas_opts.compress = (nt_basisu_encode_opts_t){0};
    atlas_opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    atlas_opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    atlas_opts.mips = NT_TEX_MIPS_NONE;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "frame_inspector_atlas", &atlas_opts);
    static const uint8_t white_pixel[4] = {255, 255, 255, 255};
    nt_atlas_sprite_opts_t white_opts = nt_atlas_sprite_opts_defaults();
    white_opts.name = "_white";
    nt_atlas_add_raw(atlas, white_pixel, 1, 1, &white_opts);
    (void)nt_atlas_commit(atlas);

    nt_builder_add_font(ctx, "assets/fonts/DejaVuSansMono.ttf", &(nt_font_opts_t){.charset = NT_CHARSET_ASCII, .resource_name = "frame_inspector/mono"});

    const nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "frame_inspector.ntpack failed: %d\n", r);
        return 1;
    }
    nt_builder_write_record(out_dir);
    return 0;
}
