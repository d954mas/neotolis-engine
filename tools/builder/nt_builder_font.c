/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_font_format.h"
#include "hash/nt_hash.h"
#include "stb_truetype.h"
/* clang-format on */

#include <string.h>

/* --- Font processing: TTF -> NT_ASSET_FONT binary --- */

/* Format limit: uint16 glyph_count = 65535 max codepoints */
#define NT_FONT_MAX_CODEPOINTS UINT16_MAX

/* --- UTF-8 decoder (builder-only, clarity over speed) --- */

/* Check continuation byte: must be 10xxxxxx */
#define UTF8_CONT(b) (((b) & 0xC0) == 0x80)

/* Bitmask size for N segments, rounded up to 2-byte alignment.
 * Ensures int16 data after bitmask is naturally aligned. */
#define BITMASK_BYTES(n) ((((uint32_t)(n) + 7) / 8 + 1) & ~1U)

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t utf8_decode(const uint8_t **p) {
    uint32_t c = **p;
    if (c < 0x80) {
        (*p)++;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        NT_BUILD_ASSERT((*p)[1] != 0 && UTF8_CONT((*p)[1]) && "malformed/truncated UTF-8 (2-byte)");
        uint32_t cp = (c & 0x1FU) << 6U;
        cp |= (uint32_t)(*++(*p)) & 0x3FU;
        (*p)++;
        return cp;
    }
    if ((c & 0xF0) == 0xE0) {
        /* Check each byte sequentially — each assert proves the next access is safe */
        NT_BUILD_ASSERT((*p)[1] != 0 && "truncated UTF-8 (3-byte): missing byte 2");
        NT_BUILD_ASSERT((*p)[2] != 0 && "truncated UTF-8 (3-byte): missing byte 3");
        NT_BUILD_ASSERT(UTF8_CONT((*p)[1]) && UTF8_CONT((*p)[2]) && "malformed UTF-8 (3-byte)");
        uint32_t cp = (c & 0x0FU) << 12U;
        cp |= ((uint32_t)(*++(*p)) & 0x3FU) << 6U;
        cp |= (uint32_t)(*++(*p)) & 0x3FU;
        (*p)++;
        return cp;
    }
    if ((c & 0xF8) == 0xF0) {
        NT_BUILD_ASSERT((*p)[1] != 0 && "truncated UTF-8 (4-byte): missing byte 2");
        NT_BUILD_ASSERT((*p)[2] != 0 && "truncated UTF-8 (4-byte): missing byte 3");
        NT_BUILD_ASSERT((*p)[3] != 0 && "truncated UTF-8 (4-byte): missing byte 4");
        NT_BUILD_ASSERT(UTF8_CONT((*p)[1]) && UTF8_CONT((*p)[2]) && UTF8_CONT((*p)[3]) && "malformed UTF-8 (4-byte)");
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

/* --- Bulk GPOS kern pair extraction (replaces O(N²) per-pair lookup) --- */

/* Big-endian readers (same as stb_truetype internals) */
static uint16_t gpos_u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static int16_t gpos_s16(const uint8_t *p) { return (int16_t)gpos_u16(p); }
static uint32_t gpos_u32(const uint8_t *p) { return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }

/* Get glyph class from ClassDef table (same logic as stbtt__GetGlyphClass) */
static int gpos_get_class(const uint8_t *class_def, uint16_t glyph_id) {
    uint16_t fmt = gpos_u16(class_def);
    if (fmt == 1) {
        uint16_t start = gpos_u16(class_def + 2);
        uint16_t count = gpos_u16(class_def + 4);
        if (glyph_id >= start && glyph_id < start + count) {
            return gpos_u16(class_def + 6 + (glyph_id - start) * 2);
        }
        return 0;
    }
    if (fmt == 2) {
        uint16_t range_count = gpos_u16(class_def + 2);
        const uint8_t *ranges = class_def + 4;
        /* Binary search over class ranges */
        int lo = 0;
        int hi = range_count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint8_t *r = ranges + mid * 6;
            uint16_t range_start = gpos_u16(r);
            uint16_t range_end = gpos_u16(r + 2);
            if (glyph_id < range_start) {
                hi = mid - 1;
            } else if (glyph_id > range_end) {
                lo = mid + 1;
            } else {
                return gpos_u16(r + 4);
            }
        }
        return 0;
    }
    return 0;
}

/* Check if glyph_id is in Coverage table. Returns coverage index or -1. */
static int gpos_coverage_index(const uint8_t *cov, uint16_t glyph_id) {
    uint16_t fmt = gpos_u16(cov);
    if (fmt == 1) {
        uint16_t count = gpos_u16(cov + 2);
        /* Binary search */
        int lo = 0;
        int hi = count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            uint16_t g = gpos_u16(cov + 4 + mid * 2);
            if (glyph_id < g) {
                hi = mid - 1;
            } else if (glyph_id > g) {
                lo = mid + 1;
            } else {
                return mid;
            }
        }
        return -1;
    }
    if (fmt == 2) {
        uint16_t range_count = gpos_u16(cov + 2);
        int lo = 0;
        int hi = range_count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint8_t *r = cov + 4 + mid * 6;
            uint16_t range_start = gpos_u16(r);
            uint16_t range_end = gpos_u16(r + 2);
            if (glyph_id < range_start) {
                hi = mid - 1;
            } else if (glyph_id > range_end) {
                lo = mid + 1;
            } else {
                return gpos_u16(r + 4) + (glyph_id - range_start);
            }
        }
        return -1;
    }
    return -1;
}

