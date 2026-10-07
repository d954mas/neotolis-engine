#ifndef NT_TEST_HELPER_FONT_BLOB_H
#define NT_TEST_HELPER_FONT_BLOB_H

#include <stdint.h>

/* A font asset blob (caller frees): for each codepoint first_cp..last_cp one clockwise triangle glyph filling
 * bbox 0..400 x -200..800, advance 500; ' ' is advance-only. units_per_em 1000, ascent 800, descent -200,
 * underline at -100 (thickness 50), strikeout at 250 (size 40). */
uint8_t *nt_test_font_blob(uint32_t first_cp, uint32_t last_cp, uint32_t *out_size);

#endif /* NT_TEST_HELPER_FONT_BLOB_H */
