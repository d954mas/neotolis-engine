#include "nt_builder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const s_sky_faces[6] = {"PositiveX", "NegativeX", "PositiveY", "NegativeY", "PositiveZ", "NegativeZ"};

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

static void build_core(const char *directory) {
    NtBuilderContext *ctx = start_pack(directory, "asteroids_core");
    nt_builder_add_font(ctx, "examples/asteroids/raw/DejaVuSansMono.ttf", &(nt_font_opts_t){.charset = NT_CHARSET_ASCII, .resource_name = "asteroids/font"});
    const char *const programs[] = {"rock", "planet", "sky", "noise"};
    const char *const vertices[] = {"scene", "planet", "sky", "noise"};
    for (uint32_t i = 0; i < 4; i++) {
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
    nt_tex_opts_t tex = nt_tex_opts_defaults();
    tex.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_NEAREST;
    tex.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    tex.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    tex.compress = nt_tex_compress_etc1s_default();
    /* Basis has no sRGB targets yet, so planet.frag decodes Mars to linear after sampling. */
    nt_builder_add_texture(ctx, "examples/asteroids/raw/Mars.jpg", &tex);
    nt_builder_rename(ctx, "examples/asteroids/raw/Mars.jpg", "asteroids/mars");
    for (uint32_t face = 0; face < 6; face++) {
        char name[64];
        (void)snprintf(path, sizeof(path), "examples/asteroids/raw/Galaxy/%s.jpg", s_sky_faces[face]);
        (void)snprintf(name, sizeof(name), "asteroids/sky/%u", face);
        nt_builder_add_texture(ctx, path, &tex);
        nt_builder_rename(ctx, path, name);
    }
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
    if (argc != 2) {
        (void)fprintf(stderr, "Usage: build_asteroids_packs <output_dir>\n");
        return 1;
    }
    build_core(argv[1]);
    build_space(argv[1]);
    build_ui(argv[1]);
    return 0;
}