/* Bulk kern triple: (left_charset_index, right_charset_index, value) */
typedef struct {
    uint16_t left;
    uint16_t right;
    int16_t value;
} KernTriple;

/* Extract all kern pairs from GPOS PairPos tables (Format 1 + 2).
 * charset_glyph_ids: array of stb glyph indices for each charset position.
 * out_triples/out_capacity: caller-owned dynamic array (realloc'd as needed).
 * Returns total pair count. O(K) for Format 1, O(N²) with O(1) lookup for Format 2. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t extract_gpos_kern_pairs(const stbtt_fontinfo *font, const int *charset_glyph_ids, uint32_t charset_count,
                                        KernTriple **out_triples, uint32_t *out_capacity) {
    if (!font->gpos) {
        return 0;
    }

    const uint8_t *gpos = font->data + font->gpos;
    if (gpos_u16(gpos) != 1) {
        return 0; /* only GPOS major version 1 */
    }

    /* Build glyph_id → charset_index lookup (for filtering).
     * glyph_ids can be up to ~65535, use a flat lookup if small enough. */
    uint16_t max_gid = 0;
    for (uint32_t i = 0; i < charset_count; i++) {
        if (charset_glyph_ids[i] > max_gid) {
            max_gid = (uint16_t)charset_glyph_ids[i];
        }
    }

    /* gid_to_index[gid] = charset index + 1 (0 = not in charset) */
    uint16_t *gid_to_index = (uint16_t *)calloc((size_t)max_gid + 1, sizeof(uint16_t));
    NT_BUILD_ASSERT(gid_to_index && "extract_gpos: gid lookup alloc failed");
    for (uint32_t i = 0; i < charset_count; i++) {
        gid_to_index[charset_glyph_ids[i]] = (uint16_t)(i + 1);
    }

    uint32_t pair_count = 0;
    KernTriple *triples = *out_triples;
    uint32_t capacity = *out_capacity;

    const uint8_t *lookup_list = gpos + gpos_u16(gpos + 8);
    uint16_t lookup_count = gpos_u16(lookup_list);

    for (uint16_t li = 0; li < lookup_count; li++) {
        const uint8_t *lookup = lookup_list + gpos_u16(lookup_list + 2 + li * 2);
        uint16_t lookup_type = gpos_u16(lookup);
        uint16_t sub_count = gpos_u16(lookup + 4);

        if (lookup_type != 2 && lookup_type != 9) {
            continue;
        }

        for (uint16_t si = 0; si < sub_count; si++) {
            const uint8_t *table = lookup + gpos_u16(lookup + 6 + si * 2);

            /* Unwrap Extension */
            if (lookup_type == 9) {
                uint16_t ext_type = gpos_u16(table + 2);
                if (ext_type != 2) {
                    continue;
                }
                table = table + gpos_u32(table + 4);
            }

            uint16_t pos_format = gpos_u16(table);
            uint16_t vf1 = gpos_u16(table + 4);
            uint16_t vf2 = gpos_u16(table + 6);
            if (vf1 != 4 || vf2 != 0) {
                continue; /* only xAdvance for first glyph */
            }

            // #region Format 1: iterate all PairSets → O(K)
            if (pos_format == 1) {
                uint16_t pair_set_count = gpos_u16(table + 8);
                const uint8_t *cov = table + gpos_u16(table + 2);

                for (uint16_t ps = 0; ps < pair_set_count; ps++) {
                    /* Get first glyph from coverage */
                    uint16_t cov_fmt = gpos_u16(cov);
                    uint16_t g1 = 0;
                    if (cov_fmt == 1) {
                        g1 = gpos_u16(cov + 4 + ps * 2);
                    } else {
                        /* Coverage Format 2: find glyph at coverage index ps */
                        uint16_t rc = gpos_u16(cov + 2);
                        uint16_t accumulated = 0;
                        for (uint16_t ri = 0; ri < rc; ri++) {
                            const uint8_t *rr = cov + 4 + ri * 6;
                            uint16_t rs = gpos_u16(rr);
                            uint16_t re = gpos_u16(rr + 2);
                            uint16_t start_idx = gpos_u16(rr + 4);
                            uint16_t range_size = re - rs + 1;
                            if (ps >= start_idx && ps < start_idx + range_size) {
                                g1 = rs + (ps - start_idx);
                                break;
                            }
                            accumulated += range_size;
                            (void)accumulated;
                        }
                    }

                    if (g1 > max_gid || gid_to_index[g1] == 0) {
                        continue; /* first glyph not in charset */
                    }
                    uint16_t left_idx = gid_to_index[g1] - 1;

                    const uint8_t *pair_set = table + gpos_u16(table + 10 + ps * 2);
                    uint16_t pv_count = gpos_u16(pair_set);

                    for (uint16_t pv = 0; pv < pv_count; pv++) {
                        const uint8_t *entry = pair_set + 2 + pv * 4; /* secondGlyph(2) + xAdvance(2) */
                        uint16_t g2 = gpos_u16(entry);
                        int16_t val = gpos_s16(entry + 2);
                        if (val == 0 || g2 > max_gid || gid_to_index[g2] == 0) {
                            continue;
                        }
                        uint16_t right_idx = gid_to_index[g2] - 1;

                        if (pair_count >= capacity) {
                            capacity = capacity > 0 ? capacity * 2 : 1024;
                            KernTriple *grown = (KernTriple *)realloc(triples, capacity * sizeof(KernTriple));
                            NT_BUILD_ASSERT(grown && "extract_gpos: kern realloc failed");
                            triples = grown;
                        }
                        triples[pair_count].left = left_idx;
                        triples[pair_count].right = right_idx;
                        triples[pair_count].value = val;
                        pair_count++;
                    }
                }
            }
            // #endregion

            // #region Format 2: iterate charset × charset with class matrix → O(N²) but O(1) per pair
            if (pos_format == 2) {
                const uint8_t *class_def1 = table + gpos_u16(table + 8);
                const uint8_t *class_def2 = table + gpos_u16(table + 10);
                uint16_t class1_count = gpos_u16(table + 12);
                uint16_t class2_count = gpos_u16(table + 14);
                const uint8_t *class_records = table + 16;

                for (uint32_t gi = 0; gi < charset_count; gi++) {
                    int c1 = gpos_get_class(class_def1, (uint16_t)charset_glyph_ids[gi]);
                    if (c1 < 0 || c1 >= class1_count) {
                        continue;
                    }
                    /* Check coverage */
                    if (gpos_coverage_index(table + gpos_u16(table + 2), (uint16_t)charset_glyph_ids[gi]) < 0) {
                        continue;
                    }

                    for (uint32_t gj = 0; gj < charset_count; gj++) {
                        int c2 = gpos_get_class(class_def2, (uint16_t)charset_glyph_ids[gj]);
                        if (c2 < 0 || c2 >= class2_count) {
                            continue;
                        }
                        int16_t val = gpos_s16(class_records + (c1 * class2_count + c2) * 2);
                        if (val == 0) {
                            continue;
                        }

                        if (pair_count >= capacity) {
                            capacity = capacity > 0 ? capacity * 2 : 1024;
                            KernTriple *grown = (KernTriple *)realloc(triples, capacity * sizeof(KernTriple));
                            NT_BUILD_ASSERT(grown && "extract_gpos: kern realloc failed");
                            triples = grown;
                        }
                        triples[pair_count].left = (uint16_t)gi;
                        triples[pair_count].right = (uint16_t)gj;
                        triples[pair_count].value = val;
                        pair_count++;
                    }
                }
            }
            // #endregion
        }
    }

    free(gid_to_index);
    *out_triples = triples;
    *out_capacity = capacity;
    return pair_count;
}

