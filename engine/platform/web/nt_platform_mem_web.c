/* Web (Emscripten) memory probe behind nt_platform_memory_usage().
 * `used` = allocator in-use bytes; `reserved` = total committed heap. */
#include "core/nt_platform.h"

#include <emscripten/heap.h>
#include <malloc.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    /* mallinfo().uordblks is `int`, but reading its bit pattern as unsigned before widening keeps it
       correct across the full WASM32 (4 GB) address space — the whole reachable heap on wasm32. We do
       not use mallinfo2(): the pinned Emscripten toolchain does not declare it, and the unsigned read
       already covers wasm32, so the size_t variant buys nothing here. Dev-only probe. */
    struct mallinfo mi = mallinfo();
    mem.used = (size_t)(unsigned int)mi.uordblks;
    mem.reserved = (size_t)emscripten_get_heap_size();
    return mem;
}
