#include "ui/nt_ui_state.h"

#include <string.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_internal.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void *nt_ui_state(nt_ui_context_t *ctx, uint32_t id, uint32_t size, uint32_t tag) {
    NT_ASSERT(ctx != NULL && "nt_ui_state: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_state: id 0 reserved (empty-slot sentinel)");
    NT_ASSERT(size <= (uint32_t)NT_UI_STATE_PAYLOAD_MAX && "nt_ui_state: size > payload max; store a game-owned pointer instead (D-59-09)");

    const uint32_t base = id & (uint32_t)(NT_UI_STATE_SLOTS - 1);
    for (uint32_t k = 0; k < NT_UI_STATE_PROBE_MAX; ++k) {
        nt_ui_state_cell_t *c = &ctx->state_pool[(base + k) & (uint32_t)(NT_UI_STATE_SLOTS - 1)];
        if (c->id == id) {
            NT_ASSERT(c->size == size && "nt_ui_state: id reused with a different size (D-59-08 — two widgets colliding on one id?)");
            NT_ASSERT(c->tag == tag && "nt_ui_state: id reused by a different widget tag (two widgets colliding on one id)");
            return c->payload;
        }
        if (c->id == 0U) {
            c->id = id;
            c->size = size;
            c->tag = tag;
            memset(c->payload, 0, sizeof c->payload);
            return c->payload;
        }
    }
    /* No eviction: the game clears on screen close or raises NT_UI_STATE_SLOTS. */
    NT_ASSERT(0 && "nt_ui_state: pool overflow — clear on screen close or raise NT_UI_STATE_SLOTS (D-59-07)");
    return NULL;
}

void *nt_ui_state_find(nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_find: ctx must be non-NULL");
    if (id == 0U) {
        return NULL;
    }
    const uint32_t base = id & (uint32_t)(NT_UI_STATE_SLOTS - 1);
    for (uint32_t k = 0; k < NT_UI_STATE_PROBE_MAX; ++k) {
        nt_ui_state_cell_t *c = &ctx->state_pool[(base + k) & (uint32_t)(NT_UI_STATE_SLOTS - 1)];
        if (c->id == id) {
            return c->payload;
        }
        if (c->id == 0U) {
            return NULL; /* empty cell in the probe chain => id absent */
        }
    }
    return NULL;
}

bool nt_ui_state_has_tag(const nt_ui_context_t *ctx, uint32_t id, uint32_t tag) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_has_tag: ctx must be non-NULL");
    if (id == 0U) {
        return false;
    }
    const uint32_t base = id & (uint32_t)(NT_UI_STATE_SLOTS - 1);
    for (uint32_t k = 0; k < NT_UI_STATE_PROBE_MAX; ++k) {
        const nt_ui_state_cell_t *c = &ctx->state_pool[(base + k) & (uint32_t)(NT_UI_STATE_SLOTS - 1)];
        if (c->id == id) {
            return c->tag == tag;
        }
        if (c->id == 0U) {
            return false;
        }
    }
    return false;
}

void nt_ui_state_clear(nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_clear: ctx must be non-NULL");
    if (id == 0U) {
        return; /* no-op */
    }
    const uint32_t base = id & (uint32_t)(NT_UI_STATE_SLOTS - 1);
    for (uint32_t k = 0; k < NT_UI_STATE_PROBE_MAX; ++k) {
        nt_ui_state_cell_t *c = &ctx->state_pool[(base + k) & (uint32_t)(NT_UI_STATE_SLOTS - 1)];
        if (c->id == id) {
            /* Leave payload bytes — next create zeroes them. */
            c->id = 0U;
            c->size = 0U;
            c->tag = 0U;
            return;
        }
        if (c->id == 0U) {
            return; /* absent */
        }
    }
}

void nt_ui_state_clear_all(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_clear_all: ctx must be non-NULL");
    for (uint32_t i = 0; i < (uint32_t)NT_UI_STATE_SLOTS; ++i) {
        ctx->state_pool[i].id = 0U;
        ctx->state_pool[i].size = 0U;
        ctx->state_pool[i].tag = 0U;
    }
}

uint32_t nt_ui_state_used_slots(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_used_slots: ctx must be non-NULL");
    uint32_t n = 0;
    for (uint32_t i = 0; i < (uint32_t)NT_UI_STATE_SLOTS; ++i) {
        if (ctx->state_pool[i].id != 0U) {
            ++n;
        }
    }
    return n;
}

uint32_t nt_ui_state_used_bytes(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_state_used_bytes: ctx must be non-NULL");
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < (uint32_t)NT_UI_STATE_SLOTS; ++i) {
        if (ctx->state_pool[i].id != 0U) {
            bytes += ctx->state_pool[i].size;
        }
    }
    return bytes;
}
