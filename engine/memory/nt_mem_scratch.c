#include "memory/nt_mem_scratch.h"

#include <stdbool.h>
#include <stdlib.h>

#include "core/nt_assert.h"

static struct {
    uint8_t *base;
    size_t size;
    size_t used;
    bool initialized;
} s_scratch;

void nt_mem_scratch_init(size_t size_bytes) {
    NT_ASSERT(!s_scratch.initialized && "nt_mem_scratch_init: already initialized");
    NT_ASSERT(size_bytes > 0 && "nt_mem_scratch_init: size must be > 0");
    s_scratch.base = (uint8_t *)malloc(size_bytes);
    NT_ASSERT(s_scratch.base != NULL && "nt_mem_scratch_init: malloc failed");
    s_scratch.size = size_bytes;
    s_scratch.used = 0;
    s_scratch.initialized = true;
}

void nt_mem_scratch_shutdown(void) {
    NT_ASSERT(s_scratch.initialized && "nt_mem_scratch_shutdown: not initialized");
    free(s_scratch.base);
    s_scratch.base = NULL;
    s_scratch.size = 0;
    s_scratch.used = 0;
    s_scratch.initialized = false;
}

void nt_mem_scratch_reset(void) {
    NT_ASSERT(s_scratch.initialized && "nt_mem_scratch_reset: not initialized");
    s_scratch.used = 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void *nt_mem_scratch_alloc(size_t size, size_t align) {
    NT_ASSERT(s_scratch.initialized && "nt_mem_scratch_alloc: call nt_mem_scratch_init first");
    NT_ASSERT(align > 0 && (align & (align - 1)) == 0 && "nt_mem_scratch_alloc: align must be power of 2");
    NT_ASSERT(size > 0 && "nt_mem_scratch_alloc: size must be > 0");
    const size_t aligned = (s_scratch.used + (align - 1)) & ~(align - 1);
    NT_ASSERT(aligned + size <= s_scratch.size && "nt_mem_scratch_alloc: out of space; raise nt_mem_scratch_init size");
    void *p = s_scratch.base + aligned;
    s_scratch.used = aligned + size;
    return p;
}

#ifdef NT_TEST_ACCESS
size_t nt_mem_scratch_test_used(void) { return s_scratch.used; }
size_t nt_mem_scratch_test_size(void) { return s_scratch.size; }
#endif
