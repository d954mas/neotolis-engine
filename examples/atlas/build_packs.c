/*
 * Build atlas demo pack:
 *   atlas_demo.ntpack — cube mesh + shaders + spineboy sprite atlas
 *
 * Packs all 63 Spineboy sprites (from JCash/atlaspacker) into a
 * polygon-mode atlas with convex hull regions and debug PNG output.
 *
 * Usage: build_atlas_packs <output_dir>
 * Run from the project root directory.
 */

#define NT_BUILD_MAX_ASSETS 16384
#define GLOB_MAX_MATCHES 8192
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

#define HEADER_DIR "examples/atlas/generated"

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

/* Glob callback that adds sprites with an optional count limit */
typedef struct {
    NtBuilderContext *ctx;
    uint32_t count;
    uint32_t limit; /* 0 = unlimited */
} LimitedAddData;

static void limited_add_callback(const char *path, void *user) {
    LimitedAddData *d = (LimitedAddData *)user;
    if (d->limit > 0 && d->count >= d->limit) {
        return;
    }
    nt_builder_atlas_add(d->ctx, path, NULL);
    d->count++;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_atlas_packs <pack_dir> [max_size] [tile_size] [glob] [name] [r=rect] [max_sprites]\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build Atlas Demo Pack -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(HEADER_DIR);

    /* Header paths for merge */
    char pack_hdr[512];
    (void)snprintf(pack_hdr, sizeof(pack_hdr), "%s/atlas_demo.h", HEADER_DIR);

    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "atlas_demo.ntpack"));
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start pack\n");
        return 1;
    }
    if (argc < 5) { /* codegen only for default spineboy test */
        nt_builder_set_header_dir(ctx, HEADER_DIR);
    }
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
    nt_builder_set_cache_dir(ctx, "build/examples/atlas/_cache");
    /* --- Mesh + shaders (reuse textured_quad assets) --- */

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false},
    };
    nt_builder_add_mesh(ctx, "assets/meshes/cube.glb", &(nt_mesh_opts_t){.layout = layout, .stream_count = 2});
    nt_builder_add_shader(ctx, "assets/shaders/mesh_inst.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/mesh_inst.frag", NT_BUILD_SHADER_FRAGMENT);

    /* --- Atlas: pack sprites with polygon mode --- */

    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.max_size = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 2048;
    opts.polygon_mode = true;
    opts.max_vertices = 8;
    opts.allow_rotate = true;
    opts.debug_png = true;
    opts.tile_size = (argc >= 4) ? (uint8_t)atoi(argv[3]) : 4;
    const char *glob_pattern = (argc >= 5) ? argv[4] : "assets/sprites/spineboy/*.png";
    const char *atlas_name = (argc >= 6) ? argv[5] : "spineboy";
    if (argc >= 7 && argv[6][0] == 'r') {
        opts.polygon_mode = false;
    }
    if (argc >= 7 && argv[6][0] == 'v') {
        opts.vector_pack = true;
    }
    uint32_t max_sprites = (argc >= 8) ? (uint32_t)atoi(argv[7]) : 0;
    (void)printf("atlas=%s max=%u ts=%u poly=%s max_sprites=%u\n", atlas_name, opts.max_size, opts.tile_size, opts.polygon_mode ? "yes" : "no", max_sprites);

    nt_builder_begin_atlas(ctx, atlas_name, &opts);
    if (max_sprites > 0) {
        LimitedAddData data = {ctx, 0, max_sprites};
        (void)nt_builder_glob_iterate(glob_pattern, limited_add_callback, &data);
        (void)printf("Added %u sprites (limited to %u)\n", data.count, max_sprites);
    } else {
        nt_builder_atlas_add_glob(ctx, glob_pattern);
    }
    nt_builder_end_atlas(ctx);

    /* Finish and generate headers */
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "Pack failed: %d\n", r);
        return 1;
    }

    /* Generate combined header */
    const char *headers[] = {pack_hdr};
    char combined[512];
    (void)snprintf(combined, sizeof(combined), "%s/atlas_assets.h", HEADER_DIR);
    nt_builder_merge_headers(headers, 1, combined);

    (void)printf("\nDone.\n");
    return 0;
}
