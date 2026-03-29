#ifndef NT_FONT_FORMAT_H
#define NT_FONT_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "FONT" as uint32_t little-endian = 0x544E4F46 */
#define NT_FONT_MAGIC 0x544E4F46
#define NT_FONT_VERSION 1

/*
 * Font asset binary layout:
 *
 *   Offset 0: NtFontAssetHeader (16 bytes)
 *   Offset 16: NtFontGlyphEntry[glyph_count] (24 bytes each)
 *
 *   Per-glyph data blocks follow glyph entries. Each glyph's data_offset
 *   is relative to the start of NtFontAssetHeader. Data block order:
 *
 *     kerns_ptr   = base + data_offset
 *     curves_ptr  = kerns_ptr + kern_count * sizeof(NtFontKernEntry)   [8]
 *     bands_ptr   = curves_ptr + curve_count * sizeof(NtFontCurve)     [12]
 *     index_ptr   = bands_ptr + band_count * sizeof(NtFontBand)        [4]
 *
 *   Index table: uint16_t[] — curve indices per band. Size = sum of
 *   band[i].curve_count for all bands (derived at runtime, not stored).
 *
 *   Curves store quadratic Bezier control points as float16 (IEEE 754
 *   half-precision). Runtime uploads directly to RGBA16F texture.
 *   Bands store uint16 indices — direct upload to RG16UI texture.
 */

/* NtFontAssetHeader — 16 bytes. Font-level metadata. */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;        /* 0:  NT_FONT_MAGIC ("FONT") */
    uint16_t version;      /* 4:  NT_FONT_VERSION */
    uint16_t glyph_count;  /* 6:  number of NtFontGlyphEntry records */
    uint16_t units_per_em; /* 8:  font design units per em */
    int16_t ascent;        /* 10: typographic ascent in font units */
    int16_t descent;       /* 12: typographic descent in font units (negative) */
    int16_t line_gap;      /* 14: typographic line gap in font units */
} NtFontAssetHeader;       /* 16 bytes total */
#pragma pack(pop)
_Static_assert(sizeof(NtFontAssetHeader) == 16, "NtFontAssetHeader must be 16 bytes");

/* NtFontGlyphEntry — 24 bytes. Per-glyph metadata, sorted by codepoint for bsearch. */
#pragma pack(push, 1)
typedef struct {
    uint32_t codepoint;   /* 0:  Unicode codepoint (bsearch key) */
    uint32_t data_offset; /* 4:  byte offset from NtFontAssetHeader start to per-glyph data */
    int16_t advance;      /* 8:  horizontal advance in font units */
    int16_t bbox_x0;      /* 10: bounding box left in font units */
    int16_t bbox_y0;      /* 12: bounding box bottom in font units */
    int16_t bbox_x1;      /* 14: bounding box right in font units */
    int16_t bbox_y1;      /* 16: bounding box top in font units (bearing_y = bbox_y1) */
    uint16_t curve_count; /* 18: number of unique quadratic Bezier curves */
    uint8_t kern_count;   /* 20: number of kern pairs for this glyph */
    uint8_t band_count;   /* 21: number of horizontal bands */
    uint8_t _pad[2];      /* 22: align to 24 bytes */
} NtFontGlyphEntry;       /* 24 bytes total */
#pragma pack(pop)
_Static_assert(sizeof(NtFontGlyphEntry) == 24, "NtFontGlyphEntry must be 24 bytes");

/* NtFontKernEntry — 8 bytes. Kern pair, sorted by right_codepoint for bsearch. */
#pragma pack(push, 1)
typedef struct {
    uint32_t right_codepoint; /* 0: right glyph codepoint (bsearch key, sorted ascending) */
    int16_t value;            /* 4: kern adjustment in font units */
    uint16_t _pad;            /* 6: align to 8 bytes */
} NtFontKernEntry;            /* 8 bytes total */
#pragma pack(pop)
_Static_assert(sizeof(NtFontKernEntry) == 8, "NtFontKernEntry must be 8 bytes");

/* NtFontCurve — 12 bytes. Quadratic Bezier, 3 control points as float16 raw bits. */
#pragma pack(push, 1)
typedef struct {
    uint16_t p0x, p0y; /* 0: start point (float16 bits) */
    uint16_t p1x, p1y; /* 4: control point (float16 bits) */
    uint16_t p2x, p2y; /* 8: end point (float16 bits) */
} NtFontCurve;         /* 12 bytes total */
#pragma pack(pop)
_Static_assert(sizeof(NtFontCurve) == 12, "NtFontCurve must be 12 bytes");

/* NtFontBand — 4 bytes. References into per-glyph curve index table. */
#pragma pack(push, 1)
typedef struct {
    uint16_t curve_start; /* 0: start index into per-glyph index table */
    uint16_t curve_count; /* 2: number of curve indices for this band */
} NtFontBand;             /* 4 bytes total */
#pragma pack(pop)
_Static_assert(sizeof(NtFontBand) == 4, "NtFontBand must be 4 bytes");

#endif /* NT_FONT_FORMAT_H */
