/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_font_format.h"
#include "hash/nt_hash.h"
#include "stb_truetype.h"
/* clang-format on */

#include <string.h>

/* --- Font processing: TTF -> NT_ASSET_FONT binary --- */

/* Maximum codepoints per font (builder-side limit) */
#define NT_FONT_MAX_CODEPOINTS 4096

/* --- UTF-8 decoder (builder-only, clarity over speed) --- */

static uint32_t utf8_decode(const uint8_t **p) {
    uint32_t c = **p;
    if (c < 0x80) {
        (*p)++;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        uint32_t cp = (c & 0x1FU) << 6U;
        cp |= (uint32_t)(*++(*p)) & 0x3FU;
        (*p)++;
        return cp;
    }
    if ((c & 0xF0) == 0xE0) {
        uint32_t cp = (c & 0x0FU) << 12U;
        cp |= ((uint32_t)(*++(*p)) & 0x3FU) << 6U;
        cp |= (uint32_t)(*++(*p)) & 0x3FU;
        (*p)++;
        return cp;
    }
    if ((c & 0xF8) == 0xF0) {
        uint32_t cp = (c & 0x07U) << 18U;
        cp |= ((uint32_t)(*++(*p)) & 0x3FU) << 12U;
        cp |= ((uint32_t)(*++(*p)) & 0x3FU) << 6U;
        cp |= (uint32_t)(*++(*p)) & 0x3FU;
        (*p)++;
        return cp;
    }
    (*p)++;
    return 0xFFFD; /* replacement character */
}

/* --- Charset parser: UTF-8 string -> sorted unique codepoints --- */

static int codepoint_compare(const void *a, const void *b) {
    uint32_t ca = *(const uint32_t *)a;
    uint32_t cb = *(const uint32_t *)b;
    if (ca < cb) {
        return -1;
    }
    if (ca > cb) {
        return 1;
    }
    return 0;
}

static uint32_t parse_charset(const char *charset_utf8, uint32_t *out_codepoints, uint32_t max_codepoints) {
    const uint8_t *p = (const uint8_t *)charset_utf8;
    uint32_t count = 0;

    while (*p != '\0') {
        uint32_t cp = utf8_decode(&p);
        if (cp == 0) {
            continue; /* skip null codepoint */
        }
        NT_BUILD_ASSERT(count < max_codepoints && "charset exceeds max codepoints");
        out_codepoints[count++] = cp;
    }

    /* Sort */
    if (count > 1) {
        qsort(out_codepoints, count, sizeof(uint32_t), codepoint_compare);
    }

    /* Remove duplicates in-place */
    if (count > 1) {
        uint32_t unique = 1;
        for (uint32_t i = 1; i < count; i++) {
            if (out_codepoints[i] != out_codepoints[i - 1]) {
                out_codepoints[unique++] = out_codepoints[i];
            }
        }
        count = unique;
    }

    return count;
}

/* --- Per-glyph shape info (first pass) --- */

typedef struct {
    int glyph_idx;
    uint16_t curve_count;
    uint8_t kern_count;
} GlyphInfo;

