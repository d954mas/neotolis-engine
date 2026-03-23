#ifndef NT_SORT_H
#define NT_SORT_H

#include <stdint.h>

#include "render/nt_render_defs.h"

/**
 * Sort render items by sort_key in ascending order using LSD radix sort.
 *
 * @param items   Array of render items to sort (modified in-place).
 * @param count   Number of items. If < 2, returns immediately.
 * @param scratch Caller-provided scratch buffer; must hold at least @p count items.
 *
 * Zero heap allocation: only uses @p scratch and stack-local histogram.
 * Stable sort: items with equal sort_key preserve their relative input order.
 */
void nt_sort_by_key(nt_render_item_t *items, uint32_t count, nt_render_item_t *scratch);

#endif /* NT_SORT_H */
