#include "memory/nt_arena.h"

#include <stddef.h>
#include <stdlib.h>

#include "core/nt_align.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"

/* Scratch shares this TU with the arena so its wrappers inline the arena calls. */

// #region arena
void nt_arena_init(nt_arena_t *arena, void *base, size_t size) {
    NT_ASSERT(base != NULL && size > 0 && "nt_arena_init: base must be non-NULL and size > 0");
    /* Offsets are aligned relative to base, so base carries the max_align_t guarantee. */
    NT_ASSERT(((uintptr_t)base & (_Alignof(max_align_t) - 1U)) == 0 && "nt_arena_init: base must be aligned to max_align_t");
    *arena = (nt_arena_t){.base = (uint8_t *)base, .size = size};
}

void *nt_arena_alloc(nt_arena_t *arena, size_t size, size_t align) {
    NT_ASSERT(align > 0 && (align & (align - 1)) == 0 && align <= _Alignof(max_align_t) && "nt_arena_alloc: align must be a power of 2 <= max_align_t");
    NT_ASSERT(size > 0 && "nt_arena_alloc: size must be > 0");
    /* used <= size, so the align-up cannot wrap; an uninitialized arena (size 0) fails here. */
    const size_t aligned = NT_ALIGN_UP(arena->used, align);
    NT_ASSERT(aligned <= arena->size && size <= arena->size - aligned && "nt_arena_alloc: out of space; raise the arena size");
    arena->used = aligned + size;
    if (arena->used > arena->high_water) {
        arena->high_water = arena->used;
    }
    return arena->base + aligned;
}

void *nt_arena_alloc_array(nt_arena_t *arena, size_t elem_size, size_t count, size_t align) {
    NT_ASSERT(elem_size > 0 && count > 0 && "nt_arena_alloc_array: elem_size and count must be > 0");
    NT_ASSERT(count <= SIZE_MAX / elem_size && "nt_arena_alloc_array: count * elem_size overflow");
    return nt_arena_alloc(arena, elem_size * count, align);
}

void nt_arena_reset(nt_arena_t *arena) { arena->used = 0; }

void nt_arena_rewind(nt_arena_t *arena, size_t mark) {
    NT_ASSERT(mark <= arena->used && "nt_arena_rewind: mark is past the head (stale after reset or rewind?)");
    arena->used = mark;
}
// #endregion

// #region scratch
static nt_arena_t s_scratch;

void nt_mem_scratch_init(size_t size_bytes) {
    NT_ASSERT(s_scratch.base == NULL && "nt_mem_scratch_init: already initialized");
    nt_arena_init(&s_scratch, malloc(size_bytes), size_bytes);
}

void nt_mem_scratch_shutdown(void) {
    NT_ASSERT(s_scratch.base != NULL && "nt_mem_scratch_shutdown: not initialized");
    free(s_scratch.base);
    s_scratch = (nt_arena_t){0};
}

void nt_mem_scratch_reset(void) { nt_arena_reset(&s_scratch); }

void *nt_mem_scratch_alloc(size_t size, size_t align) { return nt_arena_alloc(&s_scratch, size, align); }

void *nt_mem_scratch_alloc_array(size_t elem_size, size_t count, size_t align) { return nt_arena_alloc_array(&s_scratch, elem_size, count, align); }

void nt_mem_scratch_rewind(size_t mark) { nt_arena_rewind(&s_scratch, mark); }

size_t nt_mem_scratch_used(void) { return s_scratch.used; }

size_t nt_mem_scratch_high_water_mark(void) { return s_scratch.high_water; }
// #endregion
