#include "pool/nt_pool.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_pool_init(nt_pool_t *pool, uint16_t capacity) {
    NT_ASSERT(pool);
    NT_ASSERT(capacity > 0);

    pool->slots = (nt_pool_slot_t *)calloc(capacity + 1, sizeof(nt_pool_slot_t));
    NT_ASSERT(pool->slots); /* alloc fail at init = fatal */

    pool->free_queue = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    if (!pool->free_queue) {
        free(pool->slots);
        pool->slots = NULL;
        NT_ASSERT(pool->free_queue); /* alloc fail at init = fatal */
    }

    /* Fill free queue: stack with indices 1..capacity, lowest index on top */
    pool->queue_top = capacity;
    for (uint32_t i = 0; i < capacity; i++) {
        pool->free_queue[i] = capacity - i;
    }

    pool->capacity = capacity;
}

void nt_pool_shutdown(nt_pool_t *pool) {
    if (!pool) {
        return;
    }
    free(pool->slots);
    free(pool->free_queue);
    memset(pool, 0, sizeof(*pool));
}

uint32_t nt_pool_alloc(nt_pool_t *pool) {
    if (pool->queue_top == 0) {
        return 0; /* pool full */
    }
    pool->queue_top--;
    uint32_t slot_index = pool->free_queue[pool->queue_top];

    /* Increment generation (starts at 1 for first allocation) */
    uint32_t prev_gen = pool->slots[slot_index].id >> NT_POOL_SLOT_SHIFT;
    uint32_t new_gen = prev_gen + 1;
    /* Guard against generation wraparound to 0 */
    if (new_gen == 0) {
        new_gen = 1;
    }

    uint32_t id = (new_gen << NT_POOL_SLOT_SHIFT) | slot_index;
    pool->slots[slot_index].id = id;
    return id;
}

void nt_pool_free(nt_pool_t *pool, uint32_t id) {
    uint32_t slot_index = id & NT_POOL_SLOT_MASK;
    if (slot_index == 0 || slot_index > pool->capacity) {
        return;
    }
    if (pool->slots[slot_index].id != id) {
        return; /* stale handle */
    }
    uint32_t gen = (id >> NT_POOL_SLOT_SHIFT) + 1;
    if (gen == 0) {
        gen = 1;
    }
    pool->slots[slot_index].id = gen << NT_POOL_SLOT_SHIFT;
    NT_ASSERT(pool->queue_top < pool->capacity); /* double-free or corruption */
    pool->free_queue[pool->queue_top] = slot_index;
    pool->queue_top++;
}

bool nt_pool_valid(const nt_pool_t *pool, uint32_t id) {
    if (id == 0) {
        return false;
    }
    uint32_t slot_index = id & NT_POOL_SLOT_MASK;
    if (slot_index == 0 || slot_index > pool->capacity) {
        return false;
    }
    return pool->slots[slot_index].id == id;
}
