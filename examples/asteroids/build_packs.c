#include "nt_builder.h"
#include "reference_scene_data/scene_format.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t s_unique_counts[10] = {35, 50, 75, 100, 200, 300, 400, 500, 750, 1000};
static const char *const s_sky_faces[6] = {"PositiveX", "NegativeX", "PositiveY", "NegativeY", "PositiveZ", "NegativeZ"};
static const NtStreamLayout s_layout[] = {
    {"position", "POSITION", NT_STREAM_FLOAT32, 3, false, 0},
    {"normal", "NORMAL", NT_STREAM_FLOAT32, 3, false, 0},
    {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false, 0},
};

// NT_BUILD_ASSERT expands to nested fail-fast branches; the I/O sequence is linear.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint8_t *read_file(const char *path, uint32_t *size) {
    FILE *file = fopen(path, "rb");
    NT_BUILD_ASSERT(file && "reference export missing; build the offline generator target first");
    int result = fseek(file, 0, SEEK_END);
    NT_BUILD_ASSERT(result == 0);
    long length = ftell(file);
    NT_BUILD_ASSERT(length > 0 && (unsigned long)length <= UINT32_MAX);
    result = fseek(file, 0, SEEK_SET);
    NT_BUILD_ASSERT(result == 0);
    uint8_t *data = malloc((size_t)length);
    NT_BUILD_ASSERT(data);
    size_t read = fread(data, 1, (size_t)length, file);
    NT_BUILD_ASSERT(read == (size_t)length);
    result = fclose(file);
    NT_BUILD_ASSERT(result == 0);
    *size = (uint32_t)length;
    return data;
}

static NtBuilderContext *start_pack(const char *directory, const char *name) {
    char path[1024];
    (void)snprintf(path, sizeof(path), "%s/%s.ntpack", directory, name);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    NT_BUILD_ASSERT(ctx);
    nt_builder_set_header_dir(ctx, "examples/asteroids/generated");
    nt_builder_set_threads(ctx, 2);
    return ctx;
}

static void finish_pack(NtBuilderContext *ctx) {
    nt_build_result_t result = nt_builder_finish_pack(ctx);
    NT_BUILD_ASSERT(result == NT_BUILD_OK);
    nt_builder_free_pack(ctx);
}

static void add_file_blob(NtBuilderContext *ctx, const char *path, const char *name) {
    uint32_t size;
    uint8_t *data = read_file(path, &size);
    nt_builder_add_blob(ctx, data, size, name);
    free(data);
}

static void build_core(const char *directory) {
    NtBuilderContext *ctx = start_pack(directory, "asteroids_core");
    nt_builder_add_font(ctx, "examples/asteroids/raw/DejaVuSansMono.ttf", &(nt_font_opts_t){.charset = NT_CHARSET_ASCII, .resource_name = "asteroids/font"});
    const char *const programs[] = {"rock", "planet", "sky"};
    const char *const vertices[] = {"scene", "planet", "sky"};
    for (uint32_t i = 0; i < 3; i++) {
        char path[128];
        (void)snprintf(path, sizeof(path), "examples/asteroids/shaders/%s.vert", vertices[i]);
        nt_builder_add_shader(ctx, path, NT_BUILD_SHADER_VERTEX);
        (void)snprintf(path, sizeof(path), "examples/asteroids/shaders/%s.frag", programs[i]);
        nt_builder_add_shader(ctx, path, NT_BUILD_SHADER_FRAGMENT);
    }
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.frag", NT_BUILD_SHADER_FRAGMENT);
    finish_pack(ctx);
}

