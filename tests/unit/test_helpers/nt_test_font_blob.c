#include "test_helpers/nt_test_font_blob.h"

#include <stdlib.h>
#include <string.h>

#include "nt_font_format.h"
#include "unity.h"

uint8_t *nt_test_font_blob(uint32_t first_cp, uint32_t last_cp, uint32_t *out_size) {
    const uint32_t glyph_count = last_cp - first_cp + 1U;
    const uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    const uint32_t contour_offset = header_size + (glyph_count * (uint32_t)sizeof(NtFontGlyphEntry));
    /* contour_count 1, point_count 3, all on-curve: clockwise (a TrueType outer) triangle spanning the
     * bbox, so an emboldened variant grows past it. First point (0,-200), int16 deltas (0,1000) (400,-1000). */
    const uint8_t esc = NT_FONT_DELTA_SENTINEL;
    const uint8_t contour[] = {1, 0, 3, 0, 0x07, 0x00, 0x00, 0x00, 0x38, 0xFF, 0, esc, 0xE8, 0x03, esc, 0x90, 0x01, esc, 0x18, 0xFC};
    *out_size = contour_offset + (uint32_t)sizeof contour;
    uint8_t *blob = (uint8_t *)calloc(*out_size, 1);
    TEST_ASSERT_NOT_NULL(blob);

    const NtFontAssetHeader hdr = {
        .magic = NT_FONT_MAGIC,
        .version = NT_FONT_VERSION,
        .glyph_count = (uint16_t)glyph_count,
        .units_per_em = 1000,
        .ascent = 800,
        .descent = -200,
        .underline_position = -100,
        .underline_thickness = 50,
        .strikeout_position = 250,
        .strikeout_size = 40,
    };
    memcpy(blob, &hdr, sizeof hdr);

    for (uint32_t i = 0; i < glyph_count; i++) {
        NtFontGlyphEntry entry = {.codepoint = first_cp + i, .data_offset = contour_offset, .advance = 500};
        if (entry.codepoint != ' ') {
            entry.bbox_y0 = -200;
            entry.bbox_x1 = 400;
            entry.bbox_y1 = 800;
            entry.curve_count = 3;
        }
        memcpy(blob + header_size + ((size_t)i * sizeof entry), &entry, sizeof entry);
    }
    memcpy(blob + contour_offset, contour, sizeof contour);
    return blob;
}
