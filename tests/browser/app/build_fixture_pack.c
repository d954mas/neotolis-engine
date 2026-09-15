/* Browser smoke app's Basis fixture pack: a 128x128 RGBA and a 96x64 opaque RGB
 * texture of two solid halves (ETC1S when its option is ON, else UASTC), so every
 * transcode target lands on the same texels. main.c requests the same pair. */

#include "nt_builder.h"

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

#define FIXTURE_RGBA_SIZE 128U
#define FIXTURE_RGB_WIDTH 96U
#define FIXTURE_RGB_HEIGHT 64U

static const uint8_t k_left[4] = {200, 40, 40, 255};
static const uint8_t k_right_rgba[4] = {40, 40, 200, 128};
static const uint8_t k_right_rgb[4] = {40, 40, 200, 255};

static void fill_halves(uint8_t *px, uint32_t w, uint32_t h, const uint8_t *left, const uint8_t *right) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            memcpy(px + ((((size_t)y * w) + x) * 4U), (x < w / 2U) ? left : right, 4);
        }
    }
}

static void add_pair(NtBuilderContext *ctx, const char *suffix, nt_basisu_encode_opts_t compress) {
    static uint8_t rgba[FIXTURE_RGBA_SIZE * FIXTURE_RGBA_SIZE * 4U];
    static uint8_t rgb[FIXTURE_RGB_WIDTH * FIXTURE_RGB_HEIGHT * 4U];
    char name[64];

    nt_tex_opts_t opts = nt_tex_opts_defaults();
    opts.compress = compress;
    opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    opts.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;

    fill_halves(rgba, FIXTURE_RGBA_SIZE, FIXTURE_RGBA_SIZE, k_left, k_right_rgba);
    (void)snprintf(name, sizeof(name), "basis_%s_rgba", suffix);
    nt_builder_add_texture_raw(ctx, rgba, FIXTURE_RGBA_SIZE, FIXTURE_RGBA_SIZE, name, &opts);

    fill_halves(rgb, FIXTURE_RGB_WIDTH, FIXTURE_RGB_HEIGHT, k_left, k_right_rgb);
    opts.format = NT_TEXTURE_FORMAT_RGB8;
    (void)snprintf(name, sizeof(name), "basis_%s_rgb", suffix);
    nt_builder_add_texture_raw(ctx, rgb, FIXTURE_RGB_WIDTH, FIXTURE_RGB_HEIGHT, name, &opts);
    (void)printf("  Textures added: basis_%s_rgba, basis_%s_rgb\n", suffix, suffix);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_browser_fixture_pack <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];
    (void)printf("=== Build browser Basis fixture pack -> %s ===\n\n", out_dir);
    MKDIR(out_dir);

    char path[512];
    (void)snprintf(path, sizeof(path), "%s/_cache", out_dir);
    MKDIR(path);
    char cache_dir[512];
    memcpy(cache_dir, path, sizeof(cache_dir));
    (void)snprintf(path, sizeof(path), "%s/basis_fixture.ntpack", out_dir);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start basis_fixture.ntpack\n");
        return 1;
    }
    nt_builder_set_cache_dir(ctx, cache_dir);
    nt_builder_set_threads_auto(ctx);

#if NT_BASISU_HAS_ETC1S
    add_pair(ctx, "etc1s", nt_tex_compress_etc1s_default());
#else
    add_pair(ctx, "uastc", nt_tex_compress_uastc_default());
#endif

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "basis_fixture.ntpack failed: %d\n", r);
        return 1;
    }
    (void)printf("Generated: %s\n", path);
    return 0;
}
