/*
 * Builder Demo -- builds a real .neopak pack from example assets.
 *
 * Generates a cube mesh (GLB), uses lenna.png texture and
 * shaders from assets/shaders/. Run from the project root directory.
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

/* --- Generate cube GLB with position + UV --- */

static void generate_cube_glb(const char *path) {
    /* 24 vertices (4 per face), 36 indices (2 tris per face) */
    /* clang-format off */
    float positions[] = {
        /* Front face (z = +0.5) */
        -0.5F, -0.5F,  0.5F,   0.5F, -0.5F,  0.5F,   0.5F,  0.5F,  0.5F,  -0.5F,  0.5F,  0.5F,
        /* Back face (z = -0.5) */
         0.5F, -0.5F, -0.5F,  -0.5F, -0.5F, -0.5F,  -0.5F,  0.5F, -0.5F,   0.5F,  0.5F, -0.5F,
        /* Right face (x = +0.5) */
         0.5F, -0.5F,  0.5F,   0.5F, -0.5F, -0.5F,   0.5F,  0.5F, -0.5F,   0.5F,  0.5F,  0.5F,
        /* Left face (x = -0.5) */
        -0.5F, -0.5F, -0.5F,  -0.5F, -0.5F,  0.5F,  -0.5F,  0.5F,  0.5F,  -0.5F,  0.5F, -0.5F,
        /* Top face (y = +0.5) */
        -0.5F,  0.5F,  0.5F,   0.5F,  0.5F,  0.5F,   0.5F,  0.5F, -0.5F,  -0.5F,  0.5F, -0.5F,
        /* Bottom face (y = -0.5) */
        -0.5F, -0.5F, -0.5F,   0.5F, -0.5F, -0.5F,   0.5F, -0.5F,  0.5F,  -0.5F, -0.5F,  0.5F,
    };
    float uvs[] = {
        /* Each face: same UV mapping */
        0.0F, 0.0F,  1.0F, 0.0F,  1.0F, 1.0F,  0.0F, 1.0F,
        0.0F, 0.0F,  1.0F, 0.0F,  1.0F, 1.0F,  0.0F, 1.0F,
        0.0F, 0.0F,  1.0F, 0.0F,  1.0F, 1.0F,  0.0F, 1.0F,
        0.0F, 0.0F,  1.0F, 0.0F,  1.0F, 1.0F,  0.0F, 1.0F,
        0.0F, 0.0F,  1.0F, 0.0F,  1.0F, 1.0F,  0.0F, 1.0F,
        0.0F, 0.0F,  1.0F, 0.0F,  1.0F, 1.0F,  0.0F, 1.0F,
    };
    uint16_t indices[] = {
         0,  1,  2,   0,  2,  3,  /* front */
         4,  5,  6,   4,  6,  7,  /* back */
         8,  9, 10,   8, 10, 11,  /* right */
        12, 13, 14,  12, 14, 15,  /* left */
        16, 17, 18,  16, 18, 19,  /* top */
        20, 21, 22,  20, 22, 23,  /* bottom */
    };
    /* clang-format on */

    /* JSON chunk */
    char json_buf[1024];
    int json_len = snprintf(
        json_buf, sizeof(json_buf),
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":2}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":24,\"type\":\"VEC3\","
        "\"max\":[0.5,0.5,0.5],\"min\":[-0.5,-0.5,-0.5]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":24,\"type\":\"VEC2\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":36,\"type\":\"SCALAR\"}"
        "],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}"
        "],"
        "\"buffers\":[{\"byteLength\":%u}]"
        "}",
        (uint32_t)sizeof(positions),
        (uint32_t)sizeof(positions), (uint32_t)sizeof(uvs),
        (uint32_t)(sizeof(positions) + sizeof(uvs)), (uint32_t)sizeof(indices),
        (uint32_t)(sizeof(positions) + sizeof(uvs) + sizeof(indices)));

    uint32_t json_padded = ((uint32_t)json_len + 3U) & ~3U;
    uint32_t json_padding = json_padded - (uint32_t)json_len;

    /* Pad indices to 4-byte boundary */
    uint32_t bin_raw = (uint32_t)(sizeof(positions) + sizeof(uvs) + sizeof(indices));
    uint32_t bin_padded = (bin_raw + 3U) & ~3U;
    uint32_t bin_padding = bin_padded - bin_raw;

    uint32_t glb_magic = 0x46546C67;
    uint32_t glb_version = 2;
    uint32_t json_chunk_type = 0x4E4F534A;
    uint32_t bin_chunk_type = 0x004E4942;
    uint32_t total_length = 12 + 8 + json_padded + 8 + bin_padded;

    FILE *f = fopen(path, "wb");
    if (!f) {
        (void)fprintf(stderr, "ERROR: cannot create %s\n", path);
        return;
    }

    (void)fwrite(&glb_magic, 4, 1, f);
    (void)fwrite(&glb_version, 4, 1, f);
    (void)fwrite(&total_length, 4, 1, f);

    (void)fwrite(&json_padded, 4, 1, f);
    (void)fwrite(&json_chunk_type, 4, 1, f);
    (void)fwrite(json_buf, 1, (size_t)json_len, f);
    for (uint32_t i = 0; i < json_padding; i++) {
        char space = ' ';
        (void)fwrite(&space, 1, 1, f);
    }

    (void)fwrite(&bin_padded, 4, 1, f);
    (void)fwrite(&bin_chunk_type, 4, 1, f);
    (void)fwrite(positions, sizeof(positions), 1, f);
    (void)fwrite(uvs, sizeof(uvs), 1, f);
    (void)fwrite(indices, sizeof(indices), 1, f);
    if (bin_padding > 0) {
        uint8_t zeros[4] = {0};
        (void)fwrite(zeros, 1, bin_padding, f);
    }

    (void)fclose(f);
    (void)printf("Generated: %s (24 verts, 36 indices)\n", path);
}

/* --- Main --- */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    (void)printf("=== Builder Demo ===\n\n");

    /* Generate cube mesh */
    MKDIR("assets");
    MKDIR("assets/meshes");
    generate_cube_glb("assets/meshes/cube.glb");

    /* Stream layout: position (float32 x3) + uv (float32 x2) */
    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false},
    };

    MKDIR("build");
    MKDIR("build/assets");

    NtBuilderContext *ctx = nt_builder_start_pack("build/assets/demo.neopak");
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
    nt_builder_dump_pack("build/assets/demo.neopak");

    nt_builder_free_pack(ctx);

    (void)printf("\nDone. Pack: build/assets/demo.neopak\n");
    return 0;
}
