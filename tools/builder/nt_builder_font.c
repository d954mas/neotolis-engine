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

/* --- Bulk GPOS kern pair extraction (replaces O(N^2) per-pair lookup) --- */

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
            return gpos_u16(class_def + 6 + ((size_t)(glyph_id - start) * 2));
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
            const uint8_t *r = ranges + ((size_t)mid * 6);
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
            uint16_t g = gpos_u16(cov + 4 + ((size_t)mid * 2));
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
            const uint8_t *r = cov + 4 + ((size_t)mid * 6);
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

/* Grow triples array if needed */
static void kern_triples_push(KernTriple **triples, uint32_t *capacity, uint32_t *count, uint16_t left, uint16_t right, int16_t value) {
    if (*count >= *capacity) {
        *capacity = *capacity > 0 ? (*capacity) * 2 : 1024;
        KernTriple *grown = (KernTriple *)realloc(*triples, (size_t)(*capacity) * sizeof(KernTriple));
        NT_BUILD_ASSERT(grown && "kern_triples_push: realloc failed");
        *triples = grown;
    }
    (*triples)[*count].left = left;
    (*triples)[*count].right = right;
    (*triples)[*count].value = value;
    (*count)++;
}

/* Extract all kern pairs from GPOS PairPos tables (Format 1 + 2).
 * charset_glyph_ids: array of stb glyph indices for each charset position.
 * out_triples/out_capacity: caller-owned dynamic array (realloc'd as needed).
 * Returns total pair count. O(K) for Format 1, O(N^2) with O(1) lookup for Format 2. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t extract_gpos_kern_pairs(const stbtt_fontinfo *font, const int *charset_glyph_ids, uint32_t charset_count, KernTriple **out_triples, uint32_t *out_capacity) {
    if (!font->gpos) {
        return 0;
    }

    const uint8_t *gpos = font->data + font->gpos;
    if (gpos_u16(gpos) != 1) {
        return 0; /* only GPOS major version 1 */
    }

    /* Build glyph_id -> charset_index lookup (for filtering).
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
        const uint8_t *lookup = lookup_list + gpos_u16(lookup_list + 2 + ((size_t)li * 2));
        uint16_t lookup_type = gpos_u16(lookup);
        uint16_t sub_count = gpos_u16(lookup + 4);

        if (lookup_type != 2 && lookup_type != 9) {
            continue;
        }

        for (uint16_t si = 0; si < sub_count; si++) {
            const uint8_t *table = lookup + gpos_u16(lookup + 6 + ((size_t)si * 2));

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

            // #region Format 1: iterate all PairSets -> O(K)
            if (pos_format == 1) {
                uint16_t pair_set_count = gpos_u16(table + 8);
                const uint8_t *cov = table + gpos_u16(table + 2);

                for (uint16_t ps = 0; ps < pair_set_count; ps++) {
                    /* Get first glyph from coverage */
                    uint16_t cov_fmt = gpos_u16(cov);
                    uint16_t g1 = 0;
                    if (cov_fmt == 1) {
                        g1 = gpos_u16(cov + 4 + ((size_t)ps * 2));
                    } else {
                        /* Coverage Format 2: find glyph at coverage index ps */
                        uint16_t rc = gpos_u16(cov + 2);
                        for (uint16_t ri = 0; ri < rc; ri++) {
                            const uint8_t *rr = cov + 4 + ((size_t)ri * 6);
                            uint16_t rs = gpos_u16(rr);
                            uint16_t re = gpos_u16(rr + 2);
                            uint16_t start_idx = gpos_u16(rr + 4);
                            uint16_t range_size = re - rs + 1;
                            if (ps >= start_idx && ps < start_idx + range_size) {
                                g1 = rs + (ps - start_idx);
                                break;
                            }
                        }
                    }

                    if (g1 > max_gid || gid_to_index[g1] == 0) {
                        continue; /* first glyph not in charset */
                    }
                    uint16_t left_idx = gid_to_index[g1] - 1;

                    const uint8_t *pair_set = table + gpos_u16(table + 10 + ((size_t)ps * 2));
                    uint16_t pv_count = gpos_u16(pair_set);

                    for (uint16_t pv = 0; pv < pv_count; pv++) {
                        const uint8_t *entry = pair_set + 2 + ((size_t)pv * 4); /* secondGlyph(2) + xAdvance(2) */
                        uint16_t g2 = gpos_u16(entry);
                        int16_t val = gpos_s16(entry + 2);
                        if (val == 0 || g2 > max_gid || gid_to_index[g2] == 0) {
                            continue;
                        }
                        uint16_t right_idx = gid_to_index[g2] - 1;
                        kern_triples_push(&triples, &capacity, &pair_count, left_idx, right_idx, val);
                    }
                }
            }
            // #endregion

            // #region Format 2: iterate charset x charset with class matrix -> O(N^2) but O(1) per pair
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
                        int16_t val = gpos_s16(class_records + ((size_t)((c1 * class2_count) + c2) * 2));
                        if (val == 0) {
                            continue;
                        }
                        kern_triples_push(&triples, &capacity, &pair_count, (uint16_t)gi, (uint16_t)gj, val);
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