static void build_space(const char *directory) {
    NtBuilderContext *ctx = start_pack(directory, "asteroids_space");
    char path[1024];
    (void)snprintf(path, sizeof(path), "%s/reference/geometry/35/environment.glb", directory);
    nt_glb_scene_t scene = {0};
    nt_build_result_t parsed = nt_builder_parse_glb_scene(&scene, path);
    NT_BUILD_ASSERT(parsed == NT_BUILD_OK && scene.mesh_count == 2);
    nt_builder_add_scene_mesh(ctx, &scene, 0, 0, "asteroids/planet", &(nt_mesh_opts_t){.layout = s_layout, .stream_count = 3});
    nt_builder_add_scene_mesh(ctx, &scene, 1, 0, "asteroids/sky_cube", &(nt_mesh_opts_t){.layout = s_layout, .stream_count = 3});
    nt_builder_free_glb_scene(&scene);
    nt_tex_opts_t tex = nt_tex_opts_defaults();
    tex.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_NEAREST;
    tex.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    tex.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    tex.format = NT_TEXTURE_FORMAT_SRGBA8;
    nt_builder_add_texture(ctx, "examples/asteroids/raw/Mars.jpg", &tex);
    nt_builder_rename(ctx, "examples/asteroids/raw/Mars.jpg", "asteroids/mars");
    tex.format = NT_TEXTURE_FORMAT_RGBA8;
    for (uint32_t face = 0; face < 6; face++) {
        char name[64];
        (void)snprintf(path, sizeof(path), "examples/asteroids/raw/Galaxy/%s.jpg", s_sky_faces[face]);
        (void)snprintf(name, sizeof(name), "asteroids/sky/%u", face);
        nt_builder_add_texture(ctx, path, &tex);
        nt_builder_rename(ctx, path, name);
    }
    finish_pack(ctx);
}

// NT_BUILD_ASSERT expansions account for the complexity; source tables are validated once.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void build_level(const char *directory, uint32_t level) {
    char name[64];
    char path[1024];
    (void)snprintf(name, sizeof(name), "asteroids_level_%u", level);
    NtBuilderContext *ctx = start_pack(directory, name);
    (void)snprintf(path, sizeof(path), "%s/reference/geometry/%u/geometry.bin", directory, s_unique_counts[level]);
    uint32_t size;
    uint8_t *geometry = read_file(path, &size);
    NT_BUILD_ASSERT(size >= 20);
    const uint32_t *header = (const uint32_t *)geometry;
    NT_BUILD_ASSERT(header[0] == 0x4F454741U && header[1] == 1 && header[2] == s_unique_counts[level]);
    NT_BUILD_ASSERT(header[3] <= 28 && header[4] == s_unique_counts[level] * 4);
    NT_BUILD_ASSERT(size == 20 + header[3] * 20 + header[4] * 32);
    nt_builder_add_blob(ctx, geometry, size, "asteroids/geometry");
    const uint32_t *chunks = header + 5;
    uint32_t subdivision_chunk[4] = {0};
    for (uint32_t chunk = 0; chunk < header[3]; chunk++) {
        uint32_t subdivision = chunks[(size_t)chunk * 5U];
        NT_BUILD_ASSERT(subdivision < 4);
        (void)snprintf(path, sizeof(path), "%s/reference/geometry/%u/rocks_s%u_c%u.glb", directory, s_unique_counts[level], subdivision, subdivision_chunk[subdivision]++);
        (void)snprintf(name, sizeof(name), "asteroids/rock_chunk_%u", chunk);
        nt_glb_scene_t scene = {0};
        nt_build_result_t parsed = nt_builder_parse_glb_scene(&scene, path);
        NT_BUILD_ASSERT(parsed == NT_BUILD_OK && scene.mesh_count == 1);
        nt_builder_add_scene_mesh(ctx, &scene, 0, 0, name, &(nt_mesh_opts_t){.layout = s_layout, .stream_count = 3});
        nt_builder_free_glb_scene(&scene);
    }
    free(geometry);
    (void)snprintf(path, sizeof(path), "%s/reference/scene/scene_level_%u.instances.bin", directory, level);
    add_file_blob(ctx, path, "asteroids/instances");
    finish_pack(ctx);
}

static void build_noise(const char *directory, uint32_t textures) {
    const uint32_t face_bytes = 256U * 256U * 4U;
    const uint32_t size = 24U + (textures * 3U * face_bytes);
    uint8_t *data = malloc(size);
    NT_BUILD_ASSERT(data);
    const uint32_t header[] = {0x4E545341U, 1, textures, 3, 256, 256};
    memcpy(data, header, sizeof(header));
    for (uint32_t texture = 0; texture < textures; texture++) {
        for (uint32_t face = 0; face < 3; face++) {
            char path[1024];
            (void)snprintf(path, sizeof(path), "%s/reference/scene/textures/texture_%u_layer_%u.rgba", directory, texture, face);
            uint32_t actual_size;
            uint8_t *pixels = read_file(path, &actual_size);
            NT_BUILD_ASSERT(actual_size == face_bytes);
            memcpy(data + 24U + ((((size_t)texture * 3U) + face) * face_bytes), pixels, face_bytes);
            free(pixels);
        }
    }
    char name[64];
    (void)snprintf(name, sizeof(name), "asteroids_noise_%u", textures);
    NtBuilderContext *ctx = start_pack(directory, name);
    nt_builder_add_blob(ctx, data, size, "asteroids/noise");
    free(data);
    finish_pack(ctx);
}

