#include "ui/nt_ui_state.h"

#include <string.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_internal.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void *nt_ui_state(nt_ui_context_t *ctx, uint32_t id, uint32_t size, uint32_t tag) {
    NT_ASSERT(ctx != NULL && "nt_ui_state: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_state: id 0 reserved (empty-slot sentinel)");
    NT_ASSERT(size <= (uint32_t)NT_UI_STATE_PAYLOAD_MAX && "nt_ui_state: size > payload max; store a game-owned pointer instead");

    /* Two passes over the window: clear() leaves holes mid-chain, so the id must be searched
     * in the FULL window before claiming an earlier hole (else one id lands in two cells).
     * Track the stalest occupied slot as the eviction fallback when the window is full. */
    const uint16_t tick = (uint16_t)ctx->current_generation;
    const uint32_t mask = ctx->state_slots - 1U;
    const uint32_t base = id & mask;
    nt_ui_state_cell_t *first_empty = NULL;
    nt_ui_state_cell_t *stalest = NULL;
    uint16_t stalest_age = 0U;
    for (uint32_t k = 0; k < ctx->state_probe_max; ++k) {
        nt_ui_state_cell_t *c = &ctx->state_pool[(base + k) & mask];
        if (c->id == id) {
            NT_ASSERT(c->size == size && "nt_ui_state: id reused with a different size (two widgets colliding on one id?)");
            NT_ASSERT(c->tag == tag && "nt_ui_state: id reused by a different widget tag (two widgets colliding on one id)");
            c->last_touch = tick;
            return c->payload;
        }
        if (c->id == 0U) {
            if (first_empty == NULL) {
                first_empty = c;
            }
        } else {
            /* Wrap-safe age: ticks behind the current frame. Least-recently-touched wins. */
            const uint16_t age = (uint16_t)(tick - c->last_touch);
            if (stalest == NULL || age > stalest_age) {
                stalest = c;
                stalest_age = age;
            }
        }
    }
    nt_ui_state_cell_t *target = first_empty;
    if (target == NULL) {
        /* Window full: evict the stalest slot (mirrors nt_ui_anim). Counter surfaces the
         * degradation so games can raise state_slots / state_probe_max. stalest is non-NULL:
         * no empty slot means every probed cell was occupied (probe_max >= 1). */
        NT_ASSERT(stalest != NULL && "nt_ui_state: full window must yield a stalest victim");
        ctx->state_evictions++;
        target = stalest;
    }
    target->id = id;
    target->size = size;
    target->tag = tag;
    target->last_touch = tick;
    memset(target->payload, 0, sizeof target->payload);
    return target->payload;
}

void *nt_ui_state_find(nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_find: ctx must be non-NULL");
    if (id == 0U) {
        return NULL;
    }
    /* Full-window scan — clear() leaves holes mid-chain, an early empty does NOT mean absent. */
    const uint32_t mask = ctx->state_slots - 1U;
    const uint32_t base = id & mask;
    for (uint32_t k = 0; k < ctx->state_probe_max; ++k) {
        nt_ui_state_cell_t *c = &ctx->state_pool[(base + k) & mask];
        if (c->id == id) {
            return c->payload;
        }
    }
    return NULL;
}

bool nt_ui_state_has_tag(const nt_ui_context_t *ctx, uint32_t id, uint32_t tag) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_has_tag: ctx must be non-NULL");
    if (id == 0U) {
        return false;
    }
    const uint32_t mask = ctx->state_slots - 1U;
    const uint32_t base = id & mask;
    for (uint32_t k = 0; k < ctx->state_probe_max; ++k) {
        const nt_ui_state_cell_t *c = &ctx->state_pool[(base + k) & mask];
        if (c->id == id) {
            return c->tag == tag;
        }
    }
    return false;
}

void nt_ui_state_clear(nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_clear: ctx must be non-NULL");
    if (id == 0U) {
        return; /* no-op */
    }
    const uint32_t mask = ctx->state_slots - 1U;
    const uint32_t base = id & mask;
    for (uint32_t k = 0; k < ctx->state_probe_max; ++k) {
        nt_ui_state_cell_t *c = &ctx->state_pool[(base + k) & mask];
        if (c->id == id) {
            /* Leave payload bytes — next create zeroes them. */
            c->id = 0U;
            c->size = 0U;
            c->tag = 0U;
            return;
        }
    }
}

void nt_ui_state_clear_all(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_clear_all: ctx must be non-NULL");
    for (uint32_t i = 0; i < ctx->state_slots; ++i) {
        ctx->state_pool[i].id = 0U;
        ctx->state_pool[i].size = 0U;
        ctx->state_pool[i].tag = 0U;
    }
}

uint32_t nt_ui_state_used_slots(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_used_slots: ctx must be non-NULL");
    uint32_t n = 0;
    for (uint32_t i = 0; i < ctx->state_slots; ++i) {
        if (ctx->state_pool[i].id != 0U) {
            ++n;
        }
    }
    return n;
}

uint32_t nt_ui_state_used_bytes(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_used_bytes: ctx must be non-NULL");
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < ctx->state_slots; ++i) {
        if (ctx->state_pool[i].id != 0U) {
            bytes += ctx->state_pool[i].size;
        }
    }
    return bytes;
}

uint32_t nt_ui_state_evictions(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_evictions: ctx must be non-NULL");
    return ctx->state_evictions;
}