/* --- nt_builder_decode_font: TTF -> final NT_ASSET_FONT binary --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_decode_font(const char *path, const char *charset, uint8_t **out_data, uint32_t *out_size) {
    if (!path || !charset || !out_data || !out_size) {
        return NT_BUILD_ERR_VALIDATION;
    }

    // #region Parse charset
    uint32_t codepoints[NT_FONT_MAX_CODEPOINTS];
    uint32_t glyph_count = parse_charset(charset, codepoints, NT_FONT_MAX_CODEPOINTS);
    NT_BUILD_ASSERT(glyph_count > 0 && "charset is empty");
    // #endregion

    // #region Init stb_truetype
    uint32_t file_size = 0;
    char *file_data = nt_builder_read_file(path, &file_size);
    NT_BUILD_ASSERT(file_data && "decode_font: failed to read TTF file");

    stbtt_fontinfo font;
    int ok = stbtt_InitFont(&font, (const unsigned char *)file_data, stbtt_GetFontOffsetForIndex((const unsigned char *)file_data, 0));
    NT_BUILD_ASSERT(ok && "decode_font: stbtt_InitFont failed");
    // #endregion

    // #region GPOS warning (BLD-08, D-14)
    if (font.gpos != 0 && font.kern == 0) {
        NT_LOG_WARN("%s: GPOS table present but no kern table -- kerning may be incomplete", path);
    }
    // #endregion

    // #region Extract font-level metrics
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

    /* Compute units_per_em from stbtt_ScaleForPixelHeight */
    float scale = stbtt_ScaleForPixelHeight(&font, 1.0F);
    uint16_t units_per_em = (uint16_t)((1.0F / scale) + 0.5F);
    // #endregion

    // #region First pass -- count curves and kerns per glyph
    GlyphInfo *ginfo = (GlyphInfo *)calloc(glyph_count, sizeof(GlyphInfo));
    NT_BUILD_ASSERT(ginfo && "decode_font: glyph info alloc failed");

    uint32_t total_curves = 0;
    uint32_t total_kerns = 0;

    for (uint32_t i = 0; i < glyph_count; i++) {
        int glyph_idx = stbtt_FindGlyphIndex(&font, (int)codepoints[i]);
        NT_BUILD_ASSERT(glyph_idx != 0 && "codepoint not in font (D-09)");
        ginfo[i].glyph_idx = glyph_idx;

        /* Count curves */
        stbtt_vertex *verts = NULL;
        int nv = stbtt_GetGlyphShape(&font, glyph_idx, &verts);
        uint16_t curves = 0;
        for (int v = 0; v < nv; v++) {
            switch (verts[v].type) {
            case STBTT_vmove:
                break;
            case STBTT_vline: /* fall through — both line and curve produce one NtFontCurve */
            case STBTT_vcurve:
                curves++;
                break;
            case STBTT_vcubic:
                NT_BUILD_ASSERT(0 && "cubic curves not supported (CFF font?)");
                break;
            default:
                break;
            }
        }
        stbtt_FreeShape(&font, verts);
        ginfo[i].curve_count = curves;
        total_curves += curves;

        /* Count kern pairs for this glyph (left glyph = current, right = other) */
        uint32_t kc = 0;
        for (uint32_t j = 0; j < glyph_count; j++) {
            if (j == i) {
                continue;
            }
            int other_idx = stbtt_FindGlyphIndex(&font, (int)codepoints[j]);
            int kern = stbtt_GetGlyphKernAdvance(&font, glyph_idx, other_idx);
            if (kern != 0) {
                kc++;
            }
        }
        /* Cap at 255 (kern_count is uint8) */
        if (kc > 255) {
            kc = 255;
        }
        ginfo[i].kern_count = (uint8_t)kc;
        total_kerns += kc;
    }
    // #endregion

    // #region Compute output size and allocate
    uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    uint32_t glyph_table_size = glyph_count * (uint32_t)sizeof(NtFontGlyphEntry);
    uint32_t data_blocks_size = (total_kerns * (uint32_t)sizeof(NtFontKernEntry)) + (total_curves * (uint32_t)sizeof(NtFontCurve));
    uint32_t total_size = header_size + glyph_table_size + data_blocks_size;

    uint8_t *buffer = (uint8_t *)calloc(total_size, 1);
    NT_BUILD_ASSERT(buffer && "decode_font: output buffer alloc failed");
    // #endregion

    // #region Build header
    NtFontAssetHeader *hdr = (NtFontAssetHeader *)buffer;
    hdr->magic = NT_FONT_MAGIC;
    hdr->version = NT_FONT_VERSION;
    hdr->glyph_count = (uint16_t)glyph_count;
    hdr->units_per_em = units_per_em;
    hdr->ascent = (int16_t)ascent;
    hdr->descent = (int16_t)descent;
    hdr->line_gap = (int16_t)line_gap;
    // #endregion

    // #region Build glyph entries and data blocks
    NtFontGlyphEntry *entries = (NtFontGlyphEntry *)(buffer + header_size);
    uint32_t data_offset = header_size + glyph_table_size;

    for (uint32_t i = 0; i < glyph_count; i++) {
        NtFontGlyphEntry *ge = &entries[i];
        memset(ge, 0, sizeof(*ge));
        ge->codepoint = codepoints[i];
        ge->data_offset = data_offset;
        ge->curve_count = ginfo[i].curve_count;
        ge->kern_count = ginfo[i].kern_count;

        /* Metrics */
        int advance = 0;
        int lsb = 0;
        stbtt_GetGlyphHMetrics(&font, ginfo[i].glyph_idx, &advance, &lsb);
        ge->advance = (int16_t)advance;

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        int has_box = stbtt_GetGlyphBox(&font, ginfo[i].glyph_idx, &x0, &y0, &x1, &y1);
        if (has_box) {
            ge->bbox_x0 = (int16_t)x0;
            ge->bbox_y0 = (int16_t)y0;
            ge->bbox_x1 = (int16_t)x1;
            ge->bbox_y1 = (int16_t)y1;
        }
        /* else: all bbox fields remain 0 (space, empty glyph) */

        /* Write kern entries sorted by right_codepoint */
        NtFontKernEntry *kern_ptr = (NtFontKernEntry *)(buffer + data_offset);
        uint32_t kw = 0;
        for (uint32_t j = 0; j < glyph_count && kw < ginfo[i].kern_count; j++) {
            if (j == i) {
                continue;
            }
            int other_idx = stbtt_FindGlyphIndex(&font, (int)codepoints[j]);
            int kern = stbtt_GetGlyphKernAdvance(&font, ginfo[i].glyph_idx, other_idx);
            if (kern != 0) {
                kern_ptr[kw].right_codepoint = codepoints[j];
                kern_ptr[kw].value = (int16_t)kern;
                kern_ptr[kw]._pad = 0;
                kw++;
            }
        }
        data_offset += (uint32_t)(ginfo[i].kern_count * sizeof(NtFontKernEntry));

        /* Write curve entries */
        NtFontCurve *curve_ptr = (NtFontCurve *)(buffer + data_offset);
        stbtt_vertex *verts = NULL;
        int nv = stbtt_GetGlyphShape(&font, ginfo[i].glyph_idx, &verts);
        uint32_t cw = 0;
        float prev_x = 0.0F;
        float prev_y = 0.0F;

        for (int v = 0; v < nv; v++) {
            switch (verts[v].type) {
            case STBTT_vmove:
                prev_x = (float)verts[v].x;
                prev_y = (float)verts[v].y;
                break;
            case STBTT_vline: {
                /* Promote to degenerate quadratic: p1 = midpoint(p0, p2) */
                float p2x = (float)verts[v].x;
                float p2y = (float)verts[v].y;
                float p1x = (prev_x + p2x) * 0.5F;
                float p1y = (prev_y + p2y) * 0.5F;
                curve_ptr[cw].p0x = nt_builder_float32_to_float16(prev_x);
                curve_ptr[cw].p0y = nt_builder_float32_to_float16(prev_y);
                curve_ptr[cw].p1x = nt_builder_float32_to_float16(p1x);
                curve_ptr[cw].p1y = nt_builder_float32_to_float16(p1y);
                curve_ptr[cw].p2x = nt_builder_float32_to_float16(p2x);
                curve_ptr[cw].p2y = nt_builder_float32_to_float16(p2y);
                cw++;
                prev_x = p2x;
                prev_y = p2y;
                break;
            }
            case STBTT_vcurve: {
                /* Quadratic Bezier: p0 = prev, p1 = (cx,cy), p2 = (x,y) */
                float p1x = (float)verts[v].cx;
                float p1y = (float)verts[v].cy;
                float p2x = (float)verts[v].x;
                float p2y = (float)verts[v].y;
                curve_ptr[cw].p0x = nt_builder_float32_to_float16(prev_x);
                curve_ptr[cw].p0y = nt_builder_float32_to_float16(prev_y);
                curve_ptr[cw].p1x = nt_builder_float32_to_float16(p1x);
                curve_ptr[cw].p1y = nt_builder_float32_to_float16(p1y);
                curve_ptr[cw].p2x = nt_builder_float32_to_float16(p2x);
                curve_ptr[cw].p2y = nt_builder_float32_to_float16(p2y);
                cw++;
                prev_x = p2x;
                prev_y = p2y;
                break;
            }
            default:
                break;
            }
        }
        stbtt_FreeShape(&font, verts);
        data_offset += (uint32_t)(ginfo[i].curve_count * sizeof(NtFontCurve));
    }
    // #endregion

    // #region Build log (BLD-07, D-13)
    {
        char packed_str[16];
        char ttf_str[16];
        nt_format_size(total_size, packed_str, sizeof(packed_str));
        nt_format_size(file_size, ttf_str, sizeof(ttf_str));
        NT_LOG_INFO("  FONT %s: %u glyphs, %s packed (TTF %s)", path, glyph_count, packed_str, ttf_str);
    }
    // #endregion

    // #region Cleanup and return
    free(ginfo);
    free(file_data);
    *out_data = buffer;
    *out_size = total_size;
    return NT_BUILD_OK;
    // #endregion
}

