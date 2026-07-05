#ifndef NT_FONT_HOT_H
#define NT_FONT_HOT_H

/* Slot pointer is stable until destroy/shutdown but contents mutate on every
 * nt_font_step — resolve once per draw scope, do not cache across frames. */

#include "font/nt_font.h"

typedef struct nt_font_slot_s nt_font_slot_t;

nt_font_slot_t *nt_font_get_slot(nt_font_t font); /* NULL if invalid */

/* Weight-aware glyph lookup: regular text passes key_offset 0; emboldened
 * variants pass a quantized weight bucket (font units, from nt_font_quantize_weight).
 * (codepoint, key_offset) is the LRU cache key — variants coexist in one font's cache. */
const nt_glyph_cache_entry_t *nt_font_lookup_glyph_offset(nt_font_slot_t *slot, uint32_t codepoint, int16_t key_offset);
const nt_glyph_cache_entry_t *nt_font_lookup_glyph_in_slot(nt_font_slot_t *slot, uint32_t codepoint); /* == offset 0 */

/* Quantize a font-unit embolden weight to the int16 cache key_offset: rounds to
 * NT_FONT_WEIGHT_QUANT_STEP (nearby weights share a slot), saturates to int16 (no wrap). */
#ifndef NT_FONT_WEIGHT_QUANT_STEP
#define NT_FONT_WEIGHT_QUANT_STEP 8
#endif
/* Used as the round-to divisor in nt_font_quantize_weight: 0 divides-by-zero, negative makes bad buckets. */
_Static_assert(NT_FONT_WEIGHT_QUANT_STEP > 0 && NT_FONT_WEIGHT_QUANT_STEP <= 32767, "NT_FONT_WEIGHT_QUANT_STEP must be in [1, 32767]");
int16_t nt_font_quantize_weight(float weight_units);

int16_t nt_font_get_kern_in_slot(const nt_font_slot_t *slot, uint32_t left_codepoint, uint32_t right_codepoint);

#endif /* NT_FONT_HOT_H */
