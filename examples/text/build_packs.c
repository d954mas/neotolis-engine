/*
 * Build font packs for the text rendering demo:
 *   text_base.ntpack -- Slug shaders + Latin/Cyrillic font resource
 *   text_cjk.ntpack  -- CJK font resource (progressive loading addon)
 *
 * Usage: build_text_packs <output_dir>
 */

#include "nt_builder.h"

#include "stb_truetype.h"

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

/* ---- UTF-8 encoder for charset generation ---- */

static uint32_t encode_utf8(char *buf, uint32_t cp) {
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* CJK charset buffer — only includes codepoints actually present in the font.
 * Scans CJK Unified, Hangul, Kana, and punctuation ranges via stbtt. */
#define CJK_CHARSET_BUF_SIZE (128 * 1024)
static char s_cjk_charset[CJK_CHARSET_BUF_SIZE];

/* Add codepoints from [start, end] that exist in the font to the charset buffer. */
static uint32_t charset_add_range_filtered(char *buf, uint32_t buf_size, uint32_t start, uint32_t end, const stbtt_fontinfo *font, uint32_t *glyph_count) {
    uint32_t off = 0;
    for (uint32_t cp = start; cp <= end; cp++) {
        if (stbtt_FindGlyphIndex(font, (int)cp) == 0) {
            continue; /* not in font */
        }
        char tmp[4];
        uint32_t n = encode_utf8(tmp, cp);
        if (off + n + 1 > buf_size) {
            break;
        }
        memcpy(buf + off, tmp, n);
        off += n;
        (*glyph_count)++;
    }
    buf[off] = '\0';
    return off;
}

static void build_cjk_charset(const char *font_path) {
    /* Load font for cmap scanning */
    FILE *f = fopen(font_path, "rb");
    if (!f) {
        return;
    }
    (void)fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc((size_t)sz);
    (void)fread(data, 1, (size_t)sz, f);
    (void)fclose(f);

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, data, 0)) {
        free(data);
        return;
    }

    uint32_t off = 0;
    uint32_t count = 0;
    /* CJK Unified Ideographs */
    off += charset_add_range_filtered(s_cjk_charset + off, CJK_CHARSET_BUF_SIZE - off, 0x4E00, 0x9FFF, &font, &count);
    /* CJK Unified Ideographs Extension A */
    off += charset_add_range_filtered(s_cjk_charset + off, CJK_CHARSET_BUF_SIZE - off, 0x3400, 0x4DBF, &font, &count);
    /* Hangul Syllables */
    off += charset_add_range_filtered(s_cjk_charset + off, CJK_CHARSET_BUF_SIZE - off, 0xAC00, 0xD7AF, &font, &count);
    /* Hangul Jamo */
    off += charset_add_range_filtered(s_cjk_charset + off, CJK_CHARSET_BUF_SIZE - off, 0x1100, 0x11FF, &font, &count);
    /* CJK Symbols and Punctuation */
    off += charset_add_range_filtered(s_cjk_charset + off, CJK_CHARSET_BUF_SIZE - off, 0x3000, 0x303F, &font, &count);
    /* Hiragana + Katakana */
    off += charset_add_range_filtered(s_cjk_charset + off, CJK_CHARSET_BUF_SIZE - off, 0x3040, 0x30FF, &font, &count);

    free(data);
    (void)printf("  CJK charset: %u glyphs found in font (%u bytes)\n", count, off);
}

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

        /* Generate full CJK charset — only codepoints present in the font */
        build_cjk_charset(font_path);

        NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "text_cjk.ntpack"));
        if (!ctx) {
            (void)fprintf(stderr, "Failed to start text_cjk.ntpack\n");
            return 1;
        }
        nt_builder_set_header_dir(ctx, header_dir);
        nt_builder_set_cache_dir(ctx, cache_dir);

        /* CJK font resource — all glyphs from CJK+Hangul+Kana ranges */
        nt_builder_add_font(ctx, font_path,
                            &(nt_font_opts_t){
                                .charset = s_cjk_charset,
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
