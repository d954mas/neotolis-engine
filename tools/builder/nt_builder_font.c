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

#define NT_FONT_MAX_CONTOURS 256

typedef struct {
    uint16_t segment_count;
    uint16_t quad_count; /* rest are lines */
} ContourStats;

typedef struct {
    int glyph_idx;
    uint16_t total_segments;
    uint16_t contour_count;
    uint16_t kern_count;
    uint32_t curve_data_size; /* contour data bytes for this glyph */
    ContourStats contours[NT_FONT_MAX_CONTOURS];
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
    NT_BUILD_ASSERT(glyph_count <= UINT16_MAX && "glyph count exceeds uint16 format limit (65535)");
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

    // #region First pass -- analyze contours and count kerns per glyph
    GlyphInfo *ginfo = (GlyphInfo *)calloc(glyph_count, sizeof(GlyphInfo));
    NT_BUILD_ASSERT(ginfo && "decode_font: glyph info alloc failed");

    uint32_t total_kerns = 0;
    uint32_t total_curve_data = 0;

    for (uint32_t i = 0; i < glyph_count; i++) {
        int glyph_idx = stbtt_FindGlyphIndex(&font, (int)codepoints[i]);
        NT_BUILD_ASSERT(glyph_idx != 0 && "codepoint not in font (D-09)");
        ginfo[i].glyph_idx = glyph_idx;

        /* Analyze contours: count segments, lines vs quads */
        stbtt_vertex *verts = NULL;
        int nv = stbtt_GetGlyphShape(&font, glyph_idx, &verts);
        uint16_t cc = 0;  /* contour count */
        uint16_t ts = 0;  /* total segments */
        uint32_t cdata = 2; /* start with contour_count uint16 */

        for (int v = 0; v < nv; v++) {
            switch (verts[v].type) {
            case STBTT_vmove:
                NT_BUILD_ASSERT(cc < NT_FONT_MAX_CONTOURS && "too many contours");
                if (cc > 0) {
                    /* Finalize previous contour size */
                    ContourStats *prev = &ginfo[i].contours[cc - 1];
                    uint32_t bitmask_bytes = ((uint32_t)prev->segment_count + 7) / 8;
                    cdata += 6 + bitmask_bytes; /* header: seg_count + start_x + start_y + bitmask */
                    cdata += (uint32_t)prev->quad_count * 8;
                    cdata += (uint32_t)(prev->segment_count - prev->quad_count) * 4;
                }
                cc++;
                ginfo[i].contours[cc - 1].segment_count = 0;
                ginfo[i].contours[cc - 1].quad_count = 0;
                break;
            case STBTT_vline:
                ginfo[i].contours[cc - 1].segment_count++;
                ts++;
                break;
            case STBTT_vcurve:
                ginfo[i].contours[cc - 1].segment_count++;
                ginfo[i].contours[cc - 1].quad_count++;
                ts++;
                break;
            case STBTT_vcubic:
                NT_BUILD_ASSERT(0 && "cubic curves not supported (CFF font?)");
                break;
            default:
                break;
            }
        }
        /* Finalize last contour */
        if (cc > 0) {
            ContourStats *last = &ginfo[i].contours[cc - 1];
            uint32_t bitmask_bytes = ((uint32_t)last->segment_count + 7) / 8;
            cdata += 6 + bitmask_bytes;
            cdata += (uint32_t)last->quad_count * 8;
            cdata += (uint32_t)(last->segment_count - last->quad_count) * 4;
        }

        stbtt_FreeShape(&font, verts);
        ginfo[i].contour_count = cc;
        ginfo[i].total_segments = ts;
        ginfo[i].curve_data_size = (ts > 0) ? cdata : 0;
        total_curve_data += ginfo[i].curve_data_size;

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
        if (kc > UINT16_MAX) {
            kc = UINT16_MAX;
        }
        ginfo[i].kern_count = (uint16_t)kc;
        total_kerns += kc;
    }
    // #endregion

    // #region Compute output size and allocate
    uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    uint32_t glyph_table_size = glyph_count * (uint32_t)sizeof(NtFontGlyphEntry);
    uint32_t data_blocks_size = (total_kerns * (uint32_t)sizeof(NtFontKernEntry)) + total_curve_data;
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
        ge->curve_count = ginfo[i].total_segments;
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

        /* Write kern entries sorted by right_glyph_index (j = glyph table index) */
        NtFontKernEntry *kern_ptr = (NtFontKernEntry *)(buffer + data_offset);
        uint32_t kw = 0;
        for (uint32_t j = 0; j < glyph_count && kw < ginfo[i].kern_count; j++) {
            if (j == i) {
                continue;
            }
            int other_idx = stbtt_FindGlyphIndex(&font, (int)codepoints[j]);
            int kern = stbtt_GetGlyphKernAdvance(&font, ginfo[i].glyph_idx, other_idx);
            if (kern != 0) {
                kern_ptr[kw].right_glyph_index = (uint16_t)j;
                kern_ptr[kw].value = (int16_t)kern;
                kw++;
            }
        }
        data_offset += (uint32_t)(ginfo[i].kern_count * sizeof(NtFontKernEntry));

        /* Write contour-based curve data */
        if (ginfo[i].total_segments > 0) {
            uint8_t *wp = buffer + data_offset; /* write pointer */

            /* contour_count */
            memcpy(wp, &ginfo[i].contour_count, 2);
            wp += 2;

            /* Get shape again for writing */
            stbtt_vertex *verts = NULL;
            int nv = stbtt_GetGlyphShape(&font, ginfo[i].glyph_idx, &verts);
            int vi = 0; /* vertex index */

            for (uint16_t ci = 0; ci < ginfo[i].contour_count; ci++) {
                ContourStats *cs = &ginfo[i].contours[ci];

                /* Find the moveto for this contour */
                while (vi < nv && verts[vi].type != STBTT_vmove) {
                    vi++;
                }
                NT_BUILD_ASSERT(vi < nv && "expected moveto");

                /* Contour header: segment_count + start_x + start_y */
                memcpy(wp, &cs->segment_count, 2);
                wp += 2;
                int16_t sx = verts[vi].x;
                int16_t sy = verts[vi].y;
                memcpy(wp, &sx, 2);
                wp += 2;
                memcpy(wp, &sy, 2);
                wp += 2;
                vi++; /* past moveto */

                /* Type bitmask: bit=1 for quad, bit=0 for line (LSB first) */
                uint32_t bitmask_bytes = ((uint32_t)cs->segment_count + 7) / 8;
                uint8_t *bitmask = wp;
                memset(bitmask, 0, bitmask_bytes);
                wp += bitmask_bytes;

                /* Build bitmask and write delta-encoded segment data.
                 * All coordinates are int16 deltas from previous chain endpoint
                 * (start_x/y for first segment, p2 of previous segment after). */
                int16_t prev_x = sx;
                int16_t prev_y = sy;
                for (uint16_t s = 0; s < cs->segment_count; s++) {
                    NT_BUILD_ASSERT(vi < nv && "unexpected end of vertices");
                    if (verts[vi].type == STBTT_vcurve) {
                        bitmask[s / 8] |= (uint8_t)(1U << (s % 8));
                        int16_t dp1x = (int16_t)(verts[vi].cx - prev_x);
                        int16_t dp1y = (int16_t)(verts[vi].cy - prev_y);
                        int16_t dp2x = (int16_t)(verts[vi].x - prev_x);
                        int16_t dp2y = (int16_t)(verts[vi].y - prev_y);
                        memcpy(wp, &dp1x, 2); wp += 2;
                        memcpy(wp, &dp1y, 2); wp += 2;
                        memcpy(wp, &dp2x, 2); wp += 2;
                        memcpy(wp, &dp2y, 2); wp += 2;
                        prev_x = verts[vi].x;
                        prev_y = verts[vi].y;
                    } else {
                        int16_t dp2x = (int16_t)(verts[vi].x - prev_x);
                        int16_t dp2y = (int16_t)(verts[vi].y - prev_y);
                        memcpy(wp, &dp2x, 2); wp += 2;
                        memcpy(wp, &dp2y, 2); wp += 2;
                        prev_x = verts[vi].x;
                        prev_y = verts[vi].y;
                    }
                    vi++;
                }
            }

            stbtt_FreeShape(&font, verts);
            data_offset += ginfo[i].curve_data_size;
        }
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
