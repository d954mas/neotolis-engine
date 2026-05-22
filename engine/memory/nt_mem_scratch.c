#include "memory/nt_mem_scratch.h"

#include <stdbool.h>
#include <stddef.h>
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
    /* malloc guarantees max_align_t alignment; higher would need over-allocate. */
    NT_ASSERT(align <= _Alignof(max_align_t) && "nt_mem_scratch_alloc: align exceeds malloc guarantee");
    NT_ASSERT(size > 0 && "nt_mem_scratch_alloc: size must be > 0");
    NT_ASSERT(s_scratch.used <= SIZE_MAX - (align - 1U) && "nt_mem_scratch_alloc: align bump would overflow");
    const size_t aligned = (s_scratch.used + (align - 1U)) & ~(align - 1U);
    NT_ASSERT(aligned <= s_scratch.size && size <= s_scratch.size - aligned && "nt_mem_scratch_alloc: out of space; raise nt_mem_scratch_init size");
    void *p = s_scratch.base + aligned;
    s_scratch.used = aligned + size;
    return p;
}

void *nt_mem_scratch_alloc_array(size_t elem_size, size_t count, size_t align) {
    NT_ASSERT(elem_size > 0 && "nt_mem_scratch_alloc_array: elem_size must be > 0");
    NT_ASSERT(count > 0 && "nt_mem_scratch_alloc_array: count must be > 0");
    NT_ASSERT(count <= SIZE_MAX / elem_size && "nt_mem_scratch_alloc_array: count * elem_size overflow");
    return nt_mem_scratch_alloc(elem_size * count, align);
}

#ifdef NT_TEST_ACCESS
size_t nt_mem_scratch_test_used(void) { return s_scratch.used; }
size_t nt_mem_scratch_test_size(void) { return s_scratch.size; }
#endif
