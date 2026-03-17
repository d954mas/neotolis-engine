/*
 * Builder Demo -- code-first asset pipeline example.
 *
 * Shows how a game links nt_builder and writes a custom pack build script.
 * This is NOT meant to be run (the referenced assets don't exist),
 * it demonstrates the API usage pattern.
 */

#include "nt_builder.h"

#include <stdio.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Stream layout: position (float32 x3) + uv0 (float32 x2) */
    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false},
    };

    NtBuilderContext *ctx = nt_builder_start_pack("build/assets/demo.neopak");
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start pack\n");
        return 1;
    }

    /* Single-file additions */
    nt_builder_add_shader(ctx, "assets/shaders/mesh.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/mesh.frag", NT_BUILD_SHADER_FRAGMENT);

    /* Batch add all .glb files from a directory (single-level glob, no recursion) */
    nt_builder_add_meshes(ctx, "assets/meshes/*.glb", layout, 2);

    /* Batch add all .png textures */
    nt_builder_add_textures(ctx, "assets/textures/*.png");

    nt_build_result_t result = nt_builder_finish_pack(ctx);
    if (result != NT_BUILD_OK) {
        (void)fprintf(stderr, "Build failed with code %d\n", result);
        nt_builder_free_pack(ctx);
        return 1;
    }

    /* Verify the pack */
    nt_builder_dump_pack("build/assets/demo.neopak");

    nt_builder_free_pack(ctx);
    return 0;
}
