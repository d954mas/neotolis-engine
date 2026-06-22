/* Web (Emscripten) memory probe behind nt_platform_memory_usage().
 * `used` = allocator in-use bytes; `reserved` = total committed heap. */
#include "core/nt_platform.h"

#include <emscripten/heap.h>
#include <malloc.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    /* uordblks (size_t on wasm32) = allocator in-use bytes; mallinfo2 not declared on the pinned toolchain. */
    struct mallinfo mi = mallinfo();
    mem.used = (size_t)mi.uordblks;
    mem.reserved = (size_t)emscripten_get_heap_size();
    return mem;
}
