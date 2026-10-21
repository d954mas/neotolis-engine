/* Native process probes behind nt_platform.h: resident memory and CPU time.
 * One branch per OS; all OS calls live here so no module touches the syscall. */
#include "core/nt_assert.h"
#include "core/nt_platform.h"

#if defined(NT_PLATFORM_WIN)
#include <windows.h>
/* psapi.h after windows.h: it depends on windows.h types. */
#include <psapi.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#if defined(__linux__)
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#else
#include <mach/mach.h>
#endif
#else
#error "nt_platform: unsupported native OS"
#endif

// #region memory
#if defined(NT_PLATFORM_WIN)
nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    PROCESS_MEMORY_COUNTERS pmc = {0};
    /* A syscall failure is a bug, not "0 bytes used" — assert rather than report a bogus figure. */
    BOOL ok = GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    NT_ASSERT(ok && "GetProcessMemoryInfo failed");
    (void)ok;
    mem.used = (size_t)pmc.WorkingSetSize;
    mem.reserved = mem.used;
    return mem;
}

#elif defined(__linux__)
/* Resident pages from /proc/self/statm field 2. strtol over scanf: scanf can't report parse errors. */
static long statm_resident_pages(void) {
    FILE *f = fopen("/proc/self/statm", "r");
    NT_ASSERT(f != NULL && "fopen(/proc/self/statm) failed");
    char buf[128];
    char *line = fgets(buf, sizeof(buf), f);
    (void)fclose(f);
    NT_ASSERT(line != NULL && "read /proc/self/statm failed");
    char *p = buf;
    (void)strtol(p, &p, 10); /* field 1 (total pages) — skip */
    char *after_total = p;
    long pages_rss = strtol(p, &p, 10); /* field 2 (resident pages) */
    NT_ASSERT(p != after_total && pages_rss >= 0 && "bad /proc/self/statm resident field");
    return pages_rss;
}

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    /* statm field 2 = resident pages; × page size = resident bytes. */
    long pages_rss = statm_resident_pages();
    long page = sysconf(_SC_PAGESIZE);
    NT_ASSERT(page > 0 && "bad page size from sysconf(_SC_PAGESIZE)");
    mem.used = (size_t)pages_rss * (size_t)page;
    mem.reserved = mem.used;
    return mem;
}

#else
nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    mach_task_basic_info_data_t info = {0};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    /* A task_info failure is a bug, not "0 bytes" — assert rather than report a bogus figure. */
    kern_return_t kr = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
    NT_ASSERT(kr == KERN_SUCCESS && "task_info(MACH_TASK_BASIC_INFO) failed");
    (void)kr;
    mem.used = (size_t)info.resident_size;
    mem.reserved = mem.used;
    return mem;
}
#endif
// #endregion

// #region cpu
#if defined(NT_PLATFORM_WIN)
/* FILETIME counts 100 ns ticks. */
static uint64_t filetime_ns(FILETIME ft) { return ((((uint64_t)ft.dwHighDateTime) << 32) | ft.dwLowDateTime) * 100U; }

nt_platform_cpu_t nt_platform_cpu_usage(void) {
    FILETIME creation;
    FILETIME exit_time;
    FILETIME kernel;
    FILETIME user;
    BOOL ok = GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user);
    NT_ASSERT(ok && "GetProcessTimes failed");
    (void)ok;
    nt_platform_cpu_t cpu = {filetime_ns(user), filetime_ns(kernel)};
    return cpu;
}

#else
static uint64_t timeval_ns(struct timeval tv) { return ((uint64_t)tv.tv_sec * 1000000000U) + ((uint64_t)tv.tv_usec * 1000U); }

nt_platform_cpu_t nt_platform_cpu_usage(void) {
    struct rusage ru;
    int rc = getrusage(RUSAGE_SELF, &ru);
    NT_ASSERT(rc == 0 && "getrusage(RUSAGE_SELF) failed");
    (void)rc;
    nt_platform_cpu_t cpu = {timeval_ns(ru.ru_utime), timeval_ns(ru.ru_stime)};
    return cpu;
}
#endif

/* Only the sum is monotonic everywhere: older Linux kernels could shift time between user and system. */
float nt_platform_cpu_percent(nt_platform_cpu_t prev, nt_platform_cpu_t cur, uint64_t wall_ns) {
    uint64_t prev_ns = prev.user_ns + prev.system_ns;
    uint64_t cur_ns = cur.user_ns + cur.system_ns;
    NT_ASSERT(wall_ns > 0 && "zero wall interval");
    NT_ASSERT(cur_ns >= prev_ns && "cpu readings out of order");
    return (float)(100.0 * (double)(cur_ns - prev_ns) / (double)wall_ns);
}
// #endregion
