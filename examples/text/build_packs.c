/*
 * Build font packs for the text rendering demo:
 *   text_base.ntpack -- Slug shaders + Latin/Cyrillic font resource
 *   text_cjk.ntpack  -- CJK font resource (progressive loading addon)
 *
 * Usage: build_text_packs <output_dir>
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

/* ---- Font path ---- */

/* LilitaOne-RussianChineseKo.ttf (SIL OFL license, D-21)
 * Fallback to system Arial for development if custom font not present. */
#ifndef FONT_PATH
#define FONT_PATH "assets/fonts/LilitaOne-RussianChineseKo.ttf"
#endif

#ifndef FALLBACK_FONT_PATH
#define FALLBACK_FONT_PATH "C:/Windows/Fonts/arial.ttf"
#endif

/* ---- Charsets ---- */

/* Cyrillic: basic Russian letters U+0410-U+044F (uppercase + lowercase) + Yo */
/* clang-format off */
static const char CYRILLIC_CHARSET[] =
    "\xd0\x90\xd0\x91\xd0\x92\xd0\x93\xd0\x94\xd0\x95\xd0\x96\xd0\x97"
    "\xd0\x98\xd0\x99\xd0\x9a\xd0\x9b\xd0\x9c\xd0\x9d\xd0\x9e\xd0\x9f"
    "\xd0\xa0\xd0\xa1\xd0\xa2\xd0\xa3\xd0\xa4\xd0\xa5\xd0\xa6\xd0\xa7"
    "\xd0\xa8\xd0\xa9\xd0\xaa\xd0\xab\xd0\xac\xd0\xad\xd0\xae\xd0\xaf"
    "\xd0\xb0\xd0\xb1\xd0\xb2\xd0\xb3\xd0\xb4\xd0\xb5\xd0\xb6\xd0\xb7"
    "\xd0\xb8\xd0\xb9\xd0\xba\xd0\xbb\xd0\xbc\xd0\xbd\xd0\xbe\xd0\xbf"
    "\xd1\x80\xd1\x81\xd1\x82\xd1\x83\xd1\x84\xd1\x85\xd1\x86\xd1\x87"
    "\xd1\x88\xd1\x89\xd1\x8a\xd1\x8b\xd1\x8c\xd1\x8d\xd1\x8e\xd1\x8f"
    "\xd0\x81\xd1\x91"; /* Yo yo */
/* clang-format on */

/* CJK: specific characters used in demo (D-24) */
/* Chinese: ni hao shi jie (Hello World) + Korean: annyeonghaseyo (Hello) */
/* clang-format off */
static const char CJK_CHARSET[] =
    "\xe4\xbd\xa0\xe5\xa5\xbd"               /* ni hao */
    "\xe4\xb8\x96\xe7\x95\x8c"               /* shi jie */
    "\xec\x95\x88\xeb\x85\x95"               /* an nyeong */
    "\xed\x95\x98\xec\x84\xb8\xec\x9a\x94"; /* ha se yo */
/* clang-format on */

/* ---- Path helper ---- */

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

/* ---- Resolve font path (prefer custom, fallback to system) ---- */

static bool s_has_primary_font;

static const char *resolve_font_path(void) {
    FILE *f = fopen(FONT_PATH, "rb");
    if (f) {
        (void)fclose(f);
        s_has_primary_font = true;
        return FONT_PATH;
    }
    (void)printf("WARNING: %s not found, falling back to %s\n", FONT_PATH, FALLBACK_FONT_PATH);
    (void)printf("NOTE: CJK pack will be skipped (fallback font lacks CJK glyphs)\n");
    s_has_primary_font = false;
    f = fopen(FALLBACK_FONT_PATH, "rb");
    if (f) {
        (void)fclose(f);
        return FALLBACK_FONT_PATH;
    }
    (void)fprintf(stderr, "ERROR: No font file available\n");
    return NULL;
}

