#ifndef NT_FONT_HOT_H
#define NT_FONT_HOT_H

/* Cross-module hot-path API for nt_font. NOT a public engine surface —
 * include this header only from tightly-coupled consumers that run a
 * per-codepoint inner loop (e.g. nt_text_renderer's draw_n / measure_n).
 * Games and high-level UI should use the handle-based API in nt_font.h.
 *
 * Purpose: skip the per-call `nt_pool_valid + get_slot` overhead that the
 * handle-based functions pay. The caller resolves a handle to an opaque
 * `nt_font_slot_t *` once, then passes the pointer to the per-codepoint
 * lookup functions.
 *
 * `nt_font_slot_t` is opaque — its layout lives in nt_font_internal.h and
 * is not part of the API. Callers can only pass the pointer through.
 *
 * Lifetime contract:
 *   - Pointer ADDRESS is stable until `nt_font_destroy(font)` or
 *     `nt_font_shutdown` — the slots array is allocated once at init and
 *     slot indices don't move.
 *   - Pointer CONTENTS may mutate on any `nt_font_step` call (resource
 *     load/unload triggers rebuild_slot_indices, ASCII fast-path refresh,
 *     measure cache clear, glyph cache flush, metrics reset). Hold the
 *     pointer only within a single draw/measure scope — typically resolve
 *     at the top of a draw_n / measure_n and discard at the end. Do NOT
 *     cache slot pointers across frames. */

#include "font/nt_font.h"

#include <stdint.h>

typedef struct nt_font_slot_s nt_font_slot_t;

/* Returns NULL if the module is uninitialized or the handle is invalid. */
nt_font_slot_t *nt_font_get_slot(nt_font_t font);

/* Same observable behavior as nt_font_lookup_glyph (including upload-on-miss).
 * NOT const — mutates LRU counter and may insert into the GPU glyph cache. */
const nt_glyph_cache_entry_t *nt_font_lookup_glyph_in_slot(nt_font_slot_t *slot, uint32_t codepoint);

/* Same observable behavior as nt_font_get_kern. Pure read — the kern table
 * sits in the immutable blob, so the slot pointer is const. */
int16_t nt_font_get_kern_in_slot(const nt_font_slot_t *slot, uint32_t left_codepoint, uint32_t right_codepoint);

#endif /* NT_FONT_HOT_H */
