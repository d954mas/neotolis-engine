/* Web (Emscripten) memory probe behind nt_platform_memory_usage().
 * `used` = allocator in-use bytes; `reserved` = total committed heap. */
#include "core/nt_platform.h"

#include <emscripten/heap.h>
#include <malloc.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    /* uordblks = allocator in-use bytes. struct mallinfo's fields are int on the pinned emsdk 3.1.51
       (the size_t-fielded mallinfo2 is not declared there); a >2GiB heap can read negative, so clamp. */
    struct mallinfo mi = mallinfo();
    mem.used = mi.uordblks > 0 ? (size_t)mi.uordblks : 0;
    mem.reserved = (size_t)emscripten_get_heap_size();
    return mem;
}
