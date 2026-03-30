/* font_bench.c — Font format size benchmark
 * Compares NT font format vs raw TTF (raw + gzipped).
 * Usage: font_bench <font.ttf> [font2.ttf ...]
 */

/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_font_format.h"
#include "stb_truetype.h"
#include "miniz.h"
/* clang-format on */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── UTF-8 encoder ─────────────────────────────────────────── */

static char *utf8_encode_cp(char *p, uint32_t cp) {
    if (cp < 0x80) {
        *p++ = (char)cp;
    } else if (cp < 0x800) {
        *p++ = (char)(0xC0 | (cp >> 6));
        *p++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *p++ = (char)(0xE0 | (cp >> 12));
        *p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *p++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *p++ = (char)(0xF0 | (cp >> 18));
        *p++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *p++ = (char)(0x80 | (cp & 0x3F));
    }
    return p;
}

/* ── Charset builder: probe font, emit only present codepoints ─ */

typedef struct {
    char *utf8;       /* heap-allocated UTF-8 string */
    uint32_t count;   /* codepoints included */
    uint32_t skipped; /* codepoints not in font */
} CharsetResult;

static CharsetResult build_charset(const stbtt_fontinfo *font,
                                   const uint32_t *ranges, uint32_t range_count) {
    /* Worst case: 4 bytes per codepoint + null */
    uint32_t max_cps = 0;
    for (uint32_t i = 0; i < range_count; i += 2) {
        max_cps += (ranges[i + 1] - ranges[i]) + 1;
    }

    char *buf = (char *)malloc(max_cps * 4 + 1);
    char *p = buf;
    uint32_t count = 0;
    uint32_t skipped = 0;

    for (uint32_t i = 0; i < range_count; i += 2) {
        for (uint32_t cp = ranges[i]; cp <= ranges[i + 1]; cp++) {
            if (stbtt_FindGlyphIndex(font, (int)cp) != 0) {
                p = utf8_encode_cp(p, cp);
                count++;
            } else {
                skipped++;
            }
        }
    }
    *p = '\0';

    CharsetResult r = {.utf8 = buf, .count = count, .skipped = skipped};
    return r;
}

/* ── Gzip helper (miniz, best compression) ─────────────────── */

static uint32_t gz_size(const uint8_t *data, uint32_t size) {
    mz_ulong bound = mz_compressBound((mz_ulong)size);
    uint8_t *tmp = (uint8_t *)malloc(bound);
    mz_ulong out = bound;
    int r = mz_compress2(tmp, &out, data, (mz_ulong)size, MZ_BEST_COMPRESSION);
    free(tmp);
    return (r == MZ_OK) ? (uint32_t)out : 0;
}

/* ── Font binary analysis ──────────────────────────────────── */

typedef struct {
    uint32_t total;
    uint32_t header;
    uint32_t glyph_table;
    uint32_t kern_data;
    uint32_t curve_data;
    uint16_t glyph_count;
    uint32_t total_curves;
    uint32_t total_kerns;
} FontBreakdown;

static FontBreakdown analyze(const uint8_t *data, uint32_t size) {
    const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)data;
    const NtFontGlyphEntry *entries =
        (const NtFontGlyphEntry *)(data + sizeof(NtFontAssetHeader));

    FontBreakdown b = {0};
    b.total = size;
    b.header = (uint32_t)sizeof(NtFontAssetHeader);
    b.glyph_count = hdr->glyph_count;
    b.glyph_table = (uint32_t)(hdr->glyph_count * sizeof(NtFontGlyphEntry));

    for (uint16_t i = 0; i < hdr->glyph_count; i++) {
        b.total_curves += entries[i].curve_count;
        b.total_kerns += entries[i].kern_count;
    }
    b.kern_data = (uint32_t)(b.total_kerns * sizeof(NtFontKernEntry));
    b.curve_data = b.total - b.header - b.glyph_table - b.kern_data;
    return b;
}

/* ── Per-charset benchmark row ─────────────────────────────── */

static void bench_charset(const char *label, const char *font_path,
                          const uint8_t *ttf_data, uint32_t ttf_size,
                          const char *charset, uint32_t charset_count,
                          uint32_t charset_skipped) {
    if (charset_count == 0) {
        printf("  %-22s  -- no glyphs found, skipped\n", label);
        return;
    }

    uint8_t *nt_data = NULL;
    uint32_t nt_size = 0;
    nt_build_result_t r = nt_builder_decode_font(font_path, charset, &nt_data, &nt_size);
    if (r != NT_BUILD_OK) {
        printf("  %-22s  -- decode failed (err=%d)\n", label, r);
        return;
    }

    FontBreakdown b = analyze(nt_data, nt_size);
    uint32_t ttf_gz = gz_size(ttf_data, ttf_size);
    uint32_t nt_gz = gz_size(nt_data, nt_size);

    /* Row 1: sizes */
    printf("  %-22s %5u glyphs", label, b.glyph_count);
    if (charset_skipped > 0) {
        printf(" (%u missing)", charset_skipped);
    }
    printf("\n");

    printf("    TTF:   %8u raw  %8u gz\n", ttf_size, ttf_gz);
    printf("    NT:    %8u raw  %8u gz    (%.2fx raw, %.2fx gz)\n",
           nt_size, nt_gz,
           (double)nt_size / (double)ttf_size,
           (double)nt_gz / (double)ttf_gz);

    /* Row 2: component breakdown */
    printf("    breakdown:  header %u  |  entries %u (%.0f%%)  |  kerns %u (%.0f%%)  |  curves %u (%.0f%%)\n",
           b.header,
           b.glyph_table, 100.0 * b.glyph_table / b.total,
           b.kern_data, 100.0 * b.kern_data / b.total,
           b.curve_data, 100.0 * b.curve_data / b.total);

    /* Row 3: per-glyph stats */
    printf("    per glyph:  %.1f curves avg (%.1f B)  |  %.1f kerns avg (%.1f B)  |  entry 24 B\n",
           (double)b.total_curves / b.glyph_count,
           (double)b.curve_data / b.glyph_count,
           (double)b.total_kerns / b.glyph_count,
           (double)b.kern_data / b.glyph_count);

    /* Row 4: bytes per glyph total */
    printf("    total B/glyph: %.1f raw  |  %.1f gz\n",
           (double)nt_size / b.glyph_count,
           (double)nt_gz / b.glyph_count);

    printf("\n");
    free(nt_data);
}

