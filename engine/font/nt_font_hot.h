#ifndef NT_FONT_HOT_H
#define NT_FONT_HOT_H

/* Cross-module hot-path API for nt_font. Skips the per-call pool_valid +
 * slot-resolve overhead in per-codepoint loops (text renderers, layout
 * walkers). For games and high-level UI use the handle-based API in nt_font.h.
 *
 * Slot pointer is stable until nt_font_destroy / shutdown. Contents may
 * mutate on any nt_font_step (resource load/unload, cache flush). Resolve
 * once per draw scope; do NOT cache across frames. */

#include "font/nt_font.h"

#include <stdint.h>

typedef struct nt_font_slot_s nt_font_slot_t;

/* NULL if the module is uninitialized or the handle is invalid. */
nt_font_slot_t *nt_font_get_slot(nt_font_t font);

/* Mutates LRU counter, may upload on miss. */
const nt_glyph_cache_entry_t *nt_font_lookup_glyph_in_slot(nt_font_slot_t *slot, uint32_t codepoint);

/* Pure read — kern table lives in the immutable blob. */
int16_t nt_font_get_kern_in_slot(const nt_font_slot_t *slot, uint32_t left_codepoint, uint32_t right_codepoint);

#endif /* NT_FONT_HOT_H */
