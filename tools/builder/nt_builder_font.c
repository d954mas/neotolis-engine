/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_font_format.h"
#include "hash/nt_hash.h"
#include "stb_truetype.h"
/* clang-format on */

#include <string.h>

/* --- Font processing: TTF -> NT_ASSET_FONT binary --- */
/* Full implementation in Plan 02 */

nt_build_result_t nt_builder_decode_font(const char *path, const char *charset, uint8_t **out_data, uint32_t *out_size) {
    (void)path;
    (void)charset;
    (void)out_data;
    (void)out_size;
    NT_BUILD_ASSERT(0 && "decode_font: not yet implemented");
    return NT_BUILD_ERR_VALIDATION;
}

void nt_builder_add_font(NtBuilderContext *ctx, const char *path, const nt_font_opts_t *opts) {
    (void)ctx;
    (void)path;
    (void)opts;
    NT_BUILD_ASSERT(0 && "add_font: not yet implemented");
}

void nt_builder_add_fonts(NtBuilderContext *ctx, const char *pattern, const nt_font_opts_t *opts) {
    (void)ctx;
    (void)pattern;
    (void)opts;
    NT_BUILD_ASSERT(0 && "add_fonts: not yet implemented");
}