/* Extract kern pairs from legacy 'kern' table (fonts without GPOS).
 * Format 0 only: sorted array of (glyph1<<16 | glyph2, value) pairs.
 * Returns total pair count. */
static uint32_t extract_legacy_kern_pairs(const stbtt_fontinfo *font, const int *charset_glyph_ids, uint32_t charset_count, KernTriple **out_triples, uint32_t *out_capacity) {
    if (!font->kern) {
        return 0;
    }

    const uint8_t *data = font->data + font->kern;

    /* Must have at least 1 subtable */
    if (gpos_u16(data + 2) < 1) {
        return 0;
    }
    /* Subtable coverage field (offset 8 in kern subtable header):
     * bit 0 = horizontal (must be 1), bits 1-7 = format/flags (must be 0 for Format 0).
     * Checking == 1 accepts only horizontal Format 0 with no cross-stream or override. */
    if (gpos_u16(data + 8) != 1) {
        return 0;
    }

    uint16_t n_pairs = gpos_u16(data + 10);
    const uint8_t *pairs_data = data + 18;

    /* Build glyph_id -> charset_index lookup */
    uint16_t max_gid = 0;
    for (uint32_t i = 0; i < charset_count; i++) {
        if (charset_glyph_ids[i] > max_gid) {
            max_gid = (uint16_t)charset_glyph_ids[i];
        }
    }
    uint16_t *gid_to_index = (uint16_t *)calloc((size_t)max_gid + 1, sizeof(uint16_t));
    NT_BUILD_ASSERT(gid_to_index && "extract_legacy_kern: gid lookup alloc failed");
    for (uint32_t i = 0; i < charset_count; i++) {
        gid_to_index[charset_glyph_ids[i]] = (uint16_t)(i + 1);
    }

    uint32_t pair_count = 0;
    KernTriple *triples = *out_triples;
    uint32_t capacity = *out_capacity;

    /* Linear scan of all kern pairs, filter by charset membership */
    for (uint16_t pi = 0; pi < n_pairs; pi++) {
        const uint8_t *pe = pairs_data + ((size_t)pi * 6);
        uint16_t g1 = gpos_u16(pe);
        uint16_t g2 = gpos_u16(pe + 2);
        int16_t val = gpos_s16(pe + 4);
        if (val == 0) {
            continue;
        }
        if (g1 > max_gid || gid_to_index[g1] == 0) {
            continue;
        }
        if (g2 > max_gid || gid_to_index[g2] == 0) {
            continue;
        }
        uint16_t left_idx = gid_to_index[g1] - 1;
        uint16_t right_idx = gid_to_index[g2] - 1;
        kern_triples_push(&triples, &capacity, &pair_count, left_idx, right_idx, val);
    }

    free(gid_to_index);
    *out_triples = triples;
    *out_capacity = capacity;
    return pair_count;
}

/* Sort kern triples by (left, right) for grouping into per-glyph arrays */
static int kern_triple_compare(const void *a, const void *b) {
    const KernTriple *ta = (const KernTriple *)a;
    const KernTriple *tb = (const KernTriple *)b;
    if (ta->left < tb->left) {
        return -1;
    }
    if (ta->left > tb->left) {
        return 1;
    }
    if (ta->right < tb->right) {
        return -1;
    }
    if (ta->right > tb->right) {
        return 1;
    }
    return 0;
}

/* --- v4 point-based contour encoding (implicit midpoints, like TrueType) --- */

/* Variable-length delta size: 1 byte if fits in [-127,127], else 3 bytes (sentinel + int16) */
static inline uint32_t varlen_size(int delta) {
    return (delta >= -127 && delta <= 127) ? 1 : 3;
}

static inline void write_varlen_delta(uint8_t **wp, int delta) {
    NT_BUILD_ASSERT(abs(delta) <= INT16_MAX && "delta overflow");
    if (delta >= -127 && delta <= 127) {
        **wp = (uint8_t)(int8_t)delta;
        (*wp)++;
    } else {
        **wp = NT_FONT_DELTA_SENTINEL;
        (*wp)++;
        int16_t val = (int16_t)delta;
        memcpy(*wp, &val, 2);
        (*wp) += 2;
    }
}

