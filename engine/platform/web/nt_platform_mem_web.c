/* Web (Emscripten) memory probe behind nt_platform_memory_usage().
 * `used` = allocator in-use bytes; `reserved` = total committed heap. */
#include "core/nt_platform.h"

#include <emscripten/heap.h>
#include <malloc.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    /* uordblks = allocator in-use bytes. On the pinned Emscripten toolchain struct mallinfo.uordblks is
       already size_t (unsigned), so it fits wasm32 exactly with no int-wrap concern — a plain widening
       read. We do not use mallinfo2(): the pinned toolchain does not declare it and buys nothing here.
       Dev-only probe. */
    struct mallinfo mi = mallinfo();
    mem.used = (size_t)mi.uordblks;
    mem.reserved = (size_t)emscripten_get_heap_size();
    return mem;
}
