#include "nt_builder.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        (void)fprintf(stderr, "Usage: build_basis_fixture <pack-path> <cache-dir>\n");
        return 1;
    }
    NtBuilderContext *ctx = nt_builder_start_pack(argv[1]);
    nt_builder_set_cache_dir(ctx, argv[2]);
    nt_builder_set_threads(ctx, 1);
    const char *names[] = {"etc1s_rgb", "etc1s_alpha", "uastc_rgb", "uastc_alpha"};
    for (uint32_t i = 0; i < 4; i++) {
        uint8_t pixels[96 * 64 * 4];
        for (uint32_t y = 0; y < 64; y++) {
            for (uint32_t x = 0; x < 96; x++) {
                uint8_t *p = &pixels[(((size_t)y * 96) + x) * 4];
                p[0] = (uint8_t)(32U + (x * 160U / 96U));
                p[1] = (uint8_t)(24U + (y * 120U / 64U));
                p[2] = 64;
                p[3] = (i & 1U) ? (uint8_t)(40U + (x * 180U / 96U)) : 255;
            }
        }
        nt_tex_compress_opts_t compression = i < 2 ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
        nt_tex_opts_t opts = nt_tex_opts_defaults();
        opts.format = (i & 1U) ? NT_TEXTURE_FORMAT_RGBA8 : NT_TEXTURE_FORMAT_RGB8;
        opts.compress = &compression;
        opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_NEAREST_MIPMAP_NEAREST;
        opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_NEAREST;
        opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
        opts.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
        nt_builder_add_texture_raw(ctx, pixels, 96, 64, names[i], &opts);
    }
    const nt_build_result_t result = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    (void)printf("BASIS_FIXTURE builder_version=%u result=%d\n", NT_BUILDER_VERSION, (int)result);
    return result == NT_BUILD_OK ? 0 : 1;
}
