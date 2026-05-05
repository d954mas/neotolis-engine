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
    /* Slug shaders for the on-screen stats overlay (FPS / draws / bunnies). */
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.frag", NT_BUILD_SHADER_FRAGMENT);
    (void)printf("  Shaders added: 4 (sprite + slug_text)\n");
    // #endregion

    // #region font for stats overlay (Latin only — overlay is ASCII)
    nt_builder_add_font(ctx, "assets/fonts/LilitaOne-RussianChineseKo.ttf",
                        &(nt_font_opts_t){
                            .charset = NT_CHARSET_ASCII,
                            .resource_name = "bunnymark/font_overlay",
                        });
    (void)printf("  Font (ASCII) added: bunnymark/font_overlay\n");
    // #endregion

    // #region atlas: 5 SD bunny variants
    nt_atlas_opts_t atlas_opts = nt_atlas_opts_defaults();
    atlas_opts.shape = NT_ATLAS_SHAPE_RECT; /* rectangular bunnies — fastest pack */
    atlas_opts.allow_transform = true;
    atlas_opts.pixels_per_unit = 1.0F; /* D-32 default: 1 source pixel = 1 world unit */
    atlas_opts.padding = 2;
    atlas_opts.margin = 2;
    atlas_opts.extrude = 2; /* OK with RECT (extrude valid only for rect shape) */
    atlas_opts.premultiplied = true;
    /* UASTC compress matches the HD pack (both packs go through the same
     * Basis transcode path on GPU). Without compress on SD, HD was sampling
     * a smaller compressed BC7/ASTC texel block while SD was reading raw
     * RGBA8 — different memory bandwidth profile, different fps. Equal
     * configs make the SD/HD comparison meaningful. */
    nt_tex_compress_opts_t sd_compress_opts = nt_tex_compress_uastc_default();
    atlas_opts.compress = &sd_compress_opts;
    /* Bunnymark is a stress test — we WANT to see the real per-fragment cost
     * under heavy overdraw, not mask it with the driver's fast-path mipmap
     * sampling on compressed textures. LINEAR no-mips matches Defold's
     * sprite default and gives a clean baseline for the renderer's CPU and
     * GPU work. (For real games with shipping content, trilinear+mipmaps
     * usually wins on this driver — pick filter per-asset to taste.) */
    atlas_opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    atlas_opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    atlas_opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    atlas_opts.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;

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

#ifdef BUNNYMARK_HD_AVAILABLE
    // #region pack 2: bunnymark_hd.ntpack (Open Q3 — guarded by CMake)
    /* HD pack uses pixels_per_unit=17.0F so the on-screen size after the
     * builder's alpha-trim matches the SD bunny.
     *
     * SD bunnies are pixel art with no transparent margin → trim is a no-op.
     * HD illustrations have ~42% transparent margin around the character that
     * the builder strips during alpha-trim, so the actual rendered region per
     * HD bunny is ~227 750 px (avg of BENCH trim_area / 5), i.e. ≈477x477
     * visible content. Dividing by ppu=17 → ~28x28 world units, close to
     * SD's 25x32. With the earlier ppu=22 the trimmed HD bunnies rendered
     * at ~22x22 — visibly smaller AND hitting fewer fragments per sprite,
     * which masked the real per-fragment cost (HD looked artificially fast
     * on 60k bunnies vs SD).
     *
     * Phase 48 atlas merge keeps region indices stable on stack so live
     * SpriteComponent.region_index values stay valid across the toggle
     * (DEMO-08). */
    (void)printf("\n=== Build Bunnymark HD Pack -> %s ===\n\n", out_dir);

    NtBuilderContext *ctx_hd = nt_builder_start_pack(pack_path(out_dir, "bunnymark_hd.ntpack"));
    if (!ctx_hd) {
        (void)fprintf(stderr, "Failed to start bunnymark_hd.ntpack\n");
        return 1;
    }

    nt_builder_set_header_dir(ctx_hd, HEADER_DIR);
    nt_builder_set_cache_dir(ctx_hd, cache_dir);
    if (threads_env && threads_env[0] != '\0') {
        uint32_t threads_hd = (uint32_t)strtoul(threads_env, NULL, 10);
        if (threads_hd > 0) {
            nt_builder_set_threads(ctx_hd, threads_hd);
        } else {
            nt_builder_set_threads_auto(ctx_hd);
        }
    } else {
        nt_builder_set_threads_auto(ctx_hd);
    }

    /* HD pack carries no shaders — they're already in SD pack and stack via
     * the resource registry by id (highest-priority winner serves both). */

    nt_atlas_opts_t hd_opts = nt_atlas_opts_defaults();
    hd_opts.shape = NT_ATLAS_SHAPE_RECT;
    hd_opts.allow_transform = true;
    hd_opts.pixels_per_unit = 17.0F; /* HD trim_area / 5 ≈ 477x477 visible / 17 ≈ 28x28 world ≈ SD 25x32 */
    hd_opts.padding = 2;
    hd_opts.margin = 2;
    hd_opts.extrude = 2;
    hd_opts.premultiplied = true;
    nt_tex_compress_opts_t uastc_compress_opts = nt_tex_compress_uastc_default();
    hd_opts.compress = &uastc_compress_opts;
    /* HD anti-aliased illustration rendered at ~17:1 downscale benefits from
     * mipmap chain selection — keep trilinear default. CLAMP_TO_EDGE so the
     * filter never bleeds across atlas region boundaries during minification. */
    hd_opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR;
    hd_opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    hd_opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    hd_opts.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;

    nt_builder_begin_atlas(ctx_hd, "bunnies", &hd_opts);
    /* atlas_add_glob picks up whatever 5 PNGs the user dropped in raw/hd/.
     * The user is expected to use the same 5 names (bunny_red.png …) so the
     * region name_hashes match SD — Phase 48 merge keeps the region indices
     * stable on stack (DEMO-08). atlas_add_glob requires opts->name == NULL
     * (each matched file derives its own name from basename). */
    nt_builder_atlas_add_glob(ctx_hd, "examples/bunnymark/raw/hd/*.png", NULL);
    nt_builder_end_atlas(ctx_hd);
    (void)printf("  Atlas 'bunnies' added: HD pack (RECT, ppu=17.0)\n");

    nt_build_result_t r_hd = nt_builder_finish_pack(ctx_hd);
    nt_builder_free_pack(ctx_hd);
    if (r_hd != NT_BUILD_OK) {
        (void)fprintf(stderr, "bunnymark_hd.ntpack failed: %d\n", r_hd);
        return 1;
    }
    // #endregion
#endif

    /* Print pack size summary */
    (void)printf("\n=== Pack Size Summary ===\n");
    FILE *f = fopen(pack_path(out_dir, "bunnymark_sd.ntpack"), "rb");
    if (f) {
        (void)fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        (void)fclose(f);
        (void)printf("  bunnymark_sd.ntpack    %8.1f KB\n", (double)sz / 1024.0);
    }
#ifdef BUNNYMARK_HD_AVAILABLE
    FILE *f_hd = fopen(pack_path(out_dir, "bunnymark_hd.ntpack"), "rb");
    if (f_hd) {
        (void)fseek(f_hd, 0, SEEK_END);
        long sz_hd = ftell(f_hd);
        (void)fclose(f_hd);
        (void)printf("  bunnymark_hd.ntpack    %8.1f KB\n", (double)sz_hd / 1024.0);
    }
#endif

    (void)printf("\n=== Done ===\n");
    return 0;
}
