/* Web (Emscripten) memory probe behind nt_platform_memory_usage().
 * `used` = allocator in-use bytes; `reserved` = total committed heap. */
#include "core/nt_platform.h"

#include <emscripten/heap.h>
#include <malloc.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    /* mallinfo()'s fields are `int`, so uordblks wraps/saturates past 2GB and reads negative — a wrapped
       int becomes a wildly wrong NT_METRICS_MEM_TOTAL sample. mallinfo2() (size_t fields) is the
       documented replacement; fall back to mallinfo() only on old toolchains without it. */
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_major__) && (__EMSCRIPTEN_major__ > 3 || (__EMSCRIPTEN_major__ == 3 && __EMSCRIPTEN_minor__ >= 1))
    struct mallinfo2 mi = mallinfo2();
    mem.used = (size_t)mi.uordblks; /* already size_t; no widen-from-signed */
#else
    struct mallinfo mi = mallinfo();
    mem.used = (size_t)(unsigned int)mi.uordblks; /* read as unsigned before widening */
#endif
    mem.reserved = (size_t)emscripten_get_heap_size();
    return mem;
}