/* --- Contour analysis: compute curve data size from stb vertices --- */

static uint32_t compute_curve_data_size(const stbtt_vertex *verts, int nv, uint16_t *out_total_segments, uint16_t *out_contour_count) {
    if (nv == 0) {
        *out_total_segments = 0;
        *out_contour_count = 0;
        return 0;
    }

    uint32_t size = 2; /* contour_count uint16 */
    uint16_t total_segs = 0;
    uint16_t cc = 0;
    uint16_t cur_segs = 0;
    uint16_t cur_quads = 0;
    bool in_contour = false;

    for (int v = 0; v < nv; v++) {
        switch (verts[v].type) {
        case STBTT_vmove:
            if (in_contour && cur_segs > 0) {
                uint32_t bm = BITMASK_BYTES(cur_segs);
                size += 6 + bm + (uint32_t)cur_quads * 8 + (uint32_t)(cur_segs - cur_quads) * 4;
                cc++;
            }
            in_contour = true;
            cur_segs = 0;
            cur_quads = 0;
            break;
        case STBTT_vline:
            cur_segs++;
            total_segs++;
            break;
        case STBTT_vcurve:
            cur_segs++;
            cur_quads++;
            total_segs++;
            break;
        case STBTT_vcubic:
            NT_BUILD_ASSERT(0 && "cubic curves not supported (CFF font?)");
            break;
        default:
            break;
        }
    }
    /* Finalize last contour */
    if (in_contour && cur_segs > 0) {
        uint32_t bm = BITMASK_BYTES(cur_segs);
        size += 6 + bm + (uint32_t)cur_quads * 8 + (uint32_t)(cur_segs - cur_quads) * 4;
        cc++;
    }

    *out_total_segments = total_segs;
    *out_contour_count = cc;
    return (total_segs > 0) ? size : 0;
}