/* Temporary point buffer for one contour (used during encoding) */
typedef struct {
    int16_t x, y;
    bool on_curve;
} FontPoint;

#define MAX_POINTS_PER_CONTOUR 8192
static FontPoint s_points[MAX_POINTS_PER_CONTOUR];

/* Convert stbtt vertices for one contour into compact point representation.
 * Detects implicit midpoints inserted by stbtt and removes them.
 * Returns number of points written to s_points. */
static uint16_t vertices_to_points(const stbtt_vertex *verts, int start, int end) {
    uint16_t np = 0;

    /* First vertex is always vmove → on-curve start point */
    NT_BUILD_ASSERT(verts[start].type == STBTT_vmove);
    s_points[np++] = (FontPoint){verts[start].x, verts[start].y, true};

    for (int vi = start + 1; vi < end; vi++) {
        if (verts[vi].type == STBTT_vline) {
            /* Line: add on-curve endpoint */
            NT_BUILD_ASSERT(np < MAX_POINTS_PER_CONTOUR);
            s_points[np++] = (FontPoint){verts[vi].x, verts[vi].y, true};
        } else if (verts[vi].type == STBTT_vcurve) {
            /* Quad curve: add off-curve control point */
            NT_BUILD_ASSERT(np < MAX_POINTS_PER_CONTOUR);
            s_points[np++] = (FontPoint){verts[vi].cx, verts[vi].cy, false};

            /* Check if endpoint is an implicit midpoint (will be skipped).
             * If next vertex is also vcurve, and current endpoint == midpoint
             * of current control and next control, it's implicit. */
            bool implicit = false;
            if (vi + 1 < end && verts[vi + 1].type == STBTT_vcurve) {
                int mid_x = (verts[vi].cx + verts[vi + 1].cx) >> 1;
                int mid_y = (verts[vi].cy + verts[vi + 1].cy) >> 1;
                if (verts[vi].x == mid_x && verts[vi].y == mid_y) {
                    implicit = true;
                }
            }
            if (!implicit) {
                /* Explicit on-curve endpoint — keep it */
                NT_BUILD_ASSERT(np < MAX_POINTS_PER_CONTOUR);
                s_points[np++] = (FontPoint){verts[vi].x, verts[vi].y, true};
            }
        }
    }

    /* Remove trailing on-curve point if it equals the start (contour is closed implicitly) */
    if (np > 1 && s_points[np - 1].on_curve && s_points[np - 1].x == s_points[0].x && s_points[np - 1].y == s_points[0].y) {
        np--;
    }

    return np;
}

static uint32_t compute_curve_data_size(const stbtt_vertex *verts, int nv, uint16_t *out_total_segments, uint16_t *out_contour_count) {
    if (nv == 0) {
        *out_total_segments = 0;
        *out_contour_count = 0;
        return 0;
    }

    uint32_t size = 2; /* contour_count uint16 */
    uint16_t total_segs = 0;
    uint16_t cc = 0;

    /* Find contour boundaries (vmove to vmove) */
    for (int v = 0; v < nv; v++) {
        if (verts[v].type != STBTT_vmove) {
            continue;
        }
        int contour_end = v + 1;
        while (contour_end < nv && verts[contour_end].type != STBTT_vmove) {
            contour_end++;
        }
        /* Count segments in this contour */
        uint16_t seg_count = 0;
        for (int j = v + 1; j < contour_end; j++) {
            if (verts[j].type == STBTT_vline || verts[j].type == STBTT_vcurve) {
                seg_count++;
            }
        }
        if (seg_count == 0) {
            continue;
        }

        /* Convert to points to get accurate size */
        uint16_t np = vertices_to_points(verts, v, contour_end);
        uint32_t flags_bytes = NT_FONT_BITMASK_BYTES(np);

        /* Size: point_count(2) + flags + first point(4) + deltas for rest */
        uint32_t contour_size = 2 + flags_bytes + 4; /* header + first point absolute */
        int prev_x = s_points[0].x;
        int prev_y = s_points[0].y;
        for (uint16_t p = 1; p < np; p++) {
            contour_size += varlen_size(s_points[p].x - prev_x) + varlen_size(s_points[p].y - prev_y);
            prev_x = s_points[p].x;
            prev_y = s_points[p].y;
        }
        size += contour_size;
        total_segs += seg_count;
        cc++;
    }

    *out_total_segments = total_segs;
    *out_contour_count = cc;
    return (total_segs > 0) ? size : 0;
}

