/*
 * Build three .neopak packs for the textured cube demo:
 *   base.neopak         — cube mesh + shaders (always loaded)
 *   lenna_pixel.neopak  — 64x64 pixel art lenna
 *   lenna_hires.neopak  — full resolution lenna
 *
 * Both texture packs share resource_id "textures/lenna"
 * so the resource system resolves by priority.
 *
 * Usage: build_tq_packs <output_dir>
 *   output_dir — where to write .neopak files (e.g. build/examples/textured_quad)
 * Run from the project root directory.
 */

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

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_tq_packs <output_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build Textured Cube Packs → %s ===\n\n", out_dir);

    MKDIR(out_dir);

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false},
    };

    /* ---- Pack 0: base (cube mesh + shaders) ---- */
    {
        NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "base.neopak"));
        if (!ctx) {
            (void)fprintf(stderr, "Failed to start base pack\n");
            return 1;
        }
        nt_builder_add_mesh(ctx, "assets/meshes/cube.glb", layout, 2);
        nt_builder_add_shader(ctx, "assets/shaders/mesh.vert", NT_BUILD_SHADER_VERTEX);
        nt_builder_add_shader(ctx, "assets/shaders/mesh.frag", NT_BUILD_SHADER_FRAGMENT);
        nt_build_result_t r = nt_builder_finish_pack(ctx);
        nt_builder_free_pack(ctx);
        if (r != NT_BUILD_OK) {
            (void)fprintf(stderr, "Base pack failed: %d\n", r);
            return 1;
        }
        (void)printf("Built: base.neopak\n");
    }

    /* ---- Pack 1: pixel texture (64x64) ---- */
    {
        NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "lenna_pixel.neopak"));
        if (!ctx) {
            (void)fprintf(stderr, "Failed to start pixel pack\n");
            return 1;
        }
        nt_builder_add_texture(ctx, "assets/textures/lenna_pixel.bmp");
        nt_builder_rename(ctx, "assets/textures/lenna_pixel.bmp", "textures/lenna");
        nt_build_result_t r = nt_builder_finish_pack(ctx);
        nt_builder_free_pack(ctx);
        if (r != NT_BUILD_OK) {
            (void)fprintf(stderr, "Pixel pack failed: %d\n", r);
            return 1;
        }
        (void)printf("Built: lenna_pixel.neopak\n");
    }

    /* ---- Pack 2: hires texture (512x512) ---- */
    {
        NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "lenna_hires.neopak"));
        if (!ctx) {
            (void)fprintf(stderr, "Failed to start hires pack\n");
            return 1;
        }
        nt_builder_add_texture(ctx, "assets/textures/lenna.png");
        nt_builder_rename(ctx, "assets/textures/lenna.png", "textures/lenna");
        nt_build_result_t r = nt_builder_finish_pack(ctx);
        nt_builder_free_pack(ctx);
        if (r != NT_BUILD_OK) {
            (void)fprintf(stderr, "Hires pack failed: %d\n", r);
            return 1;
        }
        (void)printf("Built: lenna_hires.neopak\n");
    }

    (void)printf("\nDone.\n");
    return 0;
}