static void add_ui_round_rect(NtAtlasBuild *atlas, const char *name, uint32_t width, uint32_t height, float radius, uint16_t border) {
    enum { MAX_DIM = 32, SAMPLES = 4 };
    uint8_t pixels[MAX_DIM * MAX_DIM * 4];
    NT_BUILD_ASSERT(width <= MAX_DIM && height <= MAX_DIM);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t covered = 0;
            for (uint32_t sy = 0; sy < SAMPLES; sy++) {
                for (uint32_t sx = 0; sx < SAMPLES; sx++) {
                    float px = (float)x + (((float)sx + 0.5F) / (float)SAMPLES);
                    float py = (float)y + (((float)sy + 0.5F) / (float)SAMPLES);
                    float dx = fmaxf(fmaxf(radius - px, px - ((float)width - radius)), 0.0F);
                    float dy = fmaxf(fmaxf(radius - py, py - ((float)height - radius)), 0.0F);
                    covered += (dx * dx + dy * dy <= radius * radius) ? 1U : 0U;
                }
            }
            uint8_t *pixel = pixels + ((((size_t)y * width) + x) * 4U);
            pixel[0] = 255;
            pixel[1] = 255;
            pixel[2] = 255;
            pixel[3] = (uint8_t)((covered * 255U + SAMPLES * SAMPLES / 2U) / (SAMPLES * SAMPLES));
        }
    }
    nt_atlas_sprite_opts_t sprite = nt_atlas_sprite_opts_defaults();
    sprite.name = name;
    sprite.slice9_left = border;
    sprite.slice9_right = border;
    sprite.slice9_top = border;
    sprite.slice9_bottom = border;
    nt_atlas_add_raw(atlas, pixels, width, height, &sprite);
}

static void build_ui(const char *directory) {
    NtBuilderContext *ctx = start_pack(directory, "asteroids_ui");
    nt_builder_add_shader(ctx, "assets/shaders/sprite.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/sprite.frag", NT_BUILD_SHADER_FRAGMENT);

    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.max_size = 64;
    opts.shape = NT_ATLAS_SHAPE_RECT;
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY;
    opts.margin = 2;
    opts.extrude = 1;
    opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    opts.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    opts.gen_mipmaps = false;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "asteroids_ui_atlas", &opts);
    static const uint8_t white[4] = {255, 255, 255, 255};
    nt_atlas_sprite_opts_t sprite = nt_atlas_sprite_opts_defaults();
    sprite.name = "_white";
    nt_atlas_add_raw(atlas, white, 1, 1, &sprite);
    add_ui_round_rect(atlas, "rounded", 16, 16, 5.0F, 6);
    add_ui_round_rect(atlas, "pill", 32, 18, 8.0F, 8);
    add_ui_round_rect(atlas, "thumb", 24, 24, 12.0F, 0);
    nt_build_result_t result = nt_atlas_commit(atlas);
    NT_BUILD_ASSERT(result == NT_BUILD_OK);
    finish_pack(ctx);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--ui-only") != 0)) {
        (void)fprintf(stderr, "Usage: build_asteroids_packs <output_dir> [--ui-only]\n");
        return 1;
    }
    if (argc == 3) {
        build_ui(argv[1]);
        return 0;
    }
    build_core(argv[1]);
    build_space(argv[1]);
    for (uint32_t level = 0; level < 10; level++) {
        build_level(argv[1], level);
    }
    for (uint32_t textures = 10; textures <= 50; textures += 10) {
        build_noise(argv[1], textures);
    }
    build_ui(argv[1]);
    return 0;
}
