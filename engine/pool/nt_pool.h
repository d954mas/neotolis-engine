#ifndef NT_POOL_H
#define NT_POOL_H

#include "core/nt_types.h"

/*
 * Generational handle pool.
 *
 * Handle encoding: upper 16 bits = generation, lower 16 bits = slot index.
 * Slot index 0 is reserved (invalid handle = 0). Max capacity: 65535.
 *
 * O(1) alloc (pop free stack), O(1) free (push free stack),
 * O(1) valid check (compare full id). Stale handles detected via generation.
 */

#define NT_POOL_SLOT_SHIFT 16
#define NT_POOL_SLOT_MASK 0xFFFF

typedef struct {
    uint32_t id; /* generation << 16 | slot_index, 0 when never used */
} nt_pool_slot_t;

typedef struct {
    nt_pool_slot_t *slots; /* [capacity+1], index 0 reserved */
    uint32_t *free_queue;  /* stack of free slot indices */
    uint32_t queue_top;    /* next free position (stack pointer) */
    uint32_t capacity;
} nt_pool_t;

/* Lifecycle */
void nt_pool_init(nt_pool_t *pool, uint32_t capacity);
void nt_pool_shutdown(nt_pool_t *pool);

/* Alloc / Free / Valid */
uint32_t nt_pool_alloc(nt_pool_t *pool);
void nt_pool_free(nt_pool_t *pool, uint32_t id);
bool nt_pool_valid(const nt_pool_t *pool, uint32_t id);

/* Extract slot index from handle */
static inline uint32_t nt_pool_slot_index(uint32_t id) { return id & NT_POOL_SLOT_MASK; }

/*
 * Check if slot at given index is alive (has active handle).
 * Use for iterating all live slots:
 *   for (uint32_t i = 1; i <= pool.capacity; i++)
 *       if (nt_pool_slot_alive(&pool, i)) { ... }
 */
static inline bool nt_pool_slot_alive(const nt_pool_t *pool, uint32_t index) { return (pool->slots[index].id & NT_POOL_SLOT_MASK) != 0; }

#endif /* NT_POOL_H */
