#ifndef NT_SORT_H
#define NT_SORT_H

#include <stdint.h>
#include <string.h>

/**
 * NT_SORT_DEFINE — generate a typed LSD radix sort function.
 *
 * Produces a function that sorts an array of @p item_type in ascending order
 * by the `sort_key` field (must be uint64_t, can be at any offset).
 *
 * @param func_name  Name of the generated function.
 * @param item_type  Struct type to sort. Must have a `uint64_t sort_key` field.
 *
 * Generated signature:
 *   void func_name(item_type *items, uint32_t count, item_type *scratch);
 *
 * Properties:
 * - Stable sort (equal keys preserve input order).
 * - Zero heap allocation: uses @p scratch + 8 KB stack histograms.
 * - Pre-scan optimization: one pass builds all 8 histograms, skips uniform digits.
 * - Result always lands in @p items (no trailing memcpy on the common path).
 *
 * Example:
 *   typedef struct { uint64_t sort_key; uint32_t id; } my_item_t;
 *   NT_SORT_DEFINE(my_sort, my_item_t)
 *   // ...
 *   my_sort(items, count, scratch);
 */

/* clang-format off */
#define NT_SORT_DEFINE(func_name, item_type)                                                  \
void func_name(item_type *items, uint32_t count, item_type *scratch) {                        \
    if (count < 2) { return; }                                                                \
                                                                                              \
    /* Phase 1: Build all 8 histograms in a single pass (8 KB on stack). */                   \
    uint32_t histograms_[8][256] = {{0}};                                                     \
    for (uint32_t i_ = 0; i_ < count; ++i_) {                                                \
        uint64_t k_ = items[i_].sort_key;                                                     \
        ++histograms_[0][(uint8_t)(k_)];                                                      \
        ++histograms_[1][(uint8_t)(k_ >> 8)];                                                 \
        ++histograms_[2][(uint8_t)(k_ >> 16)];                                                \
        ++histograms_[3][(uint8_t)(k_ >> 24)];                                                \
        ++histograms_[4][(uint8_t)(k_ >> 32)];                                                \
        ++histograms_[5][(uint8_t)(k_ >> 40)];                                                \
        ++histograms_[6][(uint8_t)(k_ >> 48)];                                                \
        ++histograms_[7][(uint8_t)(k_ >> 56)];                                                \
    }                                                                                         \
                                                                                              \
    /* Phase 2: Determine which passes can be skipped (uniform digit). */                     \
    int active_[8];                                                                           \
    int active_count_ = 0;                                                                    \
    for (int p_ = 0; p_ < 8; ++p_) {                                                         \
        active_[p_] = 1;                                                                      \
        for (int b_ = 0; b_ < 256; ++b_) {                                                   \
            if (histograms_[p_][b_] == count) { active_[p_] = 0; break; }                     \
        }                                                                                     \
        if (active_[p_]) { ++active_count_; }                                                 \
    }                                                                                         \
    if (active_count_ == 0) { return; }                                                       \
                                                                                              \
    /* Phase 3: If odd active passes, pre-copy so result lands in items. */                   \
    item_type *src_ = items;                                                                  \
    item_type *dst_ = scratch;                                                                \
    if (active_count_ & 1) {                                                                  \
        memcpy(scratch, items, count * sizeof(item_type));                                    \
        src_ = scratch;                                                                       \
        dst_ = items;                                                                         \
    }                                                                                         \
                                                                                              \
    /* Phase 4: Execute only active scatter passes. */                                        \
    for (int p_ = 0; p_ < 8; ++p_) {                                                         \
        if (!active_[p_]) { continue; }                                                       \
        int shift_ = p_ * 8;                                                                  \
        uint32_t *h_ = histograms_[p_];                                                       \
        uint32_t sum_ = 0;                                                                    \
        for (int b_ = 0; b_ < 256; ++b_) {                                                   \
            uint32_t c_ = h_[b_]; h_[b_] = sum_; sum_ += c_;                                 \
        }                                                                                     \
        for (uint32_t i_ = 0; i_ < count; ++i_) {                                            \
            uint8_t d_ = (uint8_t)((src_[i_].sort_key >> shift_) & 0xFF);                     \
            dst_[h_[d_]] = src_[i_];                                                          \
            ++h_[d_];                                                                         \
        }                                                                                     \
        item_type *tmp_ = src_; src_ = dst_; dst_ = tmp_;                                     \
    }                                                                                         \
}
/* clang-format on */

#endif /* NT_SORT_H */
