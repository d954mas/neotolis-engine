#ifndef NT_ARENA_H
#define NT_ARENA_H

#include <stddef.h>
#include <stdint.h>

/* Fixed-capacity bump arena over owner-provided memory. No growth, no per-alloc free;
 * reset() drops everything, rewind() drops everything allocated after a mark.
 * Fields are readable; only the functions below change them.
 *
 *   size_t mark = arena.used;
 *   tmp = nt_arena_alloc(&arena, n, 4);   // valid until rewind/reset
 *   nt_arena_rewind(&arena, mark);
 *
 * A rewind also frees whatever other code allocated from this arena after the
 * mark: rewind only around allocations whose consumers end before it. */
typedef struct nt_arena {
    uint8_t *base;
    size_t size;
    size_t used;
    size_t high_water; /* peak `used` since init; reset and rewind keep it */
} nt_arena_t;

/* base: aligned to _Alignof(max_align_t), size > 0. The owner keeps the memory alive
 * and frees it; the arena never allocates. */
void nt_arena_init(nt_arena_t *arena, void *base, size_t size);

/* align: power of 2, <= _Alignof(max_align_t). Asserts when the arena is full. */
void *nt_arena_alloc(nt_arena_t *arena, size_t size, size_t align);

/* Checked-multiply variant: asserts elem_size*count doesn't overflow. */
void *nt_arena_alloc_array(nt_arena_t *arena, size_t elem_size, size_t count, size_t align);

/* Pointers handed out before the reset become stale. */
void nt_arena_reset(nt_arena_t *arena);

/* mark: an earlier `used` of this arena, <= the current `used`. Pointers allocated
 * after the mark become stale. */
void nt_arena_rewind(nt_arena_t *arena, size_t mark);

#endif /* NT_ARENA_H */
