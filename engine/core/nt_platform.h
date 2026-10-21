#ifndef NT_PLATFORM_H
#define NT_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

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
 * `used` = in-use bytes (native RSS; web mallinfo uordblks, NOT RSS).
 * `reserved` = allocator-committed bytes (web heap size); mirrors `used` on native. */
typedef struct {
    size_t used;
    size_t reserved;
} nt_platform_mem_t;

nt_platform_mem_t nt_platform_memory_usage(void);

#if !defined(NT_PLATFORM_WEB)
/* Process CPU time probe, native only: the browser exposes no process CPU time.
 * Cumulative since process start, microseconds, summed over all threads. */
typedef struct {
    uint64_t user_us;
    uint64_t system_us;
} nt_platform_cpu_t;

nt_platform_cpu_t nt_platform_cpu_usage(void);

/* Load over the interval between two readings, one-core base: 100 = one core fully busy,
 * above 100 with several busy threads. Pure; the caller keeps `prev` and picks the window.
 * Windows counters tick every ~15.6 ms, so windows under ~0.5 s read noisy.
 * wall_us == 0 or a counter going backwards is a caller bug and asserts. */
float nt_platform_cpu_percent(nt_platform_cpu_t prev, nt_platform_cpu_t cur, uint64_t wall_us);
#endif

#endif /* NT_PLATFORM_H */
