/*
 * Builder Demo -- builds a .ntpack pack from assets/ directory.
 * NT_PROJECT_ROOT is injected by CMake so paths work regardless of cwd.
 */

#include "nt_builder.h"

#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define CHDIR(p) _chdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#define CHDIR(p) chdir(p)
#endif

#define STR(x) #x
#define XSTR(x) STR(x)

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    CHDIR(XSTR(NT_PROJECT_ROOT));

    (void)printf("=== Builder Demo ===\n\n");

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false},
    };

    MKDIR("build");
    MKDIR("build/assets");

    NtBuilderContext *ctx = nt_builder_start_pack("build/assets/demo.ntpack");
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start pack\n");
        return 1;
    }

    /* Shaders */
    nt_builder_add_shader(ctx, "assets/shaders/mesh.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/mesh.frag", NT_BUILD_SHADER_FRAGMENT);

    /* Cube mesh */
    nt_builder_add_mesh(ctx, "assets/meshes/cube.glb", layout, 2);

    /* Lenna texture */
    nt_builder_add_texture(ctx, "assets/textures/lenna.png");

    nt_build_result_t result = nt_builder_finish_pack(ctx);
    if (result != NT_BUILD_OK) {
        (void)fprintf(stderr, "Build failed with code %d\n", result);
        nt_builder_free_pack(ctx);
        return 1;
    }

    (void)printf("\n--- Pack contents ---\n");
    nt_builder_dump_pack("build/assets/demo.ntpack");

    nt_builder_free_pack(ctx);

    (void)printf("\nDone. Pack: build/assets/demo.ntpack\n");
    return 0;
}