/* --- Write contour data from stb vertices (delta-encoded) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void write_contour_data(const stbtt_vertex *verts, int nv, uint16_t contour_count, uint8_t *wp) {
    memcpy(wp, &contour_count, 2);
    wp += 2;

    int vi = 0;
    for (uint16_t ci = 0; ci < contour_count; ci++) {
        /* Find next non-empty moveto (skip empty contours with 0 segments) */
        uint16_t seg_count = 0;
        while (vi < nv) {
            if (verts[vi].type != STBTT_vmove) {
                vi++;
                continue;
            }
            seg_count = 0;
            for (int peek = vi + 1; peek < nv && verts[peek].type != STBTT_vmove; peek++) {
                if (verts[peek].type == STBTT_vline || verts[peek].type == STBTT_vcurve) {
                    seg_count++;
                }
            }
            if (seg_count > 0) {
                break; /* found non-empty contour */
            }
            vi++; /* skip empty contour */
        }

        // #region Contour header
        memcpy(wp, &seg_count, 2);
        wp += 2;
        int16_t sx = verts[vi].x;
        int16_t sy = verts[vi].y;
        memcpy(wp, &sx, 2);
        wp += 2;
        memcpy(wp, &sy, 2);
        wp += 2;
        vi++;
        // #endregion

        // #region Bitmask + delta-encoded segments
        uint32_t bitmask_bytes = BITMASK_BYTES(seg_count);
        uint8_t *bitmask = wp;
        memset(bitmask, 0, bitmask_bytes);
        wp += bitmask_bytes;

        int prev_x = sx;
        int prev_y = sy;
        for (uint16_t s = 0; s < seg_count; s++) {
            if (verts[vi].type == STBTT_vcurve) {
                bitmask[s / 8] |= (uint8_t)(1U << (s % 8));
                int dp1x = verts[vi].cx - prev_x;
                int dp1y = verts[vi].cy - prev_y;
                int dp2x = verts[vi].x - prev_x;
                int dp2y = verts[vi].y - prev_y;
                NT_BUILD_ASSERT(abs(dp1x) <= INT16_MAX && abs(dp1y) <= INT16_MAX && "delta overflow in curve control point");
                NT_BUILD_ASSERT(abs(dp2x) <= INT16_MAX && abs(dp2y) <= INT16_MAX && "delta overflow in curve endpoint");
                int16_t d1x = (int16_t)dp1x;
                int16_t d1y = (int16_t)dp1y;
                int16_t d2x = (int16_t)dp2x;
                int16_t d2y = (int16_t)dp2y;
                memcpy(wp, &d1x, 2);
                wp += 2;
                memcpy(wp, &d1y, 2);
                wp += 2;
                memcpy(wp, &d2x, 2);
                wp += 2;
                memcpy(wp, &d2y, 2);
                wp += 2;
            } else {
                int dp2x = verts[vi].x - prev_x;
                int dp2y = verts[vi].y - prev_y;
                NT_BUILD_ASSERT(abs(dp2x) <= INT16_MAX && abs(dp2y) <= INT16_MAX && "delta overflow in line endpoint");
                int16_t d2x = (int16_t)dp2x;
                int16_t d2y = (int16_t)dp2y;
                memcpy(wp, &d2x, 2);
                wp += 2;
                memcpy(wp, &d2y, 2);
                wp += 2;
            }
            prev_x = verts[vi].x;
            prev_y = verts[vi].y;
            vi++;
        }
        // #endregion
    }
}

