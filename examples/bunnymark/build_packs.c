/*
 * Build bunnymark demo packs:
 *   bunnymark_sd.ntpack -- sprite.vert + sprite.frag + atlas with 5 SD bunnies
 *
 * SD-only in Plan 06; HD pack lands in Plan 07. The atlas uses RECT shape
 * (rect bunnies — fastest pack) with allow_transform for D4 orientations,
 * pixels_per_unit=1.0 (D-32 default; combined with HD pack at ppu=3 in Plan 07
 * the on-screen size matches when both packs share the same Transform).
 *
 * Usage: build_bunnymark_packs <pack_dir>
 * Run from the project root directory.
 */

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

#define HEADER_DIR "examples/bunnymark/generated"

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_bunnymark_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build Bunnymark SD Pack -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(HEADER_DIR);

    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);
    MKDIR(cache_dir);

    // #region pack 1: bunnymark_sd.ntpack
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "bunnymark_sd.ntpack"));
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start bunnymark_sd.ntpack\n");
        return 1;
    }

    nt_builder_set_header_dir(ctx, HEADER_DIR);
    nt_builder_set_cache_dir(ctx, cache_dir);

    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded CLI tool, getenv is fine
    const char *threads_env = getenv("NT_BUILDER_THREADS");
    if (threads_env && threads_env[0] != '\0') {
        uint32_t threads = (uint32_t)strtoul(threads_env, NULL, 10);
        if (threads > 0) {
            nt_builder_set_threads(ctx, threads);
        } else {
            nt_builder_set_threads_auto(ctx);
        }
    } else {
        nt_builder_set_threads_auto(ctx);
    }
    // #endregion

    // #region shaders (game-shipped per D-21, satisfies SPRITE-09 via Phase 35 GLSL validate)
    nt_builder_add_shader(ctx, "assets/shaders/sprite.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/sprite.frag", NT_BUILD_SHADER_FRAGMENT);
    (void)printf("  Shaders added: 2 (sprite.vert + sprite.frag)\n");
    // #endregion

    // #region atlas: 5 SD bunny variants
    nt_atlas_opts_t atlas_opts = nt_atlas_opts_defaults();
    atlas_opts.shape = NT_ATLAS_SHAPE_RECT; /* rectangular bunnies — fastest pack */
    atlas_opts.allow_transform = true;
    atlas_opts.pixels_per_unit = 1.0F; /* D-32 default: 1 source pixel = 1 world unit */
    atlas_opts.padding = 2;
    atlas_opts.margin = 2;
    atlas_opts.extrude = 2; /* OK with RECT (extrude valid only for rect shape) */

    nt_builder_begin_atlas(ctx, "bunnies", &atlas_opts);

    /* Centre pivot for all sprites (D-13 default origin reset on set_region). */
    nt_atlas_sprite_opts_t sprite_opts = nt_atlas_sprite_opts_defaults();
    /* sprite_opts is centre pivot by default — origin_x=origin_y=0.5 */

    nt_builder_atlas_add(ctx, "examples/bunnymark/raw/sd/bunny_red.png", &sprite_opts);
    nt_builder_atlas_add(ctx, "examples/bunnymark/raw/sd/bunny_green.png", &sprite_opts);
    nt_builder_atlas_add(ctx, "examples/bunnymark/raw/sd/bunny_blue.png", &sprite_opts);
    nt_builder_atlas_add(ctx, "examples/bunnymark/raw/sd/bunny_yellow.png", &sprite_opts);
    nt_builder_atlas_add(ctx, "examples/bunnymark/raw/sd/bunny_purple.png", &sprite_opts);

    nt_builder_end_atlas(ctx);
    (void)printf("  Atlas 'bunnies' added: 5 sprites (RECT, ppu=1.0)\n");
    // #endregion

    // #region finish + codegen
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "bunnymark_sd.ntpack failed: %d\n", r);
        return 1;
    }

    /* Merge per-pack header into combined bunnymark_assets.h */
    char base_hdr[512];
    (void)snprintf(base_hdr, sizeof(base_hdr), "%s/bunnymark_sd.h", HEADER_DIR);
    const char *headers[] = {base_hdr};
    char combined[512];
    (void)snprintf(combined, sizeof(combined), "%s/bunnymark_assets.h", HEADER_DIR);
    nt_builder_merge_headers(headers, 1, combined);
    (void)printf("Generated: %s\n", combined);
    // #endregion

    /* Print pack size summary */
    (void)printf("\n=== Pack Size Summary ===\n");
    FILE *f = fopen(pack_path(out_dir, "bunnymark_sd.ntpack"), "rb");
    if (f) {
        (void)fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        (void)fclose(f);
        (void)printf("  bunnymark_sd.ntpack    %8.1f KB\n", (double)sz / 1024.0);
    }

    (void)printf("\n=== Done ===\n");
    return 0;
}
