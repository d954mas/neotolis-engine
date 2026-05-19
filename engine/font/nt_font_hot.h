#ifndef NT_FONT_HOT_H
#define NT_FONT_HOT_H

/* Slot pointer is stable until destroy/shutdown but contents mutate on every
 * nt_font_step — resolve once per draw scope, do not cache across frames. */

#include "font/nt_font.h"

#include <stdint.h>

typedef struct nt_font_slot_s nt_font_slot_t;

nt_font_slot_t *nt_font_get_slot(nt_font_t font); /* NULL if invalid */
const nt_glyph_cache_entry_t *nt_font_lookup_glyph_in_slot(nt_font_slot_t *slot, uint32_t codepoint);
int16_t nt_font_get_kern_in_slot(const nt_font_slot_t *slot, uint32_t left_codepoint, uint32_t right_codepoint);

#endif /* NT_FONT_HOT_H */