/* --- Per-glyph info (single-pass analysis) --- */

typedef struct {
    int glyph_idx;
    uint16_t total_segments;
    uint16_t contour_count;
    uint16_t kern_count;
    uint32_t curve_data_size;
} GlyphInfo;

/* --- Stored kern pair (from first pass, reused in second pass) --- */

typedef struct {
    uint16_t right_glyph_index;
    int16_t value;
} KernPair;

/* --- nt_builder_decode_font: TTF -> final NT_ASSET_FONT binary --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_decode_font(const char *path, const char *charset, uint8_t **out_data, uint32_t *out_size) {
    if (!path || !charset || !out_data || !out_size) {
        return NT_BUILD_ERR_VALIDATION;
    }

    // #region Parse charset
    uint32_t *codepoints = (uint32_t *)malloc(NT_FONT_MAX_CODEPOINTS * sizeof(uint32_t));
    NT_BUILD_ASSERT(codepoints && "decode_font: codepoint buffer alloc failed");
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

    // #region Extract font-level metrics
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

    /* units_per_em: use ScaleForMappingEmToPixels, NOT ScaleForPixelHeight.
     * PixelHeight maps to (ascent-descent), EmToPixels maps to actual EM size. */
    float scale = stbtt_ScaleForMappingEmToPixels(&font, 1.0F);
    NT_BUILD_ASSERT(scale > 0.0F && "decode_font: invalid em scale");
    uint16_t units_per_em = (uint16_t)((1.0F / scale) + 0.5F);
    // #endregion

    // #region Resolve all glyph indices upfront (avoids redundant FindGlyphIndex in kern loop)
    GlyphInfo *ginfo = (GlyphInfo *)calloc(glyph_count, sizeof(GlyphInfo));
    NT_BUILD_ASSERT(ginfo && "decode_font: glyph info alloc failed");

    for (uint32_t i = 0; i < glyph_count; i++) {
        ginfo[i].glyph_idx = stbtt_FindGlyphIndex(&font, (int)codepoints[i]);
        NT_BUILD_ASSERT(ginfo[i].glyph_idx != 0 && "codepoint not in font (D-09)");
    }
    // #endregion

    // #region Single analysis pass — shapes, kerns, sizes
    /* Vertex cache: store shapes from analysis to avoid double stbtt_GetGlyphShape.
     * Allocated as void* array to satisfy strict pointer conversion rules. */
    void **vert_cache_raw = (void **)calloc(glyph_count, sizeof(void *));
    int *vert_counts = (int *)calloc(glyph_count, sizeof(int));
    NT_BUILD_ASSERT(vert_cache_raw && vert_counts && "decode_font: vertex cache alloc failed");

    /* Kern pair storage: flat array, indexed per-glyph via offset+count */
    uint32_t kern_capacity = glyph_count * 16; /* initial estimate, grows if needed */
    KernPair *kern_pairs = (KernPair *)malloc(kern_capacity * sizeof(KernPair));
    uint32_t *kern_offsets = (uint32_t *)calloc(glyph_count, sizeof(uint32_t));
    NT_BUILD_ASSERT(kern_pairs && kern_offsets && "decode_font: kern storage alloc failed");

    uint32_t total_kerns = 0;
    uint32_t total_curve_data = 0;

    for (uint32_t i = 0; i < glyph_count; i++) {
        /* Analyze shape and cache vertices */
        vert_counts[i] = stbtt_GetGlyphShape(&font, ginfo[i].glyph_idx, (stbtt_vertex **)&vert_cache_raw[i]);
        ginfo[i].curve_data_size = compute_curve_data_size((stbtt_vertex *)vert_cache_raw[i], vert_counts[i], &ginfo[i].total_segments, &ginfo[i].contour_count);
        total_curve_data += ginfo[i].curve_data_size;

        /* Collect kern pairs (single pass, stored for reuse) */
        kern_offsets[i] = total_kerns;
        uint32_t kc = 0;
        for (uint32_t j = 0; j < glyph_count; j++) {
            /* No skip for j==i: self-kern pairs (e.g. CC, TT) are valid in GPOS class-based tables */
            int kern = stbtt_GetGlyphKernAdvance(&font, ginfo[i].glyph_idx, ginfo[j].glyph_idx);
            if (kern != 0) {
                /* Grow kern_pairs if needed */
                if (total_kerns + kc >= kern_capacity) {
                    kern_capacity *= 2;
                    KernPair *grown = (KernPair *)realloc(kern_pairs, kern_capacity * sizeof(KernPair));
                    NT_BUILD_ASSERT(grown && "decode_font: kern realloc failed");
                    kern_pairs = grown;
                }
                kern_pairs[total_kerns + kc].right_glyph_index = (uint16_t)j;
                kern_pairs[total_kerns + kc].value = (int16_t)kern;
                kc++;
            }
        }
        if (kc > UINT16_MAX) {
            kc = UINT16_MAX;
        }
        ginfo[i].kern_count = (uint16_t)kc;
        /* Kern pairs are sorted by right_glyph_index because j iterates ascending.
         * Assert this invariant — bsearch at runtime depends on it. */
        for (uint32_t k = 1; k < kc; k++) {
            NT_BUILD_ASSERT(kern_pairs[total_kerns + k].right_glyph_index > kern_pairs[total_kerns + k - 1].right_glyph_index && "kern pairs must be sorted by right_glyph_index");
        }
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

    // #region Build glyph entries and data blocks (no re-parsing)
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

        int bx0 = 0;
        int by0 = 0;
        int bx1 = 0;
        int by1 = 0;
        if (stbtt_GetGlyphBox(&font, ginfo[i].glyph_idx, &bx0, &by0, &bx1, &by1)) {
            ge->bbox_x0 = (int16_t)bx0;
            ge->bbox_y0 = (int16_t)by0;
            ge->bbox_x1 = (int16_t)bx1;
            ge->bbox_y1 = (int16_t)by1;
        }

        /* Write cached kern pairs */
        NtFontKernEntry *kern_dst = (NtFontKernEntry *)(buffer + data_offset);
        for (uint16_t k = 0; k < ginfo[i].kern_count; k++) {
            kern_dst[k].right_glyph_index = kern_pairs[kern_offsets[i] + k].right_glyph_index;
            kern_dst[k].value = kern_pairs[kern_offsets[i] + k].value;
        }
        data_offset += (uint32_t)(ginfo[i].kern_count * sizeof(NtFontKernEntry));

        /* Write contour data from cached vertices */
        if (ginfo[i].total_segments > 0) {
            write_contour_data((stbtt_vertex *)vert_cache_raw[i], vert_counts[i], ginfo[i].contour_count, buffer + data_offset);
            data_offset += ginfo[i].curve_data_size;
        }
    }
    NT_BUILD_ASSERT(data_offset == total_size && "buffer size mismatch after writing all glyphs");
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
    for (uint32_t i = 0; i < glyph_count; i++) {
        stbtt_FreeShape(&font, (stbtt_vertex *)vert_cache_raw[i]);
    }
    free(vert_cache_raw); /* NOLINT(bugprone-multi-level-implicit-pointer-conversion) */
    free(vert_counts);
    free(kern_pairs);
    free(kern_offsets);
    free(codepoints);
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
