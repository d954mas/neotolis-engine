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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_atlas_packs <pack_dir>\n");
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
    nt_builder_set_header_dir(ctx, HEADER_DIR);
    nt_builder_set_cache_dir(ctx, "build/examples/atlas/_cache");
    /* --- Mesh + shaders (reuse textured_quad assets) --- */

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false},
    };
    nt_builder_add_mesh(ctx, "assets/meshes/cube.glb", &(nt_mesh_opts_t){.layout = layout, .stream_count = 2});
    nt_builder_add_shader(ctx, "assets/shaders/mesh_inst.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/mesh_inst.frag", NT_BUILD_SHADER_FRAGMENT);

    /* --- Atlas: pack spineboy sprites with polygon mode --- */

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
    (void)printf("atlas=%s max=%u ts=%u poly=%s\n", atlas_name, opts.max_size, opts.tile_size, opts.polygon_mode ? "yes" : "no");

    nt_builder_begin_atlas(ctx, atlas_name, &opts);
    nt_builder_atlas_add_glob(ctx, glob_pattern);
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