/* ---- Main ---- */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_text_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];
    const char *header_dir = "examples/text/generated";
    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);

    (void)printf("=== Build Text Packs -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(cache_dir);
    MKDIR(header_dir);

    /* Resolve font */
    const char *font_path = resolve_font_path();
    if (!font_path) {
        return 1;
    }
    (void)printf("Using font: %s\n\n", font_path);

    // #region Pack 1: text_base.ntpack (Slug shaders + Latin/Cyrillic font)
    {
        (void)printf("--- Building text_base.ntpack ---\n");
        NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "text_base.ntpack"));
        if (!ctx) {
            (void)fprintf(stderr, "Failed to start text_base.ntpack\n");
            return 1;
        }
        nt_builder_set_header_dir(ctx, header_dir);
        nt_builder_set_cache_dir(ctx, cache_dir);

        /* Slug shaders */
        nt_builder_add_shader(ctx, "assets/shaders/slug_text.vert", NT_BUILD_SHADER_VERTEX);
        nt_builder_add_shader(ctx, "assets/shaders/slug_text.frag", NT_BUILD_SHADER_FRAGMENT);
        (void)printf("  Shaders added: 2\n");

        /* Latin + Cyrillic font resource */
        char full_charset[512];
        (void)snprintf(full_charset, sizeof(full_charset), "%s%s", NT_CHARSET_ASCII, CYRILLIC_CHARSET);

        nt_builder_add_font(ctx, font_path,
                            &(nt_font_opts_t){
                                .charset = full_charset,
                                .resource_name = "text/font_base",
                            });
        (void)printf("  Font (Latin+Cyrillic) added\n");

        nt_build_result_t r = nt_builder_finish_pack(ctx);
        nt_builder_free_pack(ctx);
        if (r != NT_BUILD_OK) {
            (void)fprintf(stderr, "text_base.ntpack failed: %d\n", r);
            return 1;
        }
        (void)printf("Built: text_base.ntpack\n\n");
    }
    // #endregion

    // #region Pack 2: text_cjk.ntpack (CJK font resource)
    if (s_has_primary_font) {
        (void)printf("--- Building text_cjk.ntpack ---\n");
        NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "text_cjk.ntpack"));
        if (!ctx) {
            (void)fprintf(stderr, "Failed to start text_cjk.ntpack\n");
            return 1;
        }
        nt_builder_set_header_dir(ctx, header_dir);
        nt_builder_set_cache_dir(ctx, cache_dir);

        /* CJK font resource */
        nt_builder_add_font(ctx, font_path,
                            &(nt_font_opts_t){
                                .charset = CJK_CHARSET,
                                .resource_name = "text/font_cjk",
                            });
        (void)printf("  Font (CJK) added\n");

        nt_build_result_t r = nt_builder_finish_pack(ctx);
        nt_builder_free_pack(ctx);
        if (r != NT_BUILD_OK) {
            (void)fprintf(stderr, "text_cjk.ntpack failed: %d\n", r);
            return 1;
        }
        (void)printf("Built: text_cjk.ntpack\n\n");
    } else {
        (void)printf("--- Skipping text_cjk.ntpack (fallback font lacks CJK) ---\n\n");
    }
    // #endregion

    /* Merge per-pack headers into combined text_assets.h */
    {
        char base_hdr[512];
        (void)snprintf(base_hdr, sizeof(base_hdr), "%s/text_base.h", header_dir);

        if (s_has_primary_font) {
            char cjk_hdr[512];
            (void)snprintf(cjk_hdr, sizeof(cjk_hdr), "%s/text_cjk.h", header_dir);
            const char *pack_headers[] = {base_hdr, cjk_hdr};
            char combined[512];
            (void)snprintf(combined, sizeof(combined), "%s/text_assets.h", header_dir);
            nt_builder_merge_headers(pack_headers, 2, combined);
        } else {
            /* Base-only: just copy the base header as the combined one */
            const char *pack_headers[] = {base_hdr};
            char combined[512];
            (void)snprintf(combined, sizeof(combined), "%s/text_assets.h", header_dir);
            nt_builder_merge_headers(pack_headers, 1, combined);
        }
        (void)printf("Generated: %s/text_assets.h\n", header_dir);
    }

    /* Print pack size summary */
    (void)printf("\n=== Pack Size Summary ===\n");
    static const char *pack_names[] = {"text_base.ntpack", "text_cjk.ntpack"};
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(pack_path(out_dir, pack_names[i]), "rb");
        if (f) {
            (void)fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            (void)fclose(f);
            (void)printf("  %-24s %8.1f KB\n", pack_names[i], (double)sz / 1024.0);
        }
    }

    (void)printf("\n=== Done ===\n");
    return 0;
}