/* ── Charset definitions (codepoint ranges) ────────────────── */

/* ASCII printable: U+0020 - U+007E */
static const uint32_t RANGE_ASCII[] = {0x0020, 0x007E};

/* Latin Extended (basic + supplement + extended-A) */
static const uint32_t RANGE_LATIN_EXT[] = {
    0x0020, 0x007E, /* Basic Latin */
    0x00A0, 0x00FF, /* Latin-1 Supplement */
    0x0100, 0x017F, /* Latin Extended-A */
};

/* Cyrillic basic */
static const uint32_t RANGE_CYRILLIC[] = {
    0x0400, 0x04FF, /* Cyrillic */
};

/* Latin + Cyrillic combined */
static const uint32_t RANGE_LATIN_CYRILLIC[] = {
    0x0020, 0x007E, /* Basic Latin */
    0x00A0, 0x00FF, /* Latin-1 Supplement */
    0x0100, 0x017F, /* Latin Extended-A */
    0x0400, 0x04FF, /* Cyrillic */
};

/* CJK Unified Ideographs (common subset: first 2000) */
static const uint32_t RANGE_CJK_2K[] = {
    0x4E00, 0x55B5, /* ~2000 most common CJK ideographs */
};

/* CJK broader: 5000 ideographs */
static const uint32_t RANGE_CJK_5K[] = {
    0x4E00, 0x6187, /* ~5000 CJK ideographs */
};

/* Full game charset: Latin + Cyrillic + CJK 2K */
static const uint32_t RANGE_GAME_FULL[] = {
    0x0020, 0x007E, /* Basic Latin */
    0x00A0, 0x00FF, /* Latin-1 Supplement */
    0x0400, 0x04FF, /* Cyrillic */
    0x4E00, 0x55B5, /* CJK 2K */
};

#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

/* ── Main ──────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: font_bench <font.ttf> [font2.ttf ...]\n");
        return 1;
    }

    for (int fi = 1; fi < argc; fi++) {
        const char *path = argv[fi];
        uint32_t file_size = 0;
        char *file_data = nt_builder_read_file(path, &file_size);
        if (!file_data) {
            (void)fprintf(stderr, "ERROR: cannot read %s\n", path);
            continue;
        }

        /* Init stb_truetype for charset probing */
        stbtt_fontinfo font;
        int ok = stbtt_InitFont(&font,
                                (const unsigned char *)file_data,
                                stbtt_GetFontOffsetForIndex((const unsigned char *)file_data, 0));
        if (!ok) {
            (void)fprintf(stderr, "ERROR: stbtt_InitFont failed for %s\n", path);
            free(file_data);
            continue;
        }

        printf("══════════════════════════════════════════════════════════════\n");
        printf("  Font: %s  (%u bytes)\n", path, file_size);
        printf("══════════════════════════════════════════════════════════════\n\n");

        /* Charsets to test */
        struct {
            const char *name;
            const uint32_t *ranges;
            uint32_t range_count;
        } charsets[] = {
            {"ASCII (95)",          RANGE_ASCII,          COUNTOF(RANGE_ASCII)},
            {"Latin Extended",      RANGE_LATIN_EXT,      COUNTOF(RANGE_LATIN_EXT)},
            {"Cyrillic",            RANGE_CYRILLIC,       COUNTOF(RANGE_CYRILLIC)},
            {"Latin+Cyrillic",      RANGE_LATIN_CYRILLIC, COUNTOF(RANGE_LATIN_CYRILLIC)},
            {"CJK 2K",             RANGE_CJK_2K,         COUNTOF(RANGE_CJK_2K)},
            {"CJK 5K",             RANGE_CJK_5K,         COUNTOF(RANGE_CJK_5K)},
            {"Game Full (L+C+CJK)", RANGE_GAME_FULL,     COUNTOF(RANGE_GAME_FULL)},
        };

        for (uint32_t i = 0; i < COUNTOF(charsets); i++) {
            CharsetResult cs = build_charset(&font, charsets[i].ranges, charsets[i].range_count);
            bench_charset(charsets[i].name, path,
                          (const uint8_t *)file_data, file_size,
                          cs.utf8, cs.count, cs.skipped);
            free(cs.utf8);
        }

        free(file_data);
    }

    return 0;
}
