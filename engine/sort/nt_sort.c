#include "sort/nt_sort.h"

#include <stdbool.h>
#include <string.h>

/**
 * LSD radix sort for render items by 64-bit sort_key.
 *
 * 8 passes (one per byte), 256-bucket histogram per pass.
 * Pass-skip optimization: if all items share the same digit value in a pass,
 * the scatter is skipped entirely.
 *
 * Working memory: caller-provided scratch buffer + stack-local histogram (1 KB).
 * Zero heap allocation.
 */
void nt_sort_by_key(nt_render_item_t *items, uint32_t count, nt_render_item_t *scratch) {
    if (count < 2) {
        return;
    }

    nt_render_item_t *src = items;
    nt_render_item_t *dst = scratch;

    for (int pass = 0; pass < 8; ++pass) {
        int shift = pass * 8;

        /* Build histogram */
        uint32_t histogram[256] = {0};
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t digit = (uint8_t)((src[i].sort_key >> shift) & 0xFF);
            ++histogram[digit];
        }

        /* Pass-skip: if a single bucket holds all items, this digit is uniform */
        bool skip = false;
        for (int b = 0; b < 256; ++b) {
            if (histogram[b] == count) {
                skip = true;
                break;
            }
        }
        if (skip) {
            continue;
        }

        /* Prefix sum: convert histogram to exclusive prefix sums */
        uint32_t sum = 0;
        for (int b = 0; b < 256; ++b) {
            uint32_t c = histogram[b];
            histogram[b] = sum;
            sum += c;
        }

        /* Scatter: copy items from src to dst using prefix sums as write indices */
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t digit = (uint8_t)((src[i].sort_key >> shift) & 0xFF);
            dst[histogram[digit]] = src[i];
            ++histogram[digit];
        }

        /* Swap src/dst pointers */
        nt_render_item_t *tmp = src;
        src = dst;
        dst = tmp;
    }

    /* If final sorted data ended up in scratch, copy back to items */
    if (src != items) {
        memcpy(items, src, count * sizeof(nt_render_item_t));
    }
}
