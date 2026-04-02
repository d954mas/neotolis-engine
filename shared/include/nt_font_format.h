#ifndef NT_FONT_FORMAT_H
#define NT_FONT_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "FONT" as uint32_t little-endian = 0x544E4F46 */
#define NT_FONT_MAGIC 0x544E4F46
#define NT_FONT_VERSION 4

/*
 * Font asset binary layout (v4 — point-based contours with implicit midpoints):
 *
 *   Offset 0: NtFontAssetHeader (16 bytes)
 *   Offset 16: NtFontGlyphEntry[glyph_count] (24 bytes each)
 *
 *   Per-glyph data blocks follow glyph entries. Each glyph's data_offset
 *   is relative to the start of NtFontAssetHeader. Data block order:
 *
 *     kerns_ptr    = base + data_offset
 *     contours_ptr = kerns_ptr + kern_count * sizeof(NtFontKernEntry)
 *
 *   Kern entries use glyph_index (position in glyph table) instead of
 *   raw codepoint — compact 4-byte pairs. Runtime already knows glyph
 *   indices from the bsearch lookup, so no extra conversion needed.
 *
 *   Contour data layout (v4 — point-based, like TrueType):
 *
 *     uint16_t contour_count
 *
 *     Per contour (contour_count times):
 *       uint16_t point_count
 *       uint8_t  on_curve_flags[ALIGN2(ceil(point_count/8))]
 *           bit=1: on-curve point, bit=0: off-curve (control) point
 *           LSB first, 2-byte aligned
 *
 *       First point: int16_t x, int16_t y  (absolute coordinates)
 *       Subsequent points (point_count - 1):
 *         varlen dx, varlen dy  (delta from previous point)
 *
 *     Variable-length coordinate encoding:
 *       - If value in [-127, +127]: 1 byte (int8)
 *       - Otherwise: 0x80 sentinel byte + int16 LE (3 bytes total)
 *
 *     Implicit midpoint rule (TrueType convention):
 *       Two consecutive off-curve points imply an on-curve midpoint
 *       between them. The decoder inserts these automatically.
 *       This saves ~19% of coordinate data for CJK glyphs.
 *
 *     Decoding rules (point sequence → quadratic curves):
 *       - on → on:  LINE segment (promote to degenerate quad: p1 = midpoint)
 *       - on → off → on:  QUAD segment (off-curve = control point)
 *       - on → off → off:  QUAD from on to midpoint(off1, off2), control = off1
 *                           Then continue from midpoint as new on-curve start
 *       - Contour closing: last point connects back to first point
 *
 *     Coordinates are in font design units.
 *     Runtime at cache miss decodes points → produces nt_curve_t (p0,p1,p2)
 *     → band decomposition → GPU upload (same as v3).
 */

/* Sentinel byte for variable-length delta encoding.
 * int8 value -128 (0x80) is reserved; real deltas of -128 use the 3-byte path. */
#define NT_FONT_DELTA_SENTINEL ((uint8_t)0x80)

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
    uint16_t curve_count; /* 18: total segments (lines+quads) across all contours */
    uint16_t kern_count;  /* 20: number of kern pairs for this glyph */
    uint8_t _reserved[2]; /* 22: reserved (pad to 24 bytes) */
} NtFontGlyphEntry;       /* 24 bytes total */
#pragma pack(pop)
_Static_assert(sizeof(NtFontGlyphEntry) == 24, "NtFontGlyphEntry must be 24 bytes");

/* NtFontKernEntry — 4 bytes. Kern pair, sorted by right_glyph_index for bsearch. */
#pragma pack(push, 1)
typedef struct {
    uint16_t right_glyph_index; /* 0: index into glyph table (bsearch key, sorted ascending) */
    int16_t value;              /* 2: kern adjustment in font units */
} NtFontKernEntry;              /* 4 bytes total */
#pragma pack(pop)
_Static_assert(sizeof(NtFontKernEntry) == 4, "NtFontKernEntry must be 4 bytes");

/* Maximum points per contour — shared limit between builder and runtime */
#define NT_FONT_MAX_POINTS_PER_CONTOUR 4096

/* Bitmask byte size for contour type bits (ceil(n/8), 2-byte aligned) */
#define NT_FONT_BITMASK_BYTES(n) ((((uint32_t)(n) + 15U) / 8U) & ~1U)

#endif /* NT_FONT_FORMAT_H */