/* --- Write point-based contour data (v4) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void write_contour_data(const stbtt_vertex *verts, int nv, uint16_t contour_count, uint8_t *wp) {
    memcpy(wp, &contour_count, 2);
    wp += 2;

    for (int v = 0; v < nv; v++) {
        if (verts[v].type != STBTT_vmove) {
            continue;
        }
        int contour_end = v + 1;
        while (contour_end < nv && verts[contour_end].type != STBTT_vmove) {
            contour_end++;
        }
        /* Skip empty contours */
        uint16_t seg_count = 0;
        for (int j = v + 1; j < contour_end; j++) {
            if (verts[j].type == STBTT_vline || verts[j].type == STBTT_vcurve) {
                seg_count++;
            }
        }
        if (seg_count == 0) {
            continue;
        }

        /* Convert to compact points */
        uint16_t np = vertices_to_points(verts, v, contour_end);

        // #region Contour header: point_count + flags
        memcpy(wp, &np, 2);
        wp += 2;

        uint32_t flags_bytes = NT_FONT_BITMASK_BYTES(np);
        uint8_t *flags = wp;
        memset(flags, 0, flags_bytes);
        for (uint16_t p = 0; p < np; p++) {
            if (s_points[p].on_curve) {
                flags[p / 8] |= (uint8_t)(1U << (p % 8));
            }
        }
        wp += flags_bytes;
        // #endregion

        // #region Coordinates: first absolute, rest varlen delta
        int16_t fx = s_points[0].x;
        int16_t fy = s_points[0].y;
        memcpy(wp, &fx, 2);
        wp += 2;
        memcpy(wp, &fy, 2);
        wp += 2;

        int prev_x = fx;
        int prev_y = fy;
        for (uint16_t p = 1; p < np; p++) {
            write_varlen_delta(&wp, s_points[p].x - prev_x);
            write_varlen_delta(&wp, s_points[p].y - prev_y);
            prev_x = s_points[p].x;
            prev_y = s_points[p].y;
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

/* --- Stored kern pair (per-glyph, used for writing to pack) --- */

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

    // #region Resolve all glyph indices upfront
    GlyphInfo *ginfo = (GlyphInfo *)calloc(glyph_count, sizeof(GlyphInfo));
    NT_BUILD_ASSERT(ginfo && "decode_font: glyph info alloc failed");

    int *charset_glyph_ids = (int *)malloc((size_t)glyph_count * sizeof(int));
    NT_BUILD_ASSERT(charset_glyph_ids && "decode_font: charset_glyph_ids alloc failed");

    for (uint32_t i = 0; i < glyph_count; i++) {
        ginfo[i].glyph_idx = stbtt_FindGlyphIndex(&font, (int)codepoints[i]);
        NT_BUILD_ASSERT(ginfo[i].glyph_idx != 0 && "codepoint not in font (D-09)");
        charset_glyph_ids[i] = ginfo[i].glyph_idx;
    }
    // #endregion

    // #region Single analysis pass -- shapes + sizes (no kern here)
    /* Vertex cache: store shapes from analysis to avoid double stbtt_GetGlyphShape.
     * Allocated as void* array to satisfy strict pointer conversion rules. */
    void **vert_cache_raw = (void **)calloc(glyph_count, sizeof(void *));
    int *vert_counts = (int *)calloc(glyph_count, sizeof(int));
    NT_BUILD_ASSERT(vert_cache_raw && vert_counts && "decode_font: vertex cache alloc failed");

    uint32_t total_curve_data = 0;

    for (uint32_t i = 0; i < glyph_count; i++) {
        /* Analyze shape and cache vertices */
        vert_counts[i] = stbtt_GetGlyphShape(&font, ginfo[i].glyph_idx, (stbtt_vertex **)&vert_cache_raw[i]);
        ginfo[i].curve_data_size = compute_curve_data_size((stbtt_vertex *)vert_cache_raw[i], vert_counts[i], &ginfo[i].total_segments, &ginfo[i].contour_count);
        total_curve_data += ginfo[i].curve_data_size;
    }
    // #endregion

    // #region Bulk kern extraction (GPOS or legacy kern table)
    KernTriple *triples = NULL;
    uint32_t triple_capacity = 0;
    uint32_t triple_count = 0;

    if (font.gpos) {
        NT_BUILD_ASSERT((uint32_t)font.gpos < file_size && "GPOS table offset exceeds font file bounds");
        triple_count = extract_gpos_kern_pairs(&font, charset_glyph_ids, glyph_count, &triples, &triple_capacity);
    } else if (font.kern) {
        NT_BUILD_ASSERT((uint32_t)font.kern < file_size && "kern table offset exceeds font file bounds");
        triple_count = extract_legacy_kern_pairs(&font, charset_glyph_ids, glyph_count, &triples, &triple_capacity);
    }

    /* Sort triples by (left, right) for grouping */
    if (triple_count > 1) {
        qsort(triples, triple_count, sizeof(KernTriple), kern_triple_compare);
    }

    /* Group into per-glyph kern counts + flat KernPair array */
    KernPair *kern_pairs = NULL;
    uint32_t *kern_offsets = (uint32_t *)calloc(glyph_count, sizeof(uint32_t));
    NT_BUILD_ASSERT(kern_offsets && "decode_font: kern_offsets alloc failed");

    if (triple_count > 0) {
        kern_pairs = (KernPair *)malloc((size_t)triple_count * sizeof(KernPair));
        NT_BUILD_ASSERT(kern_pairs && "decode_font: kern_pairs alloc failed");
    }

    uint32_t total_kerns = 0;
    {
        uint32_t ti = 0;
        for (uint32_t gi = 0; gi < glyph_count; gi++) {
            kern_offsets[gi] = total_kerns;
            uint32_t kc = 0;
            while (ti < triple_count && triples[ti].left == gi) {
                /* Deduplicate: skip if same (left, right) as previous -- keep first occurrence */
                if (kc > 0 && kern_pairs[total_kerns + kc - 1].right_glyph_index == triples[ti].right) {
                    ti++;
                    continue;
                }
                kern_pairs[total_kerns + kc].right_glyph_index = triples[ti].right;
                kern_pairs[total_kerns + kc].value = triples[ti].value;
                kc++;
                ti++;
            }
            if (kc > UINT16_MAX) {
                kc = UINT16_MAX;
            }
            ginfo[gi].kern_count = (uint16_t)kc;
            /* Kern pairs are sorted by right_glyph_index because triples were sorted by (left, right).
             * Assert this invariant -- bsearch at runtime depends on it. */
            for (uint32_t k = 1; k < kc; k++) {
                NT_BUILD_ASSERT(kern_pairs[total_kerns + k].right_glyph_index > kern_pairs[total_kerns + k - 1].right_glyph_index && "kern pairs must be sorted by right_glyph_index");
            }
            total_kerns += kc;
        }
    }
    free(triples);
    free(charset_glyph_ids);
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
        char kern_str[16];
        char curve_str[16];
        nt_format_size(total_kerns * (uint32_t)sizeof(NtFontKernEntry), kern_str, sizeof(kern_str));
        nt_format_size(total_curve_data, curve_str, sizeof(curve_str));
        NT_LOG_INFO("  FONT %s: %u glyphs, %s packed (TTF %s) | kerns: %u (%s) curves: %s", path, glyph_count,
                    packed_str, ttf_str, total_kerns, kern_str, curve_str);
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
        (void)snprintf(logical_path, sizeof(logical_path), "%s", opts->resource_name);
    } else {
        (void)snprintf(logical_path, sizeof(logical_path), "%s", path);
    }

    /* Resolve actual file path via asset roots */
    char *resolved_path = nt_builder_find_file(path, NULL, ctx);
    const char *decode_path = resolved_path ? resolved_path : path;

    /* Decode TTF -> final NT_ASSET_FONT binary (before alloc — if decode asserts,
     * nothing is leaked; EXPECT_BUILD_ASSERT in tests relies on this order) */
    uint8_t *data = NULL;
    uint32_t size = 0;
    nt_build_result_t r = nt_builder_decode_font(decode_path, opts->charset, &data, &size);
    free(resolved_path);
    NT_BUILD_ASSERT(r == NT_BUILD_OK && "add_font: decode failed");

    /* Allocate font-specific data (after successful decode — no leak on assert) */
    NtBuildFontData *fd = (NtBuildFontData *)calloc(1, sizeof(NtBuildFontData));
    NT_BUILD_ASSERT(fd && "add_font: font data alloc failed");
    fd->charset = strdup(opts->charset);
    NT_BUILD_ASSERT(fd->charset && "add_font: strdup failed");

    /* Hash decoded data and register deferred entry */
    uint64_t hash = nt_hash64(data, size).value;
    nt_builder_add_entry(ctx, logical_path, NT_BUILD_ASSET_FONT, fd, data, size, hash);
}
