#include "entity/nt_entity.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"

/* ---- Internal state ---- */

static struct {
    uint16_t *generations; /* [max_entities + 1] */
    bool *alive;           /* [max_entities + 1] */
    bool *enabled;         /* [max_entities + 1] */
    uint16_t *free_queue;  /* stack of free slot indices */
    uint16_t queue_top;    /* stack pointer */
    uint16_t max_entities; /* from descriptor */

    nt_comp_storage_reg_t registrations[NT_MAX_COMP_STORAGES];
    uint8_t reg_count;
    bool initialized;
} s_entity;

/* ---- Helpers ---- */

static inline nt_entity_t entity_make(uint16_t index, uint16_t gen) { return (nt_entity_t){.id = ((uint32_t)gen << 16) | index}; }

/* ---- Lifecycle ---- */

nt_result_t nt_entity_init(const nt_entity_desc_t *desc) {
    if (!desc || desc->max_entities == 0) {
        return NT_ERR_INVALID_ARG;
    }

    memset(&s_entity, 0, sizeof(s_entity));
    s_entity.max_entities = desc->max_entities;

    uint32_t count = (uint32_t)desc->max_entities + 1;
    s_entity.generations = (uint16_t *)calloc(count, sizeof(uint16_t));
    s_entity.alive = (bool *)calloc(count, sizeof(bool));
    s_entity.enabled = (bool *)calloc(count, sizeof(bool));
    s_entity.free_queue = (uint16_t *)malloc(desc->max_entities * sizeof(uint16_t));

    if (!s_entity.generations || !s_entity.alive || !s_entity.enabled || !s_entity.free_queue) {
        nt_entity_shutdown();
        return NT_ERR_INIT_FAILED;
    }

    /* Fill free queue: stack with lowest index on top (first alloc gets index 1) */
    s_entity.queue_top = desc->max_entities;
    for (uint16_t i = 0; i < desc->max_entities; i++) {
        s_entity.free_queue[i] = desc->max_entities - i; /* stack: top has lowest index */
    }

    s_entity.initialized = true;
    return NT_OK;
}

void nt_entity_shutdown(void) {
    free(s_entity.generations);
    free(s_entity.alive);
    free(s_entity.enabled);
    free(s_entity.free_queue);
    memset(&s_entity, 0, sizeof(s_entity));
}

/* ---- Entity operations ---- */

nt_entity_t nt_entity_create(void) {
    NT_ASSERT(s_entity.initialized);
    if (s_entity.queue_top == 0) {
        return NT_ENTITY_INVALID; /* pool full */
    }

    s_entity.queue_top--;
    uint16_t index = s_entity.free_queue[s_entity.queue_top];

    /* Increment generation (starts at 1 for first allocation) */
    s_entity.generations[index]++;
    uint16_t gen = s_entity.generations[index];

    s_entity.alive[index] = true;
    s_entity.enabled[index] = true;

    return entity_make(index, gen);
}

void nt_entity_destroy(nt_entity_t entity) {
    uint16_t index = nt_entity_index(entity);
    uint16_t gen = nt_entity_generation(entity);

    /* Bounds check */
    NT_ASSERT_ALWAYS(index > 0 && index <= s_entity.max_entities);

    /* Double destroy: slot already dead */
    if (!s_entity.alive[index]) {
        NT_ASSERT(false); /* debug: catch double destroy */
        return;           /* release: silent ignore */
    }

    /* Stale handle: slot alive but generation mismatch (old handle for recycled slot) */
    NT_ASSERT_ALWAYS(s_entity.generations[index] == gen);

    /* Call on_destroy callbacks WHILE entity is still alive.
       Callbacks may call comp_get() which asserts is_alive. */
    for (uint8_t i = 0; i < s_entity.reg_count; i++) {
        s_entity.registrations[i].on_destroy(entity);
    }

    /* NOW mark dead: bump generation, clear flags, return slot to free queue */
    s_entity.generations[index]++;
    s_entity.alive[index] = false;
    s_entity.enabled[index] = false;

    s_entity.free_queue[s_entity.queue_top] = index;
    s_entity.queue_top++;
}

bool nt_entity_is_alive(nt_entity_t entity) {
    if (entity.id == 0) {
        return false;
    }
    uint16_t index = nt_entity_index(entity);
    if (index == 0 || index > s_entity.max_entities) {
        return false;
    }
    return s_entity.alive[index] && s_entity.generations[index] == nt_entity_generation(entity);
}

void nt_entity_set_enabled(nt_entity_t entity, bool enabled) {
    NT_ASSERT_ALWAYS(nt_entity_is_alive(entity));
    uint16_t index = nt_entity_index(entity);
    s_entity.enabled[index] = enabled;
}

bool nt_entity_is_enabled(nt_entity_t entity) {
    NT_ASSERT_ALWAYS(nt_entity_is_alive(entity));
    uint16_t index = nt_entity_index(entity);
    return s_entity.enabled[index];
}

/* ---- Storage registration ---- */

void nt_entity_register_storage(const nt_comp_storage_reg_t *reg) {
    NT_ASSERT(reg && reg->name && reg->has && reg->on_destroy);
    NT_ASSERT(s_entity.reg_count < NT_MAX_COMP_STORAGES);
    s_entity.registrations[s_entity.reg_count] = *reg;
    s_entity.reg_count++;
}

/* ---- Query ---- */

uint16_t nt_entity_max(void) { return s_entity.max_entities; }