/* --- nt_builder_add_font: public API --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_add_font(NtBuilderContext *ctx, const char *path, const nt_font_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && path && "invalid add_font args");
    NT_BUILD_ASSERT(opts && opts->charset && "charset is required (D-05/D-09)");

    /* Build logical path for resource_id */
    char logical_path[1024];
    if (opts->resource_name) {
        (void)snprintf(logical_path, sizeof(logical_path), "%s/%s", path, opts->resource_name);
    } else {
        (void)snprintf(logical_path, sizeof(logical_path), "%s", path);
    }

    /* Resolve actual file path via asset roots */
    char *resolved_path = nt_builder_find_file(path, NULL, ctx);
    const char *decode_path = resolved_path ? resolved_path : path;

    /* Allocate font-specific data */
    NtBuildFontData *fd = (NtBuildFontData *)calloc(1, sizeof(NtBuildFontData));
    NT_BUILD_ASSERT(fd && "add_font: font data alloc failed");
    fd->charset = strdup(opts->charset);
    NT_BUILD_ASSERT(fd->charset && "add_font: strdup failed");

    /* Decode TTF -> final NT_ASSET_FONT binary */
    uint8_t *data = NULL;
    uint32_t size = 0;
    nt_build_result_t r = nt_builder_decode_font(decode_path, opts->charset, &data, &size);
    free(resolved_path);
    NT_BUILD_ASSERT(r == NT_BUILD_OK && "add_font: decode failed");

    /* Hash decoded data and register deferred entry */
    uint64_t hash = nt_hash64(data, size).value;
    nt_builder_add_entry(ctx, logical_path, NT_BUILD_ASSET_FONT, fd, data, size, hash);
}
