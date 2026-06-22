#ifndef NT_PLATFORM_H
#define NT_PLATFORM_H

#include <stddef.h>

/* Platform detection -- maps compiler defines to engine defines */
#if defined(__EMSCRIPTEN__)
#define NT_PLATFORM_WEB 1
#elif defined(_WIN32)
#define NT_PLATFORM_WIN 1
#else
/* Linux, macOS, other POSIX -- native builds for CI and development */
#define NT_PLATFORM_NATIVE 1
#endif

/* Build configuration -- maps standard defines to engine defines */
#ifdef NDEBUG
#define NT_RELEASE 1
#else
#define NT_DEBUG 1
#endif

/* Process memory probe (platform abstraction: never call the OS from a module).
 * `used` = in-use bytes: native RSS, web allocator in-use bytes (mallinfo uordblks, NOT RSS).
 * `reserved` = bytes committed by the allocator (web heap size); on native there
 * is no cheap distinct reserve figure, so `reserved` mirrors `used`. */
typedef struct {
    size_t used;
    size_t reserved;
} nt_platform_mem_t;

nt_platform_mem_t nt_platform_memory_usage(void);

#endif /* NT_PLATFORM_H */
