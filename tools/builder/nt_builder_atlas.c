/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_atlas_format.h"
/* clang-format on */

/* --- Atlas API stubs (fully implemented in Plan 02) --- */

void nt_builder_begin_atlas(NtBuilderContext *ctx, const char *name, const nt_atlas_opts_t *opts) {
    (void)ctx;
    (void)name;
    (void)opts;
    NT_BUILD_ASSERT(0 && "nt_builder_begin_atlas: not yet implemented");
}

void nt_builder_atlas_add(NtBuilderContext *ctx, const char *path, const char *name_override) {
    (void)ctx;
    (void)path;
    (void)name_override;
    NT_BUILD_ASSERT(0 && "nt_builder_atlas_add: not yet implemented");
}

void nt_builder_atlas_add_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const char *name) {
    (void)ctx;
    (void)rgba_pixels;
    (void)width;
    (void)height;
    (void)name;
    NT_BUILD_ASSERT(0 && "nt_builder_atlas_add_raw: not yet implemented");
}

void nt_builder_atlas_add_glob(NtBuilderContext *ctx, const char *pattern) {
    (void)ctx;
    (void)pattern;
    NT_BUILD_ASSERT(0 && "nt_builder_atlas_add_glob: not yet implemented");
}

void nt_builder_end_atlas(NtBuilderContext *ctx) {
    (void)ctx;
    NT_BUILD_ASSERT(0 && "nt_builder_end_atlas: not yet implemented");
}
